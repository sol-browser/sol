"""Exercise the global shortcut editor and a real Windows registration conflict."""
import ctypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import sqlite3
import threading
from uuid import uuid4

from smoke import ROOT, run


def main():
    profile = ROOT / 'artifacts' / f'little-sol-hotkey-{uuid4().hex[:8]}' / 'profile'
    database = profile / 'BrowserData' / 'settings.sqlite3'
    database.parent.mkdir(parents=True)
    preferences = dict(schema=1, searchEngine='google', downloadDirectory='', askDownload=False,
                       startup='restore', homepage='https://www.google.com/', shortcuts={},
                       littleSolBinding=dict(key=133, ctrl=True, alt=True, shift=False))
    with sqlite3.connect(database) as db:
        db.executescript('CREATE TABLE preferences(id INTEGER PRIMARY KEY, value TEXT NOT NULL);'
                         'PRAGMA application_id=1380271184; PRAGMA user_version=1;')
        db.execute('INSERT INTO preferences VALUES(1,?)', (json.dumps(preferences),))

    class Page(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.end_headers()
            self.wfile.write(b'<!doctype html><html lang="en"><title>Little Sol shortcut test</title>')

    user32 = ctypes.windll.user32
    with ThreadingHTTPServer(('127.0.0.1', 0), Page) as server:
        if not user32.RegisterHotKey(None, 1, 3, 131):  # Ctrl+Alt+F20 is taken by this test process.
            raise RuntimeError('Could not reserve Ctrl+Alt+F20 for the conflict fixture')
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            directory, events = run(f'http://127.0.0.1:{server.server_port}/',
                                    ['--probe-menu=little-sol-hotkey'], profile=profile)
        finally:
            server.shutdown()
            user32.UnregisterHotKey(None, 1)

    marker = 'RED_LITTLE_SOL_HOTKEY:'
    result = {event: next((row['details'] for row in events if row['event'] == event), {})
              for event in ('little_sol_hotkey_before', 'little_sol_hotkey_after')}
    messages = [row['details']['message'] for row in events if row['event'] == 'settings_console']
    result['editor'] = next((json.loads(msg[len(marker):]) for msg in messages if msg.startswith(marker)), {})
    with sqlite3.connect(database) as db:
        result['saved'] = json.loads(db.execute('SELECT value FROM preferences WHERE id=1').fetchone()[0]).get('littleSolBinding')
    result['passed'] = (result['little_sol_hotkey_before'] == {'registered': True}
                        and result['little_sol_hotkey_after'] == {'old_free': True, 'new_registered': True}
                        and result['editor'] == {'conflict': 409, 'afterConflict': 133, 'configured': 132, 'label': 'Ctrl+Alt+F21'}
                        and result['saved'] == dict(key=132, ctrl=True, alt=True, shift=False))
    (directory / 'little-sol-hotkey.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    if not result['passed']:
        raise SystemExit('Little Sol shortcut verification failed')


if __name__ == '__main__':
    main()
