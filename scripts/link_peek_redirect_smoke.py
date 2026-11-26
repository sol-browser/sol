"""Link Peek for links that leave through the source site's own redirector.

YouTube description links are https://www.youtube.com/redirect?event=...&q=<target>
with target=_blank: the clicked address is on youtube.com and only the server's
redirect reaches the other site. The pinned source here is on localhost; its
link goes to localhost/redirect?...&q=<127.0.0.1 target>, which 302s to
127.0.0.1 (another site). The probe (--probe-link-headers) pins the tab and clicks
the link at 60,60 with a DevTools input gesture (no real mouse or keyboard).

Cases (one Sol run each; default all):
  redirect-blank   target=_blank redirector link   -> Link Peek
  redirect-tab     same-tab redirector link        -> Link Peek
  same-blank       target=_blank same-site link    -> no Link Peek (ordinary tab)
  cross-blank      target=_blank cross-site link   -> Link Peek (control)
Evidence: link_headers_opened {link_peek} from native code, and that the
destination page was requested.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, quote, urlsplit
import json
import sys
import threading
from smoke import run

CASES = {
    'redirect-blank': ('target=_blank ', 'redirect', True),
    'redirect-tab': ('', 'redirect', True),
    'same-blank': ('target=_blank ', 'same', False),
    'cross-blank': ('target=_blank ', 'cross', True),
}


def run_case(name):
    target_attr, kind, want_peek = CASES[name]
    requests = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_): pass

        def do_GET(self):
            port = self.server.server_port
            path = urlsplit(self.path).path
            requests.append(path)
            if self.path == '/set':
                self.send_response(302); self.send_header('Location', f'http://localhost:{port}/source?peek'); self.end_headers(); return
            if path == '/redirect':  # The site's own out-link redirector, as youtube.com/redirect.
                self.send_response(302); self.send_header('Location', parse_qs(urlsplit(self.path).query)['q'][0]); self.end_headers(); return
            if path == '/source':
                href = {
                    'redirect': f'http://localhost:{port}/redirect?event=video_description&redir_token=abc&q='
                                + quote(f'http://127.0.0.1:{port}/target', safe='') + '&v=xyz',
                    'same': f'http://localhost:{port}/other',
                    'cross': f'http://127.0.0.1:{port}/target',
                }[kind]
                body = (f"<!doctype html><title>Source</title><a id=l {target_attr}href='{href}' "
                        "style='position:fixed;left:0;top:0;width:200px;height:200px;display:block;background:#ccc'>link</a>").encode()
            else:
                body = b"<!doctype html><title>Destination</title>ok"
            self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Content-Length', str(len(body)))
            self.end_headers(); self.wfile.write(body)

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        _, rows = run(f'http://127.0.0.1:{server.server_port}/set', ['--probe-link-headers'], allow_failure=True)
    finally:
        server.shutdown(); server.server_close()
    opened = [r['details'] for r in rows if r['event'] == 'link_headers_opened']
    peek = opened[0]['link_peek'] if opened else None
    destination = '/target' if kind != 'same' else '/other'
    ok = peek is want_peek and destination in requests
    return {'case': name, 'passed': ok, 'link_peek': peek, 'want_peek': want_peek,
            'destination_loaded': destination in requests, 'requests': requests}


def main():
    names = [a for a in sys.argv[1:] if a in CASES] or list(CASES)
    results = [run_case(name) for name in names]
    print(json.dumps(results, indent=2))
    print(json.dumps({'passed': all(r['passed'] for r in results)}))
    if not all(r['passed'] for r in results):
        raise SystemExit('Link Peek routing for redirector links failed')


if __name__ == '__main__':
    main()
