"""Two persistent Chrome Runtime profiles in one native window, across restart."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread
from urllib.parse import parse_qs, urlsplit
from uuid import uuid4
import json

from smoke import ROOT, run


def main():
    records, requests = [], []
    class Fixture(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            url = urlsplit(self.path)
            params = parse_qs(url.query, keep_blank_values=True)
            if url.path == '/record':
                records.append({key: values[0] for key, values in params.items()})
            name = params.get('name', [''])[0]
            if url.path == '/profile':
                requests.append({'name': name, 'cookie': self.headers.get('Cookie', '')})
            body = '<!doctype html><title>Profile isolation</title><h1>Profile isolation</h1>'
            if url.path == '/profile':
                body += """<script>
                    const name = new URL(location).searchParams.get('name');
                    const previous = localStorage.getItem('profile') || '';
                    localStorage.setItem('profile', name);
                    fetch('/record?' + new URLSearchParams({name, previous, cookies:document.cookie}));
                </script>"""
            data = body.encode()
            self.send_response(200)
            if url.path == '/profile':
                self.send_header('Set-Cookie', f'profile={name}; Path=/; Max-Age=3600; SameSite=Lax')
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    profile = ROOT / 'artifacts' / ('profiles-probe-' + uuid4().hex[:8])
    with ThreadingHTTPServer(('127.0.0.1', 0), Fixture) as server:
        Thread(target=server.serve_forever, daemon=True).start()
        try:
            for restart in (False, True):
                records.clear()
                requests.clear()
                directory, rows = run(f'http://127.0.0.1:{server.server_port}/source?profiles',
                                      ['--probe-link-headers'], profile=profile)
                created = [row['details'] for row in rows if row['event'] == 'profiles_probe_created']
                assert len(created) == 2 and all(item['same_window'] and item['isolated'] for item in created), created
                finished = next(row['details'] for row in rows if row['event'] == 'profiles_probe_finished')
                assert finished == {'loaded': 2, 'closed': 2}, finished
                assert len(records) == 2, records
                assert len(requests) == 2, requests
                for request in requests:
                    assert request['cookie'] == ('profile=' + request['name'] if restart else ''), request
                for record in records:
                    assert record['previous'] == (record['name'] if restart else ''), record
                    assert record['cookies'] == 'profile=' + record['name'], record
                (directory / 'profiles.json').write_text(json.dumps({'restart': restart, 'records': records, 'created': created}, indent=2))
        finally:
            server.shutdown()


if __name__ == '__main__':
    main()
