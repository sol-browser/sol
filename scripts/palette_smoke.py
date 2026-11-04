"""Exercise the integrated editor in its real CEF panel, with a disposable profile.

Uses the application's gated probe, never a public debug port. Native/UI inputs
are verified through the resulting native snapshot, SQLite and a second launch.
"""
import argparse
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


def records(path):
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def run(executable, directory, profile, url, backdrop):
    directory.mkdir(parents=True)
    report = directory / 'native.jsonl'
    command = [str(executable), f'--probe-report={report}', f'--profile-dir={profile}',
               f'--probe-url={url}', '--probe-palette', f'--probe-backdrop={backdrop}']
    seen = set()
    with (directory / 'process.log').open('w', encoding='utf-8') as log:
        process = subprocess.Popen(command, cwd=executable.parent, stdout=log, stderr=log)
        deadline = time.monotonic() + 100
        while process.poll() is None and time.monotonic() < deadline:
            for row in records(report):
                event = row['event']
                if event not in ('palette_editor_probe', 'palette_editor_reopen') or event in seen:
                    continue
                seen.add(event)
                for window in windows_of(process.pid):
                    if window['title'] in ('Sol', 'Sol — Palette test', 'Sol — Panel') and window['visible']:
                        target = directory / (event + ('-panel.bmp' if 'Panel' in window['title'] else '-window.bmp'))
                        capture(window['hwnd'], target)
            time.sleep(.1)
        if process.poll() is None:
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
            raise RuntimeError(f'Palette probe timed out: {directory}')
    rows = records(report)
    details = {r['event']: r['details'] for r in rows if r['event'].startswith('palette_')}
    database = profile / 'BrowserData' / 'workspace.sqlite3'
    if not report.exists() or not database.exists():
        raise RuntimeError(f'Palette browser exited {process.returncode} before creating evidence/database: {directory}')
    with sqlite3.connect(f'file:{database.as_posix()}?mode=ro', uri=True) as db:
        saved = db.execute('SELECT theme_hue,theme_mode,theme_noise,theme_palette FROM spaces WHERE id=(SELECT active_space FROM state)').fetchone()
        schema = db.execute('PRAGMA user_version').fetchone()[0]
    expected = details.get('palette_editor_probe', {}).get('theme', {})
    palette = expected.get('palette', {})
    encoded = ','.join(str(palette.get(k)) for k in ('version', 'count', 'radius', 'lightness', 'harmony', 'kind', 'fill'))
    if palette.get('version') == 2:
        encoded += ''.join(',' + ','.join(str(c.get(k)) for k in ('hue','distance','size')) for c in palette.get('circles', []))
        if palette.get('rotation', 0):
            encoded += ',' + str(palette['rotation'])
    checks = {
        'ui_interactions': details.get('palette_editor_probe', {}).get('passed') is True,
        'reopened_controls': details.get('palette_editor_reopen', {}).get('passed') is True,
        'native_acceptance': any(r['event'] == 'probe_result' and r['details'].get('pass') is True for r in rows),
        'saved_exact_palette': bool(saved and saved == (expected.get('hue'), 1, expected.get('noise'), encoded)),
        'schema9': schema == 9,
        'color_count3': palette.get('count') == 3,
    }
    functional = all(checks.values())
    result = {'passed': functional and process.returncode == 0, 'functionalPassed': functional,
              'exitCode': process.returncode, 'checks': checks, 'backdrop': backdrop,
              'theme': expected, 'startup': details.get('palette_startup'), 'directory': str(directory)}
    (directory / 'palette.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--backdrop', choices=('none', 'mica', 'acrylic'), default='none')
    parser.add_argument('--restart', action='store_true')
    args = parser.parse_args()
    executable = Path(os.environ.get('RED_BROWSER_TEST_EXE', ROOT / 'build/devtheme/apps/desktop/Release/sol.exe'))
    directory = ROOT / 'artifacts' / ('palette-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:8])
    profile = directory / 'profile'
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass
        def do_GET(self):
            body = b'<!doctype html><title>Space theme test</title><body style="font:16px Segoe UI;padding:24px"><h1>Space theme test</h1><p>The page keeps its own colors. Edit the space around it.</p>'
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        url = f'http://127.0.0.1:{server.server_port}/theme'
        first = run(executable, directory / 'first', profile, url, args.backdrop)
        print(json.dumps(first, indent=2), flush=True)
        if not first['functionalPassed']:
            raise RuntimeError('Integrated palette checks failed')
        if args.restart:
            second = run(executable, directory / 'restart', profile, url, args.backdrop)
            startup = second.get('startup', {}).get('theme')
            restored = startup == first['theme']
            second['restoredExactTheme'] = restored
            (directory / 'restart' / 'palette.json').write_text(json.dumps(second, indent=2) + '\n')
            print(json.dumps(second, indent=2), flush=True)
            if not second['functionalPassed'] or not restored:
                raise RuntimeError('Palette restart verification failed')
    finally:
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    main()
