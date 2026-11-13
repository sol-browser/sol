"""Little Sol (--probe-little-window); real CEF runs, disposable profiles.

Context menu: the workspace window loads /source and opens /little in Little Sol. From that page
the probe opens a foreground link (/blank) and a background link (/background), as target=_blank
links do, then sends the page to the Space it came from. Passes when Little Sol kept exactly its
one page, both links opened as tabs of the workspace window, the page moved live (one /little
document request), and Little Sol closed.

Other apps (--probe-little-window=external): while Sol runs, the script launches sol.exe again
with a link, as a chat app does when you click one. /external must open in Little Sol and not as a
tab. The probe then turns the setting off; the next link (/external-tab) must open as a tab.

Ctrl+Alt+N (--probe-little-window=launcher): the command bar opens on its own while its Little Sol
stays hidden; submitting /typed shows that window with the page. A second bar closed without
input must leave no window behind. (The real hotkey and tray are not pressed.)

Ctrl+Alt+click (--probe-little-window=click): the page is one big link; the probe clicks it with Ctrl
and Alt held, sent through CEF (the real keyboard is untouched). Blink makes this its "open in split
view" disposition; the link must open in Little Sol only, and the page must stay.

These runs open and activate windows: run them only when focus may be taken.
"""
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import threading
import time
import uuid
from smoke import ROOT, run

EXE = Path(os.environ.get('RED_BROWSER_TEST_EXE', ROOT / 'build/Release/apps/desktop/Release/sol.exe'))


def serve(requests):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            path = self.path.split('?')[0]
            requests.append(path)
            if path == '/favicon.ico':
                self.send_error(404)
                return
            body = f'<!doctype html><title>{path}</title><h1>{path}</h1><a href="/blank" target="_blank">blank</a>'.encode()
            if path == '/click-source':  # One link over the whole page, for the click scenario.
                body = b'<!doctype html><title>click</title><a href="/clicked" style="position:fixed;inset:0">link</a>'
            self.send_response(200)
            self.send_header('Content-Type', 'text/html'); self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def report(name, checks, events, requests):
    counts = {path: requests.count(path) for path in sorted(set(requests)) if path != '/favicon.ico'}
    print(json.dumps({'requests': counts, 'events': events}, indent=2))
    passed = all(checks.values())
    print(json.dumps({'scenario': name, 'passed': passed, 'checks': checks}, indent=2), flush=True)
    return passed


def context_menu():
    requests = []
    server = serve(requests)
    try:
        _, rows = run(f'http://localhost:{server.server_port}/source', ['--probe-little-window'], allow_failure=True, timeout=60)
    finally:
        server.shutdown(); server.server_close()
    events = {r['event']: r.get('details') or {} for r in rows if r['event'].startswith('little_')}
    count = requests.count
    loaded, links = events.get('little_loaded', {}), events.get('little_links', {})
    sent, closed = events.get('little_sent', {}), events.get('little_closed', {})
    return report('context menu', {
        'opened': events.get('little_opened', {}).get('created') is True,
        'one_page_on_open': loaded.get('pages') == 1 and loaded.get('sidebar_visible') is False and loaded.get('standalone') is True,
        'compact': 0 < loaded.get('width', 0) <= 1230,
        'kept_one_page': links.get('little_pages') == 1 and links.get('little_kept_page') is True,
        'links_in_workspace': links.get('foreground_in_workspace') is True and links.get('background_in_workspace') is True,
        'links_loaded': count('/blank') == 1 and count('/background') == 1,
        'background_notice': links.get('little_notice', '').startswith('Opened in '),
        'sent': sent.get('code') == 0,
        'moved_live': count('/little') == 1 and closed.get('page_in_workspace') is True,
        'little_closed': closed.get('little_closed') is True,
    }, events, requests)


def other_apps():
    requests = []
    server = serve(requests)
    origin = f'http://localhost:{server.server_port}'
    directory = ROOT / 'artifacts' / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-little-external-') + uuid.uuid4().hex[:6])
    directory.mkdir(parents=True)
    native, profile = directory / 'native.jsonl', directory / 'profile'
    rows, launched = [], set()

    def launch(path):
        # What Windows does for a link clicked in another app; CEF forwards it to the running Sol.
        subprocess.run([str(EXE), f'--profile-dir={profile}', origin + path], cwd=EXE.parent, timeout=30,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        launched.add(path)

    try:
        with (directory / 'process.log').open('w', encoding='utf-8') as log:
            process = subprocess.Popen([str(EXE), f'--probe-report={native}', f'--profile-dir={profile}',
                                        f'--probe-url={origin}/source', '--probe-little-window=external'],
                                       cwd=EXE.parent, stdout=log, stderr=log)
            deadline = time.monotonic() + 60
            while process.poll() is None and time.monotonic() < deadline:
                rows = [json.loads(line) for line in native.read_text(encoding='utf-8').splitlines()] if native.exists() else []
                seen = {row['event'] for row in rows}
                if 'little_external_ready' in seen and '/external' not in launched: launch('/external')
                if 'little_pref_off' in seen and '/external-tab' not in launched: launch('/external-tab')
                time.sleep(.2)
            if process.poll() is None:
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
            rows = [json.loads(line) for line in native.read_text(encoding='utf-8').splitlines()] if native.exists() else []
    finally:
        server.shutdown(); server.server_close()
    events = {r['event']: r.get('details') or {} for r in rows if r['event'].startswith('little_') or r['event'].startswith('external_')}
    loaded, tab = events.get('little_external_loaded', {}), events.get('little_external_tab', {})
    passed = report('other apps', {
        'forwarded_to_little_sol': 'external_launch_little_sol' in events,
        'opened_in_little_sol': loaded.get('pages') == 1 and loaded.get('little_windows') == 1 and loaded.get('in_workspace') is False,
        'loaded_once': requests.count('/external') == 1,
        'setting_saved': events.get('little_pref_off', {}).get('error') == '',
        'setting_off_opens_tab': tab.get('in_workspace') is True and tab.get('little_windows') == 1 and requests.count('/external-tab') == 1,
        'clean_exit': process.returncode == 0,
    }, events, requests)
    (directory / 'result.json').write_text(json.dumps({'passed': passed}) + '\n')
    return passed


def launcher():
    requests = []
    server = serve(requests)
    try:
        _, rows = run(f'http://localhost:{server.server_port}/source', ['--probe-little-window=launcher'], allow_failure=True, timeout=60)
    finally:
        server.shutdown(); server.server_close()
    events = {r['event']: r.get('details') or {} for r in rows if r['event'].startswith('little_')}
    bar, page = events.get('little_launcher_bar'), events.get('little_launcher_page', {})
    cancelled = events.get('little_launcher_cancelled', {})
    return report('Ctrl+Alt+N', {
        'created': events.get('little_launcher_created', {}).get('created') is True,
        'bar_alone': bar is not None and bar.get('little_visible') is False,
        'page_shows_window': page.get('visible') is True and page.get('pages') == 1 and page.get('still_launcher') is False,
        'typed_once': requests.count('/typed') == 1,
        'bar_replaced': events.get('little_launcher_bar_replaced', {}).get('detached') is False,
        'second_bar': 'little_launcher_bar_again' in events,
        'cancel_leaves_nothing': cancelled.get('launcher_gone') is True and cancelled.get('little_windows') == 1,
        'clean_exit': any(r['event'] == 'shutdown' for r in rows),
    }, events, requests)


def click():
    requests = []
    server = serve(requests)
    try:
        _, rows = run(f'http://localhost:{server.server_port}/click-source', ['--probe-little-window=click'], allow_failure=True, timeout=60)
    finally:
        server.shutdown(); server.server_close()
    events = {r['event']: r.get('details') or {} for r in rows if r['event'].startswith('little_')}
    opened = events.get('little_click_opened', {})
    return report('Ctrl+Alt+click', {
        'sent': 'little_click_sent' in events,
        'opened_in_little_sol': opened.get('pages') == 1 and opened.get('little_windows') == 1,
        'not_a_tab': opened.get('in_workspace') is False and opened.get('source_kept') is True,
        'loaded_once': requests.count('/clicked') == 1,
        'clean_exit': any(r['event'] == 'shutdown' for r in rows),
    }, events, requests)


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--scenario', choices=['all', 'menu', 'apps', 'launcher', 'click'], default='all')
    which = parser.parse_args().scenario
    scenarios = {'menu': context_menu, 'apps': other_apps, 'launcher': launcher, 'click': click}
    results = [run_one() for name, run_one in scenarios.items() if which in ('all', name)]
    if not all(results): raise SystemExit('Little Sol probe failed')


if __name__ == '__main__':
    main()
