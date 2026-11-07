"""Two live pages share a hostname while one changes its favicon."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
from urllib.parse import urlsplit

from smoke import run


class Fixture(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path in ('/base.svg', '/status.svg'):
            color = '#2ba24c' if url.path == '/status.svg' else '#2c54b4'
            body = f'<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><path fill="{color}" d="M0 0h32v32H0z"/></svg>'
            kind = 'image/svg+xml'
        else:
            body = '<!doctype html><title>Favicon fixture</title><link rel="icon" href="/base.svg"><h1>Favicon fixture</h1>'
            if url.query == 'actions':
                body += "<script>window.channel = new BroadcastChannel('favicon'); channel.onmessage = () => document.querySelector('link').href = '/status.svg';</script>"
            elif url.query == 'stable':
                body += "<script>window.channel = new BroadcastChannel('favicon'); addEventListener('load', () => setTimeout(() => channel.postMessage('build passed'), 300));</script>"
            kind = 'text/html; charset=utf-8'
        data = body.encode()
        self.send_response(200)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)


if __name__ == '__main__':
    with ThreadingHTTPServer(('127.0.0.1', 0), Fixture) as server:
        Thread(target=server.serve_forever, daemon=True).start()
        try:
            run(f'http://127.0.0.1:{server.server_port}/favicon-isolation', ['--probe-organization'])
        finally:
            server.shutdown()
