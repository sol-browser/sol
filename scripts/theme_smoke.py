"""Shell theme (hue, intensity, light/dark, grain) through actual CEF.

The probe applies themes through the controller and opens the theme popover;
this script screenshots the browser window at every step (artifact BMPs for
review), checks the snapshot theme and the native window background, and that
the theme persists across a restart.
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
from real_extensions_smoke import capture, windows_of
from smoke import ROOT
import sqlite3


def saved_theme(profile: Path):
    database = profile / 'BrowserData' / 'workspace.sqlite3'
    if not database.exists(): return None
    with sqlite3.connect(f'file:{database.as_posix()}?mode=ro', uri=True) as db:
        # Each space keeps its own theme (schema v6): the space on screen.
        return db.execute('SELECT theme_hue, theme_intensity, theme_mode, theme_noise FROM spaces WHERE id=(SELECT active_space FROM state)').fetchone()


def main():
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            body = b"<!doctype html><html lang='en'><title>Theme fixture</title><body style='font:16px Segoe UI;padding:24px'><h1>Theme fixture</h1><p>Page content stays the site's own.</p>"
            self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_port}/theme'
    directory = ROOT / 'artifacts' / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:8])
    directory.mkdir(parents=True)
    report, profile = directory / 'native.jsonl', directory / 'profile'
    executable = Path(os.environ.get('RED_BROWSER_TEST_EXE', ROOT / 'build/Release/apps/desktop/Release/sol.exe'))
    command = [str(executable), f'--probe-report={report}', f'--profile-dir={profile}', f'--probe-url={url}', '--probe-theme']
    shots = []
    try:
        with (directory / 'process.log').open('w', encoding='utf-8') as log:
            process = subprocess.Popen(command, cwd=executable.parent, stdout=log, stderr=log)
            seen, deadline = 0, time.time() + 60
            while process.poll() is None and time.time() < deadline:
                rows = [json.loads(l) for l in report.read_text().splitlines()] if report.exists() else []
                steps = [r['details']['step'] for r in rows if r['event'] == 'theme_probe_step']
                for step in steps[seen:]:
                    for window in windows_of(process.pid):
                        if window['title'] in ('Sol', 'Sol — Panel') and window['visible']:
                            path = directory / f"{step}-{'panel' if 'Panel' in window['title'] else 'window'}.bmp"
                            if capture(window['hwnd'], path): shots.append(path.name)
                seen = len(steps)
                time.sleep(0.15)
            if process.poll() is None:
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
                raise RuntimeError(f'Theme probe did not finish: {directory}')
        rows = [json.loads(l) for l in report.read_text().splitlines()]
    finally:
        server.shutdown(); server.server_close()
    steps = {r['details']['step']: r['details'] for r in rows if r['event'] == 'theme_probe_step'}
    def theme_of(name): return steps.get(name, {}).get('snapshot_theme', {})
    checks = {
        'all_steps': list(steps) == ['default', 'green_light', 'green_dark', 'coral_dark_grain', 'graphite_light_grain', 'popover'],
        'saved_ok': all(steps[s].get('code') == 0 for s in steps if s != 'popover'),
        'default_background_unchanged': (steps.get('default', {}).get('bg_r'), steps.get('default', {}).get('bg_g'), steps.get('default', {}).get('bg_b')) == (231, 222, 237),
        'snapshot_reports_theme': theme_of('green_light') == {'hue': 145, 'intensity': 120, 'mode': 'light', 'noise': 0, 'dark': False},
        'dark_resolved': steps.get('green_dark', {}).get('dark') is True and theme_of('green_dark').get('dark') is True,
        'dark_background_is_dark': steps.get('green_dark', {}).get('bg_r', 255) < 70,
        'green_tint': steps.get('green_light', {}).get('bg_g', 0) > steps.get('green_light', {}).get('bg_r', 0),
        'screenshots': len(shots) >= 6 and any(s.startswith('popover-panel') for s in shots),
        'probe_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows),
        # The last step was saved: graphite (intensity 0), light, grain 40.
        'last_theme_saved': saved_theme(profile) == (307, 0, 0, 40),
    }
    result = {'passed': all(checks.values()), 'checks': checks, 'shots': shots, 'steps': steps}
    (directory / 'theme.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'steps'}, indent=2), flush=True)
    print(directory)
    if not result['passed']: raise RuntimeError('Theme smoke failed')


if __name__ == '__main__': main()
