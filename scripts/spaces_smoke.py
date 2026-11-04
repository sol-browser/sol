"""Arc-style Spaces through actual CEF.

The probe creates a "Work" space (it opens empty with its own color), opens a
page there, switches back to the first space and returns (each space shows the
page it last showed), opens the space editor and deletes the space (its page
closes, the first space remains). Screenshots of every step are artifacts.
"""
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import threading
import time
import uuid
from real_extensions_smoke import capture, windows_of
from smoke import ROOT


def main():
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            name = self.path.strip('/') or 'home'
            body = f"<!doctype html><html lang='en'><title>{name.title()} page</title><body style='font:16px Segoe UI;padding:24px'><h1>{name.title()}</h1>".encode()
            self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_port}/home'
    directory = ROOT / 'artifacts' / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:8])
    directory.mkdir(parents=True)
    report, profile = directory / 'native.jsonl', directory / 'profile'
    executable = Path(os.environ.get('RED_BROWSER_TEST_EXE', ROOT / 'build/Release/apps/desktop/Release/sol.exe'))
    command = [str(executable), f'--probe-report={report}', f'--profile-dir={profile}', f'--probe-url={url}', '--probe-spaces']
    shots = []
    try:
        with (directory / 'process.log').open('w', encoding='utf-8') as log:
            process = subprocess.Popen(command, cwd=executable.parent, stdout=log, stderr=log)
            seen, deadline = 0, time.time() + 60
            while process.poll() is None and time.time() < deadline:
                rows = [json.loads(l) for l in report.read_text().splitlines()] if report.exists() else []
                steps = [r['details']['step'] for r in rows if r['event'] == 'spaces_probe_step']
                for step in steps[seen:]:
                    for window in windows_of(process.pid):
                        if window['title'] in ('Sol', 'Sol — Panel') and window['visible']:
                            path = directory / f"{step}-{'panel' if 'Panel' in window['title'] else 'window'}.bmp"
                            if capture(window['hwnd'], path): shots.append(path.name)
                seen = len(steps)
                time.sleep(0.15)
            if process.poll() is None:
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
                raise RuntimeError(f'Spaces probe did not finish: {directory}')
        rows = [json.loads(l) for l in report.read_text().splitlines()]
    finally:
        server.shutdown(); server.server_close()
    steps = {r['details']['step']: r['details'] for r in rows if r['event'] == 'spaces_probe_step'}
    get = lambda step, key: steps.get(step, {}).get(key)
    created = [r['details']['code'] for r in rows if r['event'] == 'spaces_probe_created']
    deleted = [r['details']['code'] for r in rows if r['event'] == 'spaces_probe_deleted']
    database = profile / 'BrowserData' / 'workspace.sqlite3'
    saved = []
    if database.exists():
        with sqlite3.connect(f'file:{database.as_posix()}?mode=ro', uri=True) as db:
            saved = db.execute('SELECT name FROM spaces ORDER BY position').fetchall()
    home_space = get('home', 'active_space')
    checks = {
        'all_steps': list(steps) == ['home', 'empty_work', 'work_tab', 'back_home', 'work_again', 'editor', 'deleted', 'brief_empty', 'brief_deleted_open', 'brief_deleted'],
        'created': created == [0] and get('empty_work', 'spaces') == 2,
        'new_space_is_empty': get('empty_work', 'active_id') == '' and get('empty_work', 'active_space') != home_space,
        'new_space_has_own_color': get('empty_work', 'hue') is not None and get('empty_work', 'hue') != get('home', 'hue'),
        'tab_opens_in_new_space': (get('work_tab', 'url') or '').endswith('/work') and get('work_tab', 'active_space') == get('empty_work', 'active_space'),
        'switch_back_shows_its_page': get('back_home', 'active_space') == home_space and get('back_home', 'active_id') == get('home', 'active_id')
            and get('back_home', 'hue') == get('home', 'hue'),
        'return_shows_last_page': get('work_again', 'active_id') == get('work_tab', 'active_id'),
        'editor_screenshot': any(s.startswith('editor-panel') for s in shots),
        'delete_closes_its_pages': deleted == [0] and get('deleted', 'spaces') == 1 and get('deleted', 'active_space') == home_space
            and get('deleted', 'live') == 1 and get('deleted', 'nodes') == get('home', 'nodes'),
        'deleted_shows_page': get('deleted', 'page_visible') is True,
        # Sidebar collapsed: a new space deleted right away shows the previous page again.
        'collapsed_delete_shows_page': get('brief_empty', 'active_id') == '' and get('brief_deleted', 'spaces') == 1
            and get('brief_deleted', 'active_id') == get('deleted', 'active_id') and get('brief_deleted', 'page_visible') is True,
        'floating_sidebar_delete_shows_page': get('brief_deleted_open', 'spaces') == 1 and get('brief_deleted_open', 'page_visible') is True,
        'saved_spaces': [tuple(r) for r in saved] == [('Personal',)],
        'probe_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows),
    }
    result = {'passed': process.returncode == 0 and all(checks.values()),
              'exit_code': process.returncode, 'directory': str(directory),
              'checks': checks, 'shots': shots, 'steps': steps}
    (directory / 'spaces.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k != 'steps'}, indent=2), flush=True)
    print(directory)
    if not result['passed']: raise RuntimeError('Spaces smoke failed')


if __name__ == '__main__': main()
