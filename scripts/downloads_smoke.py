"""Real downloads through the workspace's native download manager.

Owned loopback server and a per-run downloads directory (probe override);
the user's own Downloads folder is never touched. The in-UI probe drives the
downloads; this script then checks the files on disk.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse
import json
import os
import subprocess
from pathlib import Path
import threading
import time
from smoke import ROOT, run

REPORT = bytes(range(256)) * 256  # 65536 bytes, recognisable content.
FIXTURE = '''<!doctype html><html lang="en"><meta charset="utf-8"><title>Downloads fixture</title>
<body style="font:16px Segoe UI;padding:32px;line-height:2.2"><h1>Downloads fixture</h1>
<a href="/download/report.pdf">report.pdf (64 KB)</a><br><a href="/download/slow.bin">slow.bin (4 MB, slow)</a><br>
<a href="/download/stream.log">stream.log (unknown size)</a><br><a href="/download/evil">hostile file name</a></body>'''.encode()
SLOW_SIZE, SLOW_CHUNK = 4 * 1024 * 1024, 16 * 1024
RESERVED = {'CON', 'PRN', 'AUX', 'NUL', *(f'COM{i}' for i in range(1, 10)), *(f'LPT{i}' for i in range(1, 10))}


def manual(port: int, target: Path) -> None:
    """A person clicks the fixture's links; downloads go to a disposable folder."""
    directory = ROOT / 'artifacts' / f'downloads-manual-{int(time.time())}'
    directory.mkdir(parents=True)
    exe = Path(os.environ.get('RED_BROWSER_TEST_EXE', ROOT / 'build/Release/apps/desktop/Release/sol.exe'))
    command = [str(exe), '--workspace', f'--profile-dir={directory / "profile"}', f'--probe-report={directory / "native.jsonl"}',
               '--probe-input', f'--probe-url=http://127.0.0.1:{port}/fixture', f'--probe-downloads-dir={target}']
    with (directory / 'process.log').open('w') as log:
        subprocess.Popen(command, cwd=exe.parent, stdout=log, stderr=log).wait()
    print(json.dumps({'directory': str(directory), 'downloads_dir': str(target), 'files': sorted(p.name for p in target.iterdir())}, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--manual', action='store_true')
    args = parser.parse_args()
    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'
        def log_message(self, *_args):
            pass
        def send(self, body: bytes, extra: dict[str, str]):
            self.send_response(200)
            for key, value in {'Cache-Control': 'no-store', 'Content-Length': str(len(body)), **extra}.items():
                self.send_header(key, value)
            self.end_headers()
            self.wfile.write(body)
        def do_GET(self):
            if self.path == '/download/report.pdf':
                self.send(REPORT, {'Content-Type': 'application/pdf', 'Content-Disposition': 'attachment; filename="report.pdf"'})
            elif self.path == '/download/evil':
                self.send(b'device name', {'Content-Type': 'text/plain', 'Content-Disposition': 'attachment; filename="..\\\\..\\\\CON.txt"'})
            elif self.path == '/download/slow.bin':
                self.send_response(200)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Disposition', 'attachment; filename="slow.bin"')
                self.send_header('Content-Length', str(SLOW_SIZE))
                self.end_headers()
                try:
                    for _ in range(SLOW_SIZE // SLOW_CHUNK):
                        self.wfile.write(b'\0' * SLOW_CHUNK)
                        time.sleep(0.1)  # ~160 KB/s: long enough to pause, resume and cancel.
                except (ConnectionError, OSError):
                    pass  # The browser canceled.
            elif self.path == '/download/stream.log':
                # No Content-Length: the UI must show an indeterminate progress bar.
                self.send_response(200)
                self.send_header('Content-Type', 'text/plain')
                self.send_header('Content-Disposition', 'attachment; filename="stream.log"')
                self.send_header('Connection', 'close')
                self.end_headers()
                try:
                    for i in range(120):
                        self.wfile.write(f'line {i}\n'.encode() * 400)
                        self.wfile.flush()
                        time.sleep(0.1)
                except (ConnectionError, OSError):
                    pass
                self.close_connection = True
            else:
                self.send(FIXTURE, {'Content-Type': 'text/html; charset=utf-8'})
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    target = ROOT / 'artifacts' / f'downloads-{int(time.time())}'
    target.mkdir(parents=True)
    if args.manual:
        try:
            manual(server.server_port, target)
        finally:
            server.shutdown(); server.server_close()
        return
    try:
        directory, rows = run(f'http://127.0.0.1:{server.server_port}/fixture',
                              ['--probe-downloads', f'--probe-downloads-dir={target}'], allow_failure=True)
    finally:
        server.shutdown()
        server.server_close()
    files = sorted(p.name for p in target.iterdir())
    def zone(name: str) -> str:
        try:
            return (target / f'{name}:Zone.Identifier').read_text(errors='replace')
        except OSError:
            return ''
    checks = {
        'probe_passed': any(r['event'] == 'downloads_checks_pass' for r in rows),
        'chrome_bubble_disabled': any(r['event'] == 'chrome_download_bubble' and r['details'].get('disabled') for r in rows),
        # The real popover window rendered the three completed downloads.
        'popover_rendered': any(r['event'] == 'panel_rendered' and r['details'].get('kind') == 'downloads'
                                and r['details'].get('rows', 0) >= 3 for r in rows),
        'report_content': (target / 'report.pdf').read_bytes() == REPORT if (target / 'report.pdf').exists() else False,
        'no_overwrite': (target / 'report (1).pdf').exists(),
        # Chromium pre-cleans some names before our sanitizer (unit-tested separately).
        'device_name_neutralised': len(files) == 3 and not any(n.split('.')[0].upper() in RESERVED for n in files),
        'nothing_outside_directory': not (target.parent / 'CON.txt').exists(),
        'canceled_file_removed': not any(n.startswith('slow.bin') or n.endswith('.crdownload') for n in files),
        # Windows Mark-of-the-Web lets SmartScreen check files the user opens.
        'mark_of_the_web': 'ZoneId=' in zone('report.pdf'),
    }
    summary = {'passed': all(checks.values()), 'checks': checks, 'files': files, 'zone_identifier': zone('report.pdf'),
               'run': directory.name, 'downloads_dir': str(target),
               'states': [r['details'] for r in rows if r['event'] in ('download_started', 'download_state')]}
    (directory / 'downloads.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))
    if not summary['passed']:
        raise SystemExit('Downloads acceptance failed')


if __name__ == '__main__':
    main()
