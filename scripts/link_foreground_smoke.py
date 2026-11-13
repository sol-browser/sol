"""Links from another app bring Sol to the front (real CEF, a disposable profile).

Another app (a window of this script, made foreground as by a click) launches `sol.exe <link>`
while Sol runs behind it, as Windows does for a link clicked in a chat. Sol's window that takes
the link must become the foreground window:
- a file (always a tab): the workspace window;
- a website link (Little Sol on, the default): the new Little Sol.

One F24 key press (no visible effect) lets this script take the foreground first; nothing else is
pressed or typed. Opens windows for a few seconds.
"""
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import threading
import time
import tkinter
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get('RED_BROWSER_TEST_EXE', ROOT / 'build/Release/apps/desktop/Release/sol.exe'))
user32 = ctypes.windll.user32


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [('wVk', wintypes.WORD), ('wScan', wintypes.WORD), ('dwFlags', wintypes.DWORD), ('time', wintypes.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [('ki', KEYBDINPUT), ('pad', ctypes.c_byte * 32)]
    _anonymous_ = ('u',)
    _fields_ = [('type', wintypes.DWORD), ('u', _U)]


def tap_f24():
    keys = (INPUT * 2)()
    keys[0].type = keys[1].type = 1
    keys[0].ki.wVk = keys[1].ki.wVk = 0x87
    keys[1].ki.dwFlags = 2
    user32.SendInput(2, keys, ctypes.sizeof(INPUT))


def title(hwnd):
    buffer = ctypes.create_unicode_buffer(256); user32.GetWindowTextW(hwnd, buffer, 256)
    pid = wintypes.DWORD(); user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return buffer.value, pid.value


def sol_main_pid(profile):
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
        "Get-CimInstance Win32_Process -Filter \"Name='sol.exe'\" | Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress"],
        capture_output=True, text=True).stdout
    rows = json.loads(out) if out.strip() else []
    rows = rows if isinstance(rows, list) else [rows]
    pids = [r['ProcessId'] for r in rows if str(profile) in (r['CommandLine'] or '') and '--type=' not in (r['CommandLine'] or '')]
    return pids[0] if pids else None


def main():
    directory = ROOT / 'artifacts' / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-foreground-') + uuid.uuid4().hex[:6])
    profile = directory / 'profile'
    (profile / 'Default').mkdir(parents=True)
    page = directory / 'page.html'
    page.write_text('<!doctype html><title>file page</title>file page', encoding='utf-8')

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            body = b'<!doctype html><title>web page</title>web page'
            self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Content-Length', str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    subprocess.Popen([str(EXE), '--workspace', f'--profile-dir={profile}'], cwd=EXE.parent)
    deadline = time.monotonic() + 30
    while not sol_main_pid(profile) and time.monotonic() < deadline: time.sleep(.3)
    time.sleep(5)  # The window settles and loads.
    sol = sol_main_pid(profile)

    other = tkinter.Tk(); other.title('Other app'); other.geometry('500x300+80+80'); other.update()
    other_hwnd = user32.GetParent(other.winfo_id()) or other.winfo_id()
    checks, notes = {}, {}

    def launch_from_other(target, label, expect, background=False):
        tap_f24()
        user32.SetForegroundWindow(other_hwnd); other.update(); time.sleep(.4)
        notes[label + '_before'] = title(user32.GetForegroundWindow())[0]
        if background:
            # A process that is not in the foreground starts it (a notification, an app in the tray):
            # WMI's host has no right to hand the foreground on.
            command = f'"{EXE}" --workspace "--profile-dir={profile}" "{target}"'
            subprocess.run(['powershell', '-NoProfile', '-Command',
                "Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = '" + command.replace("'", "''") + "' } | Out-Null"],
                capture_output=True)
        else:
            # The foreground app starts the browser, as ShellExecute does for a clicked link.
            subprocess.Popen([str(EXE), '--workspace', f'--profile-dir={profile}', target], cwd=EXE.parent)
        end = time.monotonic() + 8
        seen = None
        while time.monotonic() < end:
            other.update()
            name, pid = title(user32.GetForegroundWindow())
            if pid == sol and expect(name): seen = name; break
            time.sleep(.05)
        notes[label + '_after'] = title(user32.GetForegroundWindow())[0]
        checks[label + '_in_front'] = seen is not None

    launch_from_other(str(page), 'file_tab', lambda name: name == 'Sol')
    # Sol minimized: the window must come back and to the front.
    main_window = [0]
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def find(hwnd, _):
        name, pid = title(hwnd)
        if pid == sol and name == 'Sol' and user32.IsWindowVisible(hwnd): main_window[0] = hwnd
        return True
    user32.EnumWindows(callback_type(find), 0)
    user32.ShowWindow(main_window[0], 6)  # SW_MINIMIZE
    time.sleep(.6)
    launch_from_other(str(page), 'minimized_tab', lambda name: name == 'Sol')
    checks['minimized_restored'] = not user32.IsIconic(main_window[0])
    launch_from_other(f'http://localhost:{server.server_port}/web', 'little_sol', lambda name: name == 'Little Sol')
    launch_from_other(str(page), 'background_launcher_tab', lambda name: name == 'Sol', background=True)
    launch_from_other(f'http://localhost:{server.server_port}/web2', 'background_launcher_little_sol', lambda name: name == 'Little Sol', background=True)
    other.destroy()
    server.shutdown()
    subprocess.run(['powershell', '-NoProfile', '-Command',
        f"Get-CimInstance Win32_Process -Filter \"Name='sol.exe'\" | ? {{ $_.CommandLine -like '*{directory.name}*' }} | % {{ Stop-Process -Id $_.ProcessId -Force }}"],
        capture_output=True)
    result = {'passed': all(checks.values()), 'checks': checks, 'notes': notes}
    (directory / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    if not result['passed']: raise SystemExit('link foreground failed')


if __name__ == '__main__':
    main()
