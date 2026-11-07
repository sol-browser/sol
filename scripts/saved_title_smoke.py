"""Keep a pin's home title through navigation, unload and a CEF restart."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import sqlite3
from threading import Thread
from urllib.parse import urlsplit
from uuid import uuid4

from smoke import ROOT, run


class Fixture(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        page = urlsplit(self.path).query
        title = {'home': 'Loading home', 'away': 'Away title'}.get(page, 'Title fixture')
        body = f'<!doctype html><title>{title}</title><h1>{title}</h1>'
        if page == 'home':
            body += "<script>setTimeout(() => { document.title = 'Home title'; }, 60);</script>"
        data = body.encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main():
    profile = ROOT / 'artifacts' / ('saved-title-profile-' + uuid4().hex[:8])
    database = profile / 'BrowserData' / 'workspace.sqlite3'

    def saved():
        with sqlite3.connect(database) as db:
            return db.execute("SELECT url,page_title FROM nodes WHERE title='Title regression pin' AND pinned=1").fetchone()

    with ThreadingHTTPServer(('127.0.0.1', 0), Fixture) as server:
        Thread(target=server.serve_forever, daemon=True).start()
        url = f'http://127.0.0.1:{server.server_port}/saved-title'
        try:
            _, rows = run(url, ['--probe-organization'], profile=profile)
            first = next(row['details'] for row in rows if row['event'] == 'organization_checks_pass')
            pin = next(node for node in first['nodes'] if node['title'] == 'Title regression pin')
            assert pin['currentUrl'] == url + '?away' and pin['pageTitle'] == 'Away title' and pin['live'], pin
            assert saved() == (url + '?home', 'Home title'), saved()

            _, rows = run(url, ['--probe-organization'], profile=profile)
            second = next(row['details'] for row in rows if row['event'] == 'organization_checks_pass')
            restored = next(node for node in second['nodes'] if node['id'] == pin['id'])
            assert restored['currentUrl'] == url + '?home' and restored['pageTitle'] == 'Home title' and not restored['live'], restored
            assert saved() == (url + '?home', 'Home title'), saved()
        finally:
            server.shutdown()


if __name__ == '__main__':
    main()
