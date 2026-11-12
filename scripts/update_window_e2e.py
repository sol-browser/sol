"""End to end: "Check for Updates" against a local feed, in a real Velopack layout of the build.

Packs the build output twice as a throw-away package (SolE2E, versions 9.0.0 and 9.0.1), unpacks the
9.0.0 portable zip into an own folder and runs that Sol with its own profile and SOL_UPDATE_FEED pointing at
a local server (the 9.0.1 feed, throttled so the download takes a few seconds). The browser's menu probe
(--probe-menu=update) opens the update window through its menu item and records every status the window
would show; this script also photographs the window (PrintWindow, our own process only). Cases, in order,
on one install:

  failed       nothing answers at the feed address
  upToDate     the feed holds only the installed version
  downloading  the feed holds 9.0.1: the download must move (several steps), then "ready"
  restart      "Restart to update": Velopack applies 9.0.1 and Sol starts again as 9.0.1

  python scripts/update_window_e2e.py [--build build/sol041/apps/desktop/Release] [--rate 12] [--skip-restart]

Nothing outside artifacts/update-e2e-* is touched: not the real Sol, its profile, shortcuts or registration.
Processes are only ever stopped by their path inside the install folder.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import time
import uuid
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from real_extensions_smoke import capture, windows_of  # noqa: E402
from PIL import Image  # noqa: E402

VPK = ROOT / '.cache' / 'vpk' / 'vpk.exe'
OLD, NEW = '9.0.0', '9.0.1'
PHASES = ['idle', 'checking', 'downloading', 'ready', 'upToDate', 'failed', 'notInstalled']


def run(command):
    result = subprocess.run([str(c) for c in command], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f'{command[0]} failed ({result.returncode}):\n{result.stdout[-1500:]}\n{result.stderr[-1500:]}')
    return result


def stage(build, target):
    # What build_installer.ps1 stages: the runtime without test programs, symbols and logs.
    subprocess.run(['robocopy', str(build), str(target), '/MIR', '/XF', '*.pdb', '*.ilk', '*.lib', '*.exp', '*.log', '*_test.exe', 'sol.*.dll',
                    '/NFL', '/NDL', '/NJH', '/NJS', '/NP'], capture_output=True)  # robocopy exits below 8 on success


def pack(stage_dir, version, output):
    run([VPK, 'pack', '--packId', 'SolE2E', '--packVersion', version, '--packDir', stage_dir, '--mainExe', 'sol.exe', '--packTitle', 'Sol E2E',
         '--shortcuts', 'None', '--aumid', 'Sol.E2E', '--channel', 'win', '--runtime', 'win-x64', '--outputDir', output])


def feed_server(directory, rate_mb):
    served = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            path = directory / self.path.lstrip('/').split('?')[0]
            if not path.is_file():
                self.send_response(404); self.end_headers(); return
            size = path.stat().st_size
            self.send_response(200); self.send_header('Content-Length', str(size))
            self.send_header('Content-Type', 'application/octet-stream'); self.end_headers()
            served.append(path.name)
            chunk = 256 * 1024
            delay = chunk / (rate_mb * 1024 * 1024) if path.suffix == '.nupkg' else 0
            try:
                with path.open('rb') as file:
                    while data := file.read(chunk):
                        self.wfile.write(data)
                        if delay: time.sleep(delay)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, served


PAGE = b'<!doctype html><title>update e2e</title><h1>update e2e</h1>'


def page_server():
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Content-Length', str(len(PAGE))); self.end_headers()
            self.wfile.write(PAGE)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def events(report):
    if not report.exists():
        return []
    return [json.loads(line) for line in report.read_text(encoding='utf-8', errors='replace').splitlines() if line.strip()]


def shoot(pid, folder, name, stop):
    """Photographs the update window now and then while a case runs."""
    taken = 0
    while not stop.is_set() and taken < 8:
        time.sleep(1.2)
        for window in windows_of(pid):
            if window['visible'] and window['title'].startswith('Check for Updates'):
                bmp = folder / f'{name}-{taken}.bmp'
                if capture(window['hwnd'], bmp):
                    Image.open(bmp).convert('RGB').save(folder / f'{name}-{taken}.png'); bmp.unlink(); taken += 1


def probe(exe, work, name, feed, page, mode='update', timeout=160):
    """One run of the installed Sol; returns the statuses the update window went through."""
    report = work / f'{name}.jsonl'
    env = dict(os.environ, SOL_UPDATE_FEED=feed)
    stop = threading.Event()
    with (work / f'{name}.log').open('w', encoding='utf-8') as log:
        process = subprocess.Popen([str(exe), f'--probe-report={report}', f'--profile-dir={work / "profile"}', f'--probe-url={page}',
                                    f'--probe-menu={mode}'], cwd=exe.parent, stdout=log, stderr=log, env=env)
        shots = threading.Thread(target=shoot, args=(process.pid, work, name, stop), daemon=True); shots.start()
        try: process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            stop_path(exe.parent.parent)
        stop.set()
    rows = events(report)
    seen = [(PHASES[r['details']['phase']], r['details']['percent'], r['details']['version']) for r in rows if r['event'] == 'update_probe_status']
    result = next((r['details'] for r in rows if r['event'] == 'menu_probe_result'), {})
    return seen, result


def stop_path(folder):
    """Stops processes whose executable is inside |folder| (never anything else)."""
    script = ("Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith('" + str(folder).replace("'", "''") +
              "', [StringComparison]::OrdinalIgnoreCase) } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }")
    subprocess.run(['powershell', '-NoProfile', '-Command', script], capture_output=True)


def installed_version(install):
    try:
        text = (install / 'current' / 'sq.version').read_text(encoding='utf-8', errors='replace')
    except OSError:
        return ''
    found = re.search(r'<version>([^<]+)</version>', text)
    return found.group(1) if found else ''


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/sol041/apps/desktop/Release')
    parser.add_argument('--rate', type=float, default=12.0, help='feed download speed, MB/s')
    parser.add_argument('--skip-restart', action='store_true')
    args = parser.parse_args()
    work = ROOT / 'artifacts' / ('update-e2e-' + uuid.uuid4().hex[:8]); work.mkdir()
    report = {'work': str(work), 'cases': {}}
    servers = [page_server()]
    page = f'http://127.0.0.1:{servers[0].server_port}/'
    install = work / 'install'
    try:
        staged = work / 'stage'; stage(args.build, staged)
        pack(staged, OLD, work / 'feed-old'); pack(staged, NEW, work / 'feed-new')
        shutil.rmtree(staged)
        # The sandbox needs its LPAC SID on the folder (Sol's setup and build grant it): before unpacking, so files inherit it.
        install.mkdir()
        run(['icacls', install, '/grant', '*S-1-15-2-2:(OI)(CI)(RX)', '/Q'])
        with zipfile.ZipFile(work / 'feed-old' / 'SolE2E-win-Portable.zip') as archive: archive.extractall(install)
        exe = install / 'current' / 'sol.exe'
        report['installed'] = installed_version(install)

        report['cases']['failed'] = probe_case = {}
        seen, result = probe(exe, work, 'failed', 'http://127.0.0.1:9/', page)
        probe_case.update(statuses=seen, window=result.get('window'), passed=bool(seen) and seen[-1][0] == 'failed' and result.get('window') is True)

        server, _ = feed_server(work / 'feed-old', args.rate); servers.append(server)
        seen, result = probe(exe, work, 'upToDate', f'http://127.0.0.1:{server.server_port}/', page)
        report['cases']['upToDate'] = dict(statuses=seen, window=result.get('window'), passed=bool(seen) and seen[-1][0] == 'upToDate')

        server, served = feed_server(work / 'feed-new', args.rate); servers.append(server)
        seen, result = probe(exe, work, 'downloading', f'http://127.0.0.1:{server.server_port}/', page)
        percents = [p for phase, p, _ in seen if phase == 'downloading']
        report['cases']['downloading'] = dict(statuses=seen, served=sorted(set(served)),
            passed=bool(seen) and seen[-1][0] == 'ready' and seen[-1][2] == NEW and len(set(percents)) >= 3 and percents == sorted(percents))

        if not args.skip_restart:
            # The downloaded 9.0.1 waits: Ready at once, then the window's "Restart to update".
            seen, result = probe(exe, work, 'restart', f'http://127.0.0.1:{server.server_port}/', page, mode='update-restart', timeout=90)
            deadline = time.time() + 90
            while time.time() < deadline and installed_version(install) != NEW: time.sleep(1)
            time.sleep(8)  # The updated Sol starts by itself; let it come up before it is stopped.
            report['cases']['restart'] = dict(statuses=seen, version_after=installed_version(install),
                                              passed=installed_version(install) == NEW)
    finally:
        for server in servers: server.shutdown()
        stop_path(install)
    report['passed'] = all(case['passed'] for case in report['cases'].values()) and len(report['cases']) == (3 if args.skip_restart else 4)
    (work / 'result.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2, ensure_ascii=False))
    sys.exit(0 if report['passed'] else 1)


if __name__ == '__main__':
    main()
