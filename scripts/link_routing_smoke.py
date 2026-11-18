"""Ordered external link routing on Windows with CEF and two persistent profiles."""
from contextlib import closing
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Event, Thread
import json
import os
import sqlite3
import subprocess
from uuid import uuid4

from smoke import ROOT, run


EXE = Path(os.environ.get('RED_BROWSER_TEST_EXE', ROOT / 'build/Release/apps/desktop/Release/sol.exe'))


def save_routing(database, routing):
    # Seed only Sol-owned preferences in the disposable profile. CEF owns all cookies.
    with closing(sqlite3.connect(database)) as db, db:
        db.executescript('''PRAGMA application_id=1380271184; PRAGMA user_version=1;
            CREATE TABLE IF NOT EXISTS preferences(id INTEGER PRIMARY KEY, value TEXT NOT NULL);''')
        value = dict(schema=1, searchEngine='google', downloadDirectory='', askDownload=False,
                     startup='restore', homepage='https://example.com/', shortcuts={}, linkRouting=routing)
        db.execute('INSERT OR REPLACE INTO preferences VALUES(1, ?)', (json.dumps(value),))


def assert_tab(database, url, space, profile):
    with closing(sqlite3.connect(database)) as db:
        tabs = db.execute('SELECT space_id, profile_id FROM nodes WHERE url=? AND archived=0', (url,)).fetchall()
    assert (space, profile) in tabs, (url, tabs)


def main():
    paths = {}
    cold_arrived = Event()

    class Fixture(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            paths.setdefault(self.path, []).append(self.headers.get('Cookie', ''))
            body = b'<!doctype html><title>Link routing fixture</title>'
            self.send_response(200)
            self.send_header('Content-Type', 'text/html')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body)))
            if self.path in ('/prime-personal', '/work/prime'):
                name = 'work' if self.path == '/work/prime' else 'personal'
                self.send_header('Set-Cookie', f'session={name}; Path=/; Max-Age=3600; HttpOnly; SameSite=Lax')
            self.end_headers()
            self.wfile.write(body)
            if self.path == '/work/cold':
                cold_arrived.set()

    folder = ROOT / 'artifacts' / ('link-routing-' + uuid4().hex[:8])
    folder.mkdir(parents=True)
    profile = folder / 'profile'
    workspace_db = profile / 'BrowserData/workspace.sqlite3'
    settings_db = profile / 'BrowserData/settings.sqlite3'
    work, work_profile, removed = (str(uuid4()) for _ in range(3))

    with ThreadingHTTPServer(('127.0.0.1', 0), Fixture) as server:
        Thread(target=server.serve_forever, daemon=True).start()
        origin = f'http://127.0.0.1:{server.server_port}'
        try:
            _, ui_rows = run(origin + '/prime-personal', ['--workspace', '--probe-menu=link-routing'], profile=profile)
            editor = [row['details']['message'] for row in ui_rows if row['event'] == 'settings_console' and
                      row['details']['message'].startswith('LINK_ROUTING_SETTINGS')]
            assert editor == ['LINK_ROUTING_SETTINGS pass'], editor
            with closing(sqlite3.connect(settings_db)) as db:
                value = json.loads(db.execute('SELECT value FROM preferences WHERE id=1').fetchone()[0])
                assert [(rule['match'], rule['pattern']) for rule in value['linkRouting']['rules']] == [
                    ('regex', 'example.test/team/'), ('contains', 'example.test/')]
            with closing(sqlite3.connect(workspace_db)) as db, db:
                personal = db.execute('SELECT id FROM spaces WHERE position=0').fetchone()[0]
                db.execute('INSERT INTO browsing_profiles(id,name) VALUES(?,?)', (work_profile, 'Work'))
                db.execute('INSERT INTO spaces(id,name,position,profile_id) VALUES(?,?,?,?)', (work, 'Work', 1, work_profile))

            rules = [dict(match=match, pattern=pattern, destination=destination) for match, pattern, destination in [
                ('contains', '/conflict', personal),
                ('exact', origin + '/conflict', work),
                ('exact', origin + '/exact', work),
                ('regex', r'/WORK/(?:prime|cold)$', work),
                ('contains', '/resume', removed),
                ('contains', '/resume', work),
                ('regex', r'/ISSUES/\d+$', work),
                ('contains', '/issues/', personal),
                ('regex', r'/deadline/(a+)+$', work),
                ('contains', '/deadline/', personal),
            ]]
            routing = dict(fallback=personal, rules=rules)
            save_routing(settings_db, routing)
            run(origin + '/work/prime', ['--workspace', origin + '/work/prime'], profile=profile)
            assert_tab(workspace_db, origin + '/work/prime', work, work_profile)

            # Launch the first address with Sol closed, then send the rest to the running process.
            deadline = '/deadline/' + 'a' * 2100 + '!'
            requests = {'/work/cold': (work, work_profile, 'work'), '/exact': (work, work_profile, 'work'),
                        '/conflict': (personal, 'default', 'personal'), '/exact?extra=1': (personal, 'default', 'personal'),
                        '/resume': (work, work_profile, 'work'), '/fallback': (personal, 'default', 'personal'),
                        '/issues/123': (work, work_profile, 'work'), '/issues/not-a-number': (personal, 'default', 'personal'),
                        deadline: (personal, 'default', 'personal')}
            profile_arg = f'--profile-dir={profile}'
            with (folder / 'process.log').open('w', encoding='utf-8') as log:
                process = subprocess.Popen([str(EXE), '--workspace', profile_arg, f'--probe-report={folder / "native.jsonl"}',
                                            origin + '/work/cold'], cwd=EXE.parent, stdout=log, stderr=log)
                try:
                    assert cold_arrived.wait(20), f'Cold link failed to open, see {folder}'
                    warm_urls = [origin + path for path in requests if path != '/work/cold']
                    forwarded = subprocess.run([str(EXE), profile_arg, *warm_urls], cwd=EXE.parent, timeout=20)
                    # CEF_RESULT_CODE_NORMAL_EXIT_PROCESS_NOTIFIED from the pinned CEF headers.
                    assert forwarded.returncode == 24, forwarded.returncode
                    assert process.wait(timeout=25) == 0, f'Sol did not exit cleanly, see {folder}'
                finally:
                    if process.poll() is None:
                        subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True, check=False)

            for path, (space, profile_id, cookie) in requests.items():
                assert paths.get(path) == [f'session={cookie}'], (path, paths.get(path))
                assert_tab(workspace_db, origin + path, space, profile_id)
            first_requests = {path: paths[path][0] for path in requests}
            first_requests['/deadline/(long input)'] = first_requests.pop(deadline)

            rules[0], rules[1] = rules[1], rules[0]
            save_routing(settings_db, routing)
            run(origin + '/conflict', ['--workspace', origin + '/conflict'], profile=profile)
            assert paths['/conflict'] == ['session=personal', 'session=work'], paths['/conflict']
            assert_tab(workspace_db, origin + '/conflict', work, work_profile)
        finally:
            server.shutdown()

    result = {'passed': True, 'first_request_cookies': first_requests, 'conflict_after_reorder': paths['/conflict'],
              'evidence': str(folder)}
    (folder / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
