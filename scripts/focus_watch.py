"""Read-only watcher for the installed Sol's windows (a diagnostic, not a test; it never clicks).

Every 100 ms it records, to artifacts/focus-watch-<time>.jsonl:
- each top-level window of Sol's processes that appears or changes (class, title, bounds, owner,
  topmost/layered/transparent/no-activate, enabled, visible);
- the foreground window, and the window holding mouse capture or a menu/move loop
  (GetGUIThreadInfo), whenever they change.
Run: python scripts/focus_watch.py [minutes]   (default 10)
"""
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
user32 = ctypes.windll.user32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
GetWindowLongPtrW = user32.GetWindowLongPtrW
GetWindowLongPtrW.restype = ctypes.c_ssize_t


class GUITHREADINFO(ctypes.Structure):
    _fields_ = [('cbSize', wintypes.DWORD), ('flags', wintypes.DWORD), ('hwndActive', wintypes.HWND), ('hwndFocus', wintypes.HWND),
                ('hwndCapture', wintypes.HWND), ('hwndMenuOwner', wintypes.HWND), ('hwndMoveSize', wintypes.HWND),
                ('hwndCaret', wintypes.HWND), ('rcCaret', wintypes.RECT)]


def sol_pids():
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
        "Get-CimInstance Win32_Process | ? { $_.Name -in 'sol.exe','sol-tray.exe' -and $_.ExecutablePath -like '*SolBrowser*' } | Select-Object ProcessId,Name,CommandLine | ConvertTo-Json -Compress"],
        capture_output=True, text=True).stdout
    rows = json.loads(out) if out.strip() else []
    rows = rows if isinstance(rows, list) else [rows]
    return {r['ProcessId']: r['Name'] + ('' if '--type=' not in (r['CommandLine'] or '') else ':' + r['CommandLine'].split('--type=')[1].split()[0]) for r in rows}


def describe(h, pids):
    pid = wintypes.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
    cls, title = ctypes.create_unicode_buffer(256), ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(h, cls, 256); user32.GetWindowTextW(h, title, 256)
    r = wintypes.RECT(); user32.GetWindowRect(h, ctypes.byref(r))
    style, ex = GetWindowLongPtrW(h, -16) & 0xffffffff, GetWindowLongPtrW(h, -20) & 0xffffffff
    return {'hwnd': h, 'proc': pids.get(pid.value, pid.value), 'class': cls.value, 'title': title.value,
            'visible': bool(user32.IsWindowVisible(h)), 'enabled': bool(user32.IsWindowEnabled(h)),
            'topmost': bool(ex & 0x8), 'layered': bool(ex & 0x80000), 'transparent': bool(ex & 0x20),
            'noactivate': bool(ex & 0x08000000), 'toolwindow': bool(ex & 0x80), 'popup': bool(style & 0x80000000),
            'owner': user32.GetWindow(h, 4) or 0, 'rect': [r.left, r.top, r.right, r.bottom]}


def main():
    minutes = float(sys.argv[1]) if len(sys.argv) > 1 else 10
    log = ROOT / 'artifacts' / datetime.now(timezone.utc).strftime('focus-watch-%Y%m%dT%H%M%SZ.jsonl')
    log.parent.mkdir(exist_ok=True)
    pids, pids_at = sol_pids(), time.monotonic()
    known, last_state = {}, None
    end = time.monotonic() + minutes * 60
    with log.open('w', encoding='utf-8') as out:
        def write(kind, data):
            out.write(json.dumps({'t': datetime.now().strftime('%H:%M:%S.%f')[:-3], 'kind': kind, **data}, ensure_ascii=False) + '\n'); out.flush()
        write('start', {'pids': {str(k): v for k, v in pids.items()}})
        print(log, flush=True)
        while time.monotonic() < end:
            if time.monotonic() - pids_at > 5: pids, pids_at = sol_pids(), time.monotonic()
            seen = {}
            cb_t = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

            def cb(h, _):
                pid = wintypes.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
                if pid.value in pids: seen[h] = describe(h, pids)
                return True
            user32.EnumWindows(cb_t(cb), 0)
            for h, w in seen.items():
                if (w['visible'] or known.get(h, {}).get('visible')) and known.get(h) != w: write('window', w)
            for h in set(known) - set(seen):
                if known[h]['visible']: write('gone', {'hwnd': h, 'class': known[h]['class'], 'title': known[h]['title']})
            known = seen
            fg = user32.GetForegroundWindow()
            info = GUITHREADINFO(); info.cbSize = ctypes.sizeof(GUITHREADINFO)
            thread = user32.GetWindowThreadProcessId(fg, None) if fg else 0
            user32.GetGUIThreadInfo(thread, ctypes.byref(info))
            state = {'foreground': describe(fg, pids) if fg else None, 'capture': info.hwndCapture or 0, 'menu_owner': info.hwndMenuOwner or 0,
                     'move_size': info.hwndMoveSize or 0, 'flags': info.flags}
            if state['capture']: state['capture_window'] = describe(info.hwndCapture, pids)
            if state != last_state: write('focus', state); last_state = state
            time.sleep(.1)
        write('end', {})


if __name__ == '__main__':
    main()
