"""Loopback UI-only fixture with an explicitly simulated bridge; never CEF acceptance."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit
import argparse

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / 'apps/browser-ui/dist'


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        path = urlsplit(self.path).path
        if path in ('/settings.html', '/command.html', '/panel.html'):
            bridge = '/integration-bridge.js' if path == '/panel.html' else '/fixture-bridge.js'
            body = (DIST / 'index.html').read_text().replace('<head>', f'<head><script src="{bridge}"></script>').encode()
            if path == '/panel.html':
                body = body.replace(b'<html', b'<html data-ui="panel" style="background:#24252b;color-scheme:dark"', 1)
            if path == '/settings.html' and urlsplit(self.path).query == 'check=notes':
                body = body.replace(b'</body>', b'<script src="/notes-settings-ui-check.js"></script></body>')
            mime = 'text/html; charset=utf-8'
        elif path == '/fixture-bridge.js':
            body = (ROOT / 'tests/fixtures/site-search-bridge.js').read_bytes()
            mime = 'text/javascript'
        elif path == '/integration-bridge.js':
            body = (ROOT / 'tests/fixtures/integration-bridge.js').read_bytes()
            mime = 'text/javascript'
        elif path == '/notes-settings-ui-check.js':
            body = (ROOT / 'scripts/notes-settings-ui-check.js').read_bytes()
            mime = 'text/javascript'
        else:
            target = (DIST / path.lstrip('/')).resolve()
            if not target.is_relative_to(DIST) or not target.is_file():
                self.send_error(404)
                return
            body = target.read_bytes()
            mime = 'text/javascript' if target.suffix == '.js' else 'text/css' if target.suffix == '.css' else 'application/octet-stream'
        self.send_response(200)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Content-Security-Policy', "default-src 'none'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'")
        self.end_headers()
        self.wfile.write(body)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=61412)
    args = parser.parse_args()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    print(f'Simulated-bridge UI fixture: http://127.0.0.1:{server.server_port}/settings.html', flush=True)
    server.serve_forever()
