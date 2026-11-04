"""Observe the user's real extensions (uBO Lite, SponsorBlock, Dark Reader, 7TV)
in the workspace host. Requires scripts/fetch_real_extensions.py first.

Evidence only: each scenario uses a disposable profile, an owned observer
extension and native window/browser observations. Real sites need network.
Runs end with a forced process-tree stop, so this is not clean-shutdown evidence.
"""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import sqlite3
import struct
import subprocess
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
SCENARIOS = {
    'fixture': None,  # Owned loopback page: blocking + Dark Reader.
    'youtube': 'https://www.youtube.com/watch?v=S7xqdgji1PQ',  # Has SponsorBlock sponsor segments.
    'twitch': 'https://www.twitch.tv/popout/xqc/chat?popout=',  # Chat renders whether or not live.
}
FIXTURE = b'''<!doctype html><html lang="en"><meta charset="utf-8"><title>Red real-extension fixture</title>
<body style="background:#fff;color:#111;font:20px Segoe UI;padding:40px"><h1>Owned real-extension fixture</h1>
<p>Light page for Dark Reader; script requests for uBlock Origin Lite.</p>
<script>
const targets = {ad: 'https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js',
  tracker: 'https://www.googletagmanager.com/gtag/js?id=G-RED0TEST', control: 'https://example.com/'};
Promise.all(Object.entries(targets).map(([name, url]) => fetch(url, {mode: 'no-cors', cache: 'no-store'})
  .then(() => [name, 'loaded'], error => [name, 'failed:' + error.name])))
  .then(results => { document.documentElement.dataset.redBlock = JSON.stringify(Object.fromEntries(results)); });
</script>'''

user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
user32.GetWindowDC.restype = wintypes.HDC
gdi32.CreateCompatibleDC.restype = wintypes.HDC
gdi32.CreateCompatibleBitmap.restype = wintypes.HBITMAP
gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
gdi32.DeleteDC.argtypes = [wintypes.HDC]
gdi32.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT,
                            ctypes.c_void_p, ctypes.c_char_p, wintypes.UINT]
user32.GetWindowDC.argtypes = [wintypes.HWND]
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]


def windows_of(pid: int) -> list[dict]:
    """Top-level windows of our own browser process only."""
    found = []
    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            title = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(hwnd, title, 256)
            name = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, name, 256)
            rect = wintypes.RECT()
            user32.GetWindowRect(hwnd, ctypes.byref(rect))
            visible = bool(user32.IsWindowVisible(hwnd))
            if visible or title.value:
                found.append({'hwnd': hwnd, 'title': title.value, 'class': name.value, 'visible': visible,
                              'rect': [rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top]})
        return True
    user32.EnumWindows(visit, 0)
    return found


def capture(hwnd: int, path: Path) -> bool:
    """PrintWindow captures only this window, even when other apps overlap it."""
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    width, height = rect.right - rect.left, rect.bottom - rect.top
    if width <= 0 or height <= 0:
        return False
    window_dc = user32.GetWindowDC(hwnd)
    memory_dc = gdi32.CreateCompatibleDC(window_dc)
    bitmap = gdi32.CreateCompatibleBitmap(window_dc, width, height)
    gdi32.SelectObject(memory_dc, bitmap)
    ok = bool(user32.PrintWindow(hwnd, memory_dc, 2))  # PW_RENDERFULLCONTENT
    header = struct.pack('<IiiHHIIiiII', 40, width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = ctypes.create_string_buffer(width * height * 4)
    gdi32.GetDIBits(memory_dc, bitmap, 0, height, pixels, ctypes.c_char_p(header), 0)
    gdi32.DeleteObject(bitmap); gdi32.DeleteDC(memory_dc); user32.ReleaseDC(hwnd, window_dc)
    if ok:
        path.write_bytes(b'BM' + struct.pack('<IHHI', 54 + len(pixels), 0, 0, 54) + header + pixels.raw)
    return ok


def tree(database: Path) -> list[dict]:
    """Saved workspace nodes, read-only, after the run (adopted pages appear here)."""
    if not database.exists():
        return []
    with sqlite3.connect(f'file:{database.as_posix()}?mode=ro', uri=True) as db:
        db.row_factory = sqlite3.Row
        return [dict(row) for row in db.execute('SELECT * FROM nodes ORDER BY position')]


def extensions() -> list[dict]:
    fetched = json.loads((ROOT / '.cache/real-extensions/fetched.json').read_text())
    return [entry for entry in fetched if Path(entry['unpacked'], 'manifest.json').is_file()]


def scenario(name: str, url: str, port: int, reports: list, executable: Path, seconds: int) -> dict:
    directory = ROOT / 'artifacts' / (f'real-ext-{name}-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:6])
    directory.mkdir(parents=True)
    observer = ROOT / '.cache' / ('real-observer-' + uuid.uuid4().hex[:8])
    observer.mkdir(parents=True)
    for source in (ROOT / 'tests/extensions/real-observer').iterdir():
        (observer / source.name).write_text(source.read_text().replace('__PROBE_PORT__', str(port)))
    loaded = [entry['unpacked'] for entry in extensions()] + [str(observer)]
    command = [str(executable), '--workspace', f'--probe-report={directory / "native.jsonl"}', '--probe-input',
               f'--profile-dir={directory / "profile"}', f'--probe-url={url}', '--load-extension=' + ','.join(loaded)]
    reports.clear()
    print(f'Running {name}: {url}', flush=True)
    snapshots = []
    with (directory / 'process.log').open('w', encoding='utf-8') as log:
        process = subprocess.Popen(command, cwd=executable.parent, stdout=log, stderr=log)
        try:
            for second in range(seconds):
                if process.poll() is not None:
                    break
                time.sleep(1)
                if second in (8, 20, seconds - 3):
                    snapshots.append({'second': second, 'windows': windows_of(process.pid)})
                if second == 30:
                    for index, window in enumerate(w for w in windows_of(process.pid) if w['visible']):
                        capture(window['hwnd'], directory / f'window-{index}.bmp')
        finally:
            early_exit = process.poll()
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
            process.wait(timeout=30)
    rows = [json.loads(line) for line in (directory / 'native.jsonl').read_text().splitlines()] \
        if (directory / 'native.jsonl').exists() else []
    created = [r['details'] for r in rows if r['event'] == 'browser_created']
    native = [r['details'] for r in rows if r['event'] == 'native_input_observation']
    root = native[-1]['root']['handle'] if native else None
    browsers = [{k: b.get(k) for k in ('browser_id', 'class', 'visible', 'parent', 'width', 'height')}
                | {'child_of_workspace': b.get('parent') == root} for b in (native[-1]['browsers'] if native else [])]
    result = {'scenario': name, 'url': url, 'directory': str(directory), 'early_exit_code': early_exit,
              'loaded': [{k: e[k] for k in ('name', 'id', 'version', 'sha256')} for e in extensions()],
              'native_created': created, 'native_browsers_at_end': browsers, 'top_level_windows': snapshots,
              'renderer_terminations': [r['details'] for r in rows if r['event'] == 'renderer_terminated'],
              'foreign_browsers': [r for r in rows if r['event'].startswith('foreign_')],
              'shell_profile_checks': [r['details'] for r in rows if r['event'] == 'shell_profile_extensions'],
              'workspace_tree': tree(directory / 'profile/BrowserData/workspace.sqlite3'),
              'reports': list(reports)}
    result['checks'] = checks(result)
    (directory / 'real-extensions.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def checks(result: dict) -> dict:
    """Host-level acceptance for browsers Chrome created on extensions' behalf."""
    created = {r['details']['browser_id'] for r in result['foreign_browsers'] if r['event'] == 'foreign_browser_created'}
    resolved = {r['details']['browser_id']: r['details'] for r in result['foreign_browsers'] if r['event'] == 'foreign_browser_resolved'}
    tree_urls = [node['url'] for node in result['workspace_tree']]
    adopted = [d['url'] for d in resolved.values() if d['outcome'] == 'adopted']
    last = result['top_level_windows'][-1]['windows'] if result['top_level_windows'] else []
    stray = [w['title'] for w in last if w['class'] == 'Chrome_WidgetWin_1' and w['title'] != 'Sol']
    denied = [c for c in result['shell_profile_checks'] if c.get('denied')]
    return {'foreign_all_resolved': created == set(resolved),
            # Extension pages may change their fragment after load (7TV onboarding).
            'adopted_in_tree': all(any(u.split('#')[0] == url.split('#')[0] for u in tree_urls) for url in adopted),
            'no_stray_chrome_windows': not stray, 'stray_windows': stray,
            'shell_profile_extensions_detected': bool(denied),
            'renderer_crashes': len(result['renderer_terminations']), 'early_exit': result['early_exit_code']}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--only', choices=sorted(SCENARIOS), action='append')
    parser.add_argument('--seconds', type=int, default=45)
    args = parser.parse_args()
    reports = []
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass
        def do_GET(self):
            body = FIXTURE if self.path == '/fixture' else b'<!doctype html><title>Created by extension</title><h1>Created by extension</h1>'
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        def do_POST(self):
            length = int(self.headers.get('Content-Length', '0'))
            if self.path != '/report' or not 0 < length <= 262144:
                self.send_error(400)
                return
            reports.append(json.loads(self.rfile.read(length)))
            self.send_response(204)
            self.end_headers()
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    executable = Path(os.environ.get('RED_BROWSER_TEST_EXE', ROOT / 'build/Release/apps/desktop/Release/sol.exe'))
    try:
        for name in args.only or SCENARIOS:
            url = SCENARIOS[name] or f'http://127.0.0.1:{server.server_port}/fixture'
            result = scenario(name, url, server.server_port, reports, executable, args.seconds)
            print(json.dumps(result, indent=2), flush=True)
    finally:
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    main()
