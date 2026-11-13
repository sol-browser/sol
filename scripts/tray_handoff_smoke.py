"""sol-tray.exe (docs/little-sol.md, "Background"); real CEF, a disposable profile.

1. Sol starts and its windows are closed: Sol must exit and leave sol-tray.exe running.
2. sol-tray.exe's Ctrl+Alt+N (its WM_HOTKEY, posted, after one F24 key press so that the bar may
   take the foreground as with a real key press): its own command bar must show at once and hold
   the foreground, and no Sol may start while typing. Typed text (posted WM_CHAR) and Enter must
   open the address in Little Sol, loaded once; the helper must exit once that window is there.
3. Closing that Little Sol hands over to the helper again; Ctrl+Alt+N and Escape start nothing.
4. A link from another app while only the helper runs (`sol.exe <link>`, a cold start): only Little
   Sol may ever show, never the main window; the link loads once and the helper exits.

The real Ctrl+Alt+N is not pressed: the installed Sol may hold it. Nothing is typed with the real
keyboard. Runs open windows and a second tray icon for a few seconds.
"""
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get('RED_BROWSER_TEST_EXE', ROOT / 'build/Release/apps/desktop/Release/sol.exe'))
user32 = ctypes.windll.user32
WM_CLOSE, WM_HOTKEY, WM_CHAR, WM_KEYDOWN = 0x0010, 0x0312, 0x0102, 0x0100
HOTKEY_ID = 0x534C  # sol_tray.cpp kHotkeyId


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [('wVk', wintypes.WORD), ('wScan', wintypes.WORD), ('dwFlags', wintypes.DWORD), ('time', wintypes.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [('ki', KEYBDINPUT), ('pad', ctypes.c_byte * 32)]
    _anonymous_ = ('u',)
    _fields_ = [('type', wintypes.DWORD), ('u', _U)]


def tap_f24():
    """A key press without effect: this process received the last input and may pass the foreground on."""
    keys = (INPUT * 2)()
    keys[0].type = keys[1].type = 1
    keys[0].ki.wVk = keys[1].ki.wVk = 0x87
    keys[1].ki.dwFlags = 2
    user32.SendInput(2, keys, ctypes.sizeof(INPUT))


def query(name):
    script = (f"Get-CimInstance Win32_Process -Filter \"Name='{name}'\" | "
              "Select-Object ProcessId,CommandLine,PrivatePageCount | ConvertTo-Json -Compress")
    out = subprocess.run(['powershell', '-NoProfile', '-Command', script], capture_output=True, text=True).stdout.strip()
    rows = json.loads(out) if out else []
    return rows if isinstance(rows, list) else [rows]


def helpers(profile):
    return [(r['ProcessId'], round(int(r['PrivatePageCount']) / 2**20, 1)) for r in query('sol-tray.exe') if str(profile) in (r['CommandLine'] or '')]


def browser_pids(profile):
    return [r['ProcessId'] for r in query('sol.exe') if str(profile) in (r['CommandLine'] or '') and '--type=' not in (r['CommandLine'] or '')]


def describe(hwnd):
    cls, title, pid = ctypes.create_unicode_buffer(128), ctypes.create_unicode_buffer(128), wintypes.DWORD()
    user32.GetClassNameW(hwnd, cls, 128); user32.GetWindowTextW(hwnd, title, 128)
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return cls.value, title.value, pid.value


def windows(pids):
    found = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def callback(hwnd, _):
        _, title, pid = describe(hwnd)
        if pid in pids and user32.IsWindowVisible(hwnd): found.append((hwnd, title))
        return True
    user32.EnumWindows(callback_type(callback), 0)
    return found


def wait(condition, seconds, step=.25):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = condition()
        if value: return value
        time.sleep(step)
    return condition()


def bar_window():
    hwnd = user32.FindWindowW('SolTrayCommandBar', None)
    return hwnd if hwnd and user32.IsWindowVisible(hwnd) else 0


def bar_height():
    rect = wintypes.RECT()
    user32.GetWindowRect(user32.FindWindowW('SolTrayCommandBar', None), ctypes.byref(rect))
    return rect.bottom - rect.top


def press_hotkey():
    tap_f24()
    helper = user32.FindWindowW('SolTrayHelper', None)
    pid = wintypes.DWORD(); user32.GetWindowThreadProcessId(helper, ctypes.byref(pid))
    user32.AllowSetForegroundWindow(pid.value)
    user32.PostMessageW(helper, WM_HOTKEY, HOTKEY_ID, 0)


def main():
    directory = ROOT / 'artifacts' / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-tray-') + uuid.uuid4().hex[:6])
    profile = directory / 'profile'
    (profile / 'Default').mkdir(parents=True)  # Not a first run: no welcome window.
    checks, notes = {}, {}
    requests = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            requests.append(self.path)
            body = b'<!doctype html><title>typed</title><h1>typed</h1>'
            self.send_response(200 if self.path != '/favicon.ico' else 404)
            self.send_header('Content-Type', 'text/html'); self.send_header('Content-Length', str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        # 1. A normal start; close everything it shows.
        subprocess.Popen([str(EXE), '--workspace', f'--profile-dir={profile}'], cwd=EXE.parent)
        pids = wait(lambda: browser_pids(profile), 20)
        wait(lambda: windows(set(pids)), 20)
        time.sleep(3)
        for hwnd, _ in windows(set(pids)): user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
        checks['closed_sol_exits'] = wait(lambda: not browser_pids(profile), 30)
        helper = wait(lambda: helpers(profile), 10)
        checks['tray_helper_runs'] = bool(helper)
        notes['tray_helper_mb'] = helper[0][1] if helper else None
        wait(lambda: user32.FindWindowW('SolTrayHelper', None), 5)

        # 2. Ctrl+Alt+N: the bar at once, holding the foreground; Sol only after Enter.
        started = time.monotonic()
        press_hotkey()
        bar = wait(bar_window, 3, step=.005)
        notes['hotkey_to_bar_ms'] = round((time.monotonic() - started) * 1000)
        checks['bar_at_once'] = bool(bar) and notes['hotkey_to_bar_ms'] < 150
        checks['bar_has_foreground'] = bool(wait(lambda: user32.GetForegroundWindow() == bar, 1, step=.01))
        edit = user32.FindWindowExW(bar, None, 'Edit', None)
        for character in f'localhost:{server.server_port}/typed':
            user32.PostMessageW(edit, WM_CHAR, ord(character), 0); time.sleep(.01)
        time.sleep(.6)
        checks['no_sol_while_typing'] = not browser_pids(profile)
        checks['bar_still_has_foreground'] = user32.GetForegroundWindow() == bar
        checks['result_row_shown'] = bar_height() > 100
        submitted = time.monotonic()
        user32.PostMessageW(edit, WM_KEYDOWN, 0x0D, 0)
        little = wait(lambda: [w for w in windows(set(browser_pids(profile))) if w[1] == 'Little Sol'], 30, step=.02)
        notes['enter_to_little_sol_ms'] = round((time.monotonic() - submitted) * 1000)
        checks['opened_in_little_sol'] = bool(little) and bool(wait(lambda: requests.count('/typed') == 1, 10))
        checks['helper_exits_after'] = wait(lambda: not helpers(profile), 10)
        time.sleep(1)
        checks['loaded_once'] = requests.count('/typed') == 1
        foreground = describe(user32.GetForegroundWindow())
        notes['foreground_after'] = foreground[1]
        checks['little_sol_has_foreground'] = foreground[1] == 'Little Sol'
        visible = [title for _, title in windows(set(browser_pids(profile)))]
        notes['visible_after'] = visible
        checks['only_little_sol'] = visible == ['Little Sol']

        # 3. Back to the tray; Ctrl+Alt+N and Escape start nothing.
        for hwnd, _ in little: user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
        checks['little_closed_sol_exits'] = wait(lambda: not browser_pids(profile), 30)
        checks['tray_helper_back'] = bool(wait(lambda: helpers(profile), 10))
        wait(lambda: user32.FindWindowW('SolTrayHelper', None), 5)
        press_hotkey()
        bar = wait(bar_window, 3, step=.01)
        user32.PostMessageW(user32.FindWindowExW(bar, None, 'Edit', None), WM_KEYDOWN, 0x1B, 0)
        checks['escape_hides_bar'] = bool(bar) and bool(wait(lambda: not bar_window(), 2, step=.02))
        time.sleep(1)
        checks['escape_starts_nothing'] = not browser_pids(profile)

        # 4. A link from another app at a cold start from the tray.
        subprocess.Popen([str(EXE), '--workspace', f'--profile-dir={profile}', f'http://localhost:{server.server_port}/link'], cwd=EXE.parent)
        shown, end = set(), time.monotonic() + 5  # Little Sol takes about a second; watch well past it.
        while time.monotonic() < end:
            shown.update(title for _, title in windows(set(browser_pids(profile))))
            time.sleep(.02)
        notes['link_windows_seen'] = sorted(shown)
        checks['link_opens_only_little_sol'] = shown == {'Little Sol'}
        checks['link_loaded_once'] = requests.count('/link') == 1
        checks['link_helper_exits'] = wait(lambda: not helpers(profile), 10)
    finally:
        server.shutdown(); server.server_close()
        hwnd = user32.FindWindowW('SolTrayHelper', None)
        if hwnd: user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
        for pid in browser_pids(profile): subprocess.run(['taskkill', '/PID', str(pid), '/T', '/F'], capture_output=True)
    checks = {key: bool(value) for key, value in checks.items()}
    result = {'passed': all(checks.values()), 'checks': checks, 'notes': notes}
    (directory / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    if not result['passed']: raise SystemExit('tray hand-off failed')


if __name__ == '__main__':
    main()
