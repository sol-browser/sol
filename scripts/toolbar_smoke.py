"""Experiment B: Chrome's own toolbar on a single Chrome BrowserView beside the
React shell, with view swapping as tab switching.

Owned loopback fixture and owned extension only (plus, with --real, the user's
fetched extensions for manual popup clicks). Evidence only: a measured
unsupported capability is a result, not a harness failure.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import threading
import time
import uuid
from real_extensions_smoke import capture, extensions, windows_of

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = b'<!doctype html><html lang="en"><meta charset="utf-8"><title>Toolbar fixture</title><h1>Owned toolbar fixture</h1><input value="initial">'


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--mode', choices=['normal', 'location', 'manual'], default='normal',
                        help='manual: normal toolbar, no timed swaps; waits until a person closes the window')
    parser.add_argument('--real', action='store_true', help='Also load the fetched real extensions')
    args = parser.parse_args()
    reports, requests = [], Counter()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass
        def do_GET(self):
            requests[self.path] += 1
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(FIXTURE)))
            self.end_headers()
            self.wfile.write(FIXTURE)
        def do_POST(self):
            length = int(self.headers.get('Content-Length', '0'))
            if self.path != '/toolbar-report' or not 0 < length <= 262144:
                self.send_error(400)
                return
            reports.append(json.loads(self.rfile.read(length)))
            self.send_response(204)
            self.end_headers()
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    directory = ROOT / 'artifacts' / (f'toolbar-{args.mode}-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:6])
    directory.mkdir(parents=True)
    probe = ROOT / '.cache' / ('toolbar-probe-' + uuid.uuid4().hex[:8])
    probe.mkdir(parents=True)
    for source in (ROOT / 'tests/extensions/toolbar-probe').iterdir():
        (probe / source.name).write_text(source.read_text().replace('__PROBE_PORT__', str(server.server_port)))
    loaded = [str(probe)] + ([e['unpacked'] for e in extensions()] if args.real else [])
    executable = Path(os.environ.get('RED_BROWSER_TEST_EXE', ROOT / 'build/Release/apps/desktop/Release/sol.exe'))
    url = f'http://127.0.0.1:{server.server_port}/fixture'
    command = [str(executable), f'--probe-report={directory / "native.jsonl"}', f'--profile-dir={directory / "profile"}',
               f'--probe-url={url}', f'--probe-chrome-toolbar={args.mode}', '--load-extension=' + ','.join(loaded)]
    windows, code = [], None
    try:
        with (directory / 'process.log').open('w', encoding='utf-8') as log:
            process = subprocess.Popen(command, cwd=executable.parent, stdout=log, stderr=log)
            start = time.monotonic()
            shots = {4, 5, 12, 13, 20}
            limit = 1800 if args.mode == 'manual' else 45
            while process.poll() is None and time.monotonic() - start < limit:
                second = int(time.monotonic() - start)
                if second in shots:
                    shots.discard(second)
                    visible = [w for w in windows_of(process.pid) if w['visible']]
                    windows.append({'second': second, 'windows': [{k: w[k] for k in ('title', 'class', 'rect')} for w in visible]})
                    for index, window in enumerate(visible):
                        capture(window['hwnd'], directory / f'window-{second:02d}-{index}.bmp')
                time.sleep(0.2)
            if process.poll() is None:
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)  # own tree only
            code = process.wait(timeout=30)
    finally:
        server.shutdown()
        server.server_close()
    rows = [json.loads(line) for line in (directory / 'native.jsonl').read_text().splitlines()] \
        if (directory / 'native.jsonl').exists() else []
    keep = ('chrome_toolbar_available', 'chrome_toolbar_layout', 'chrome_toolbar_view_browser_destroyed',
            'browser_created', 'content_closed', 'renderer_terminated', 'load_end', 'load_error', 'shutdown')
    result = {'mode': args.mode, 'exit_code': code, 'directory': str(directory),
              'native': [r for r in rows if r['event'] in keep], 'reports': reports,
              'document_requests': dict(requests), 'windows': windows}
    (directory / 'toolbar.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
