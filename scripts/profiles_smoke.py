"""Exercise profile creation, Space assignment, sharing, isolation and restart."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
from urllib.parse import parse_qs, urlsplit
from uuid import uuid4
import sqlite3
import json
from smoke import ROOT, run


def main():
    requests, records, extensions = [], [], []
    class Fixture(BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            url = urlsplit(self.path)
            params = parse_qs(url.query, keep_blank_values=True)
            name = params.get('name', ['default'])[0]
            if url.path == '/profile-file':
                data = ('profile '+name).encode()
                self.send_response(200)
                self.send_header('Content-Type','application/octet-stream')
                self.send_header('Content-Disposition',f'attachment; filename="profile-{name}.txt"')
                self.send_header('Content-Length',str(len(data)))
                self.end_headers(); self.wfile.write(data)
                return
            if url.path == '/record':
                records.append({key: values[0] for key, values in params.items()})
            elif url.path == '/extension-record':
                extensions.append({key: values[0] for key, values in params.items()})
            elif url.path == '/profiles-product':
                requests.append((name, self.headers.get('Cookie', '')))
            body = '<!doctype html><title>Profile ' + name + '</title><h1>Profile fixture</h1>'
            if url.path == '/profiles-product':
                body += """<script>
                  const name = new URL(location).searchParams.get('name') || 'default';
                  const previous = localStorage.getItem('profile') || '';
                  localStorage.setItem('profile', name);
                  fetch('/record?' + new URLSearchParams({name,previous}));
                  if (name !== 'default') {
                    const a = document.createElement('a'); a.href = '/profile-file?name='+name;
                    a.download = 'profile-'+name+'.txt'; a.click();
                  }
                </script>"""
            data = body.encode()
            self.send_response(200)
            if url.path == '/profiles-product':
                self.send_header('Set-Cookie', f'profile={name}; Path=/; Max-Age=3600; SameSite=Lax')
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
    profile = ROOT / 'artifacts' / ('profiles-product-' + uuid4().hex[:8])
    with ThreadingHTTPServer(('127.0.0.1',0), Fixture) as server:
        Thread(target=server.serve_forever,daemon=True).start()
        try:
            for restart in (False,True):
                requests.clear(); records.clear(); extensions.clear()
                _, rows = run(f'http://127.0.0.1:{server.server_port}/profiles-product',
                    ['--probe-organization', f'--load-extension={ROOT / "tests/extensions/profiles-probe"}'], profile=profile)
                snapshot = next(row['details'] for row in rows if row['event'] == 'organization_checks_pass')
                for name in ('A','B'):
                    owned = [cookie for request_name,cookie in requests if request_name == name]
                    assert owned and all(cookie in ('','profile='+name) for cookie in owned), (name,requests)
                    values = [record['previous'] for record in records if record['name'] == name]
                    assert values and all(value in ('',name) for value in values), records
                    if restart: assert all(value == name for value in values), records
                    observed = [record for record in extensions if record['name'] == name]
                    assert observed, ('extension did not execute',name,extensions)
                    assert all(record['previous'] in ('',name) for record in observed), extensions
                    if restart: assert all(record['previous'] == name for record in observed), extensions
                    for record in observed:
                        tabs = json.loads(record['tabs'])
                        assert tabs and all(parse_qs(urlsplit(tab).query).get('name') == [name] for tab in tabs), extensions
                    selected = next(item['id'] for item in snapshot['profiles'] if item['name'] == 'Profile '+name)
                    database = profile / 'BrowserData' / ('Profile-'+selected) / 'history.sqlite3'
                    with sqlite3.connect(database) as db:
                        urls = [row[0] for row in db.execute('SELECT url FROM visits')]
                        files = [row[0] for row in db.execute('SELECT name FROM downloads')]
                    assert urls and all('name='+name in url for url in urls), urls
                    assert files and all(file.startswith('profile-'+name) for file in files), files
            print('Profiles: cookies, site/extension storage, extension tabs, history, downloads, move guards and restart PASS', flush=True)
        finally:
            server.shutdown()


if __name__ == '__main__': main()
