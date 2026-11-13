"""Pages get Sol's shortcut keys first, as in Chrome (real CEF, real key presses, disposable profiles).

A user's report (2026-11-13): Ctrl+D (Pin tab) broke duplicating in Figma, and after Pin was moved
to Ctrl+K, Sol still took Ctrl+D. Two scenarios, each with two tabs: a page that handles Ctrl+D and
Ctrl+K itself (preventDefault) and a plain page. Keys are pressed with SendInput into the test
window only (checked before every key) after one click into the page.

1. Default keys: on the handling page Ctrl+D reaches the page and pins nothing; on the plain page
   it reaches the page and Sol pins the tab.
2. Pin moved to Ctrl+K (settings written before start): on the plain page Ctrl+D reaches the page
   and pins nothing (Chrome's own Ctrl+D bookmark does not run either); Ctrl+K pins the tab.
Reserved keys stay Sol's: Ctrl+T opens a new tab even on the handling page.

Takes the keyboard and mouse for a few seconds per scenario.
"""
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get('RED_BROWSER_TEST_EXE', ROOT / 'build/Release/apps/desktop/Release/sol.exe'))
user32 = ctypes.windll.user32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
VK_CONTROL = 0x11


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [('wVk', wintypes.WORD), ('wScan', wintypes.WORD), ('dwFlags', wintypes.DWORD), ('time', wintypes.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [('dx', wintypes.LONG), ('dy', wintypes.LONG), ('mouseData', wintypes.DWORD), ('dwFlags', wintypes.DWORD), ('time', wintypes.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [('ki', KEYBDINPUT), ('mi', MOUSEINPUT)]
    _anonymous_ = ('u',)
    _fields_ = [('type', wintypes.DWORD), ('u', _U)]


def keys(*events):
    """events: (vk, up) pairs, sent as one batch."""
    batch = (INPUT * len(events))()
    for i, (vk, up) in enumerate(events):
        batch[i].type = 1; batch[i].ki.wVk = vk; batch[i].ki.dwFlags = 2 if up else 0
    user32.SendInput(len(events), batch, ctypes.sizeof(INPUT))


def chord(vk):
    keys((VK_CONTROL, False), (vk, False), (vk, True), (VK_CONTROL, True))


def click(x, y):
    old = wintypes.POINT(); user32.GetCursorPos(ctypes.byref(old))
    user32.SetCursorPos(x, y)
    batch = (INPUT * 2)()
    batch[0].type = batch[1].type = 0
    batch[0].mi.dwFlags, batch[1].mi.dwFlags = 2, 4  # MOUSEEVENTF_LEFTDOWN, LEFTUP
    user32.SendInput(2, batch, ctypes.sizeof(INPUT))
    time.sleep(.1)
    user32.SetCursorPos(old.x, old.y)


def browser_pid(profile):
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
        "Get-CimInstance Win32_Process -Filter \"Name='sol.exe'\" | Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress"],
        capture_output=True, text=True).stdout
    rows = json.loads(out) if out.strip() else []
    rows = rows if isinstance(rows, list) else [rows]
    pids = [r['ProcessId'] for r in rows if str(profile) in (r['CommandLine'] or '') and '--type=' not in (r['CommandLine'] or '')]
    return pids[0] if pids else None


def sol_window(pid):
    found = [0]
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def callback(hwnd, _):
        owner = wintypes.DWORD(); user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        title = ctypes.create_unicode_buffer(64); user32.GetWindowTextW(hwnd, title, 64)
        if owner.value == pid and title.value == 'Sol' and user32.IsWindowVisible(hwnd): found[0] = hwnd
        return True
    user32.EnumWindows(callback_type(callback), 0)
    return found[0]


def write_settings(profile, shortcuts):
    data = profile / 'BrowserData'; data.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(data / 'settings.sqlite3')
    db.executescript('PRAGMA application_id=1380271184; PRAGMA user_version=1;'
                     'CREATE TABLE preferences(id INTEGER PRIMARY KEY CHECK(id=1), value TEXT NOT NULL CHECK(length(value)<=65536));')
    # Every field as Sol writes them (an invalid file is ignored and the defaults apply).
    value = {'schema': 1, 'searchEngine': 'google', 'searchSuggestions': False, 'downloadDirectory': '', 'askDownload': False,
             'startup': 'restore', 'homepage': 'https://www.google.com/', 'shortcuts': shortcuts, 'littleSolLinks': False,
             'littleSolClick': True, 'littleSolHotkey': False, 'autoArchiveHours': 12, 'unloadMinutes': 60,
             'backdrop': 'none', 'clock': 'system'}
    db.execute('INSERT INTO preferences(id,value) VALUES(1,?)', (json.dumps(value),)); db.commit(); db.close()
    # This version's release notes were seen: no changelog tab takes the focus.
    (data / 'last-version').write_text((ROOT / 'VERSION').read_text().strip(), encoding='utf-8')


def pinned_urls(profile):
    db = sqlite3.connect(profile / 'BrowserData' / 'workspace.sqlite3')
    rows = [url for (url,) in db.execute('SELECT url FROM nodes WHERE kind=0 AND pinned=1')]
    db.close()
    return rows


def scenario(name, shortcuts, steps, server, beacons):
    directory = ROOT / 'artifacts' / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-keys-') + name + '-' + uuid.uuid4().hex[:4])
    profile = directory / 'profile'
    (profile / 'Default').mkdir(parents=True)  # Not a first run: no welcome.
    write_settings(profile, shortcuts)
    origin = f'http://localhost:{server.server_port}'
    beacons.clear()
    # Links open as tabs here (littleSolLinks off in the settings above).
    subprocess.Popen([str(EXE), '--workspace', f'--profile-dir={profile}', origin + '/handles'], cwd=EXE.parent)
    deadline = time.monotonic() + 30
    while '/loaded/handles' not in beacons and time.monotonic() < deadline: time.sleep(.1)
    time.sleep(2)
    pid = browser_pid(profile)
    window = sol_window(pid)
    results = {}

    def focus_page(path):
        # One click into the page body, so the page has the keyboard.
        keys((0x87, False), (0x87, True))  # F24: lets this script hand the foreground to the window
        user32.SetForegroundWindow(window); time.sleep(.3)
        rect = wintypes.RECT(); user32.GetWindowRect(window, ctypes.byref(rect))
        click(rect.left + (rect.right - rect.left) * 3 // 4, rect.top + (rect.bottom - rect.top) // 2)
        time.sleep(.4)

    def press(vk):
        if user32.GetForegroundWindow() != window: raise SystemExit('The test window lost the foreground; no key was sent')
        chord(vk); time.sleep(1.2)

    for step in steps:
        kind = step[0]
        if kind == 'open':
            beacons.clear()
            subprocess.Popen([str(EXE), '--workspace', f'--profile-dir={profile}', origin + step[1]], cwd=EXE.parent)
            end = time.monotonic() + 15
            while f'/loaded{step[1]}' not in beacons and time.monotonic() < end: time.sleep(.1)
            time.sleep(1.5)
            focus_page(step[1])
        elif kind == 'focus':
            focus_page(step[1])
        elif kind == 'press':
            beacons.clear(); press(step[1]); results[step[2]] = list(beacons)
    # Close gracefully so the tree is saved, then read it.
    user32.PostMessageW(window, 0x0010, 0, 0)
    end = time.monotonic() + 20
    while browser_pid(profile) and time.monotonic() < end: time.sleep(.3)
    helper = user32.FindWindowW('SolTrayHelper', None)
    if helper: user32.PostMessageW(helper, 0x0010, 0, 0)
    if browser_pid(profile): subprocess.run(['taskkill', '/PID', str(browser_pid(profile)), '/T', '/F'], capture_output=True)
    results['pinned'] = [url.replace(origin, '') for url in pinned_urls(profile)]
    return results


def main():
    beacons = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            path = self.path.split('?')[0]
            if path.startswith('/beacon/'):
                beacons.append(path[7:]); self.send_response(204); self.end_headers(); return
            handles = path == '/handles'
            script = ("addEventListener('keydown',e=>{if(e.ctrlKey&&/^[dkt]$/i.test(e.key)){"
                      "new Image().src='/beacon/key/'+e.key.toLowerCase()+location.pathname;" +
                      ("if(e.key.toLowerCase()!=='t')e.preventDefault();" if handles else "") + "}});"
                      "new Image().src='/beacon/loaded'+location.pathname;")
            body = f'<!doctype html><title>{path}</title><body style="height:100vh;margin:0">{path}<script>{script}</script>'.encode()
            self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    checks, notes = {}, {}
    try:
        D, K, T = 0x44, 0x4B, 0x54
        default = scenario('default', {}, [
            ('focus', '/handles'), ('press', D, 'handles_ctrl_d'),
            ('open', '/plain'), ('press', D, 'plain_ctrl_d'),
        ], server, beacons)
        notes['default'] = default
        checks['handled_ctrl_d_reaches_page'] = '/key/d/handles' in default['handles_ctrl_d']
        checks['plain_ctrl_d_reaches_page'] = '/key/d/plain' in default['plain_ctrl_d']
        checks['default_pins_only_plain'] = default['pinned'] == ['/plain']

        moved = scenario('moved', {'pin': {'key': K, 'ctrl': True, 'shift': False, 'alt': False}}, [
            ('focus', '/handles'), ('press', T, 'handles_ctrl_t'),
            ('open', '/plain'), ('press', D, 'plain_ctrl_d'), ('press', K, 'plain_ctrl_k'),
        ], server, beacons)
        notes['moved'] = moved
        checks['reserved_ctrl_t_opens_tab'] = '/loaded/handles' not in moved['handles_ctrl_t'] and '/key/t/handles' not in moved['handles_ctrl_t']
        checks['moved_ctrl_d_reaches_page'] = '/key/d/plain' in moved['plain_ctrl_d']
        checks['moved_ctrl_k_pins_only_plain'] = moved['pinned'] == ['/plain']
    finally:
        server.shutdown()
    result = {'passed': all(checks.values()), 'checks': checks, 'notes': notes}
    print(json.dumps(result, indent=2))
    if not result['passed']: raise SystemExit('page keys failed')


if __name__ == '__main__':
    main()
