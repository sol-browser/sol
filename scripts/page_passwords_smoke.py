"""Menu > Passwords > Passwords for This Page must not crash Sol.

Chrome's IDC_MANAGE_PASSWORDS_FOR_PAGE bubble hits NOTREACHED when the page
has no password state (Chromium 156 PasswordBubbleViewBase::CreateBubble), so
Sol opens its password manager searched for the page's host instead. The page
here has no saved passwords; the item is clicked through the real menu.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading

from smoke import run


def main():
    class Page(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            body = b'<!doctype html><title>Page passwords</title><p>No saved passwords here.'
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    with ThreadingHTTPServer(('127.0.0.1', 0), Page) as server:
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            # localhost, not 127.0.0.1: an IP address is not a site to search for.
            _, events = run(f'http://localhost:{server.server_port}/', ['--probe-menu=page-passwords'])
        finally:
            server.shutdown()
    result = next((row['details'] for row in events if row['event'] == 'page_passwords_result'), None)
    print(json.dumps({'result': result}, indent=2))
    ok = result is not None and result.get('url') == 'chrome://password-manager/passwords?q=localhost'
    print(json.dumps({'passed': ok}))
    if not ok:
        raise SystemExit('Passwords for This Page did not open the searched password manager')


if __name__ == '__main__':
    main()
