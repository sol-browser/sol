"""Real Windows Save As for a download opened with the middle mouse button.

Runs normal CEF with a disposable profile. Takes keyboard and mouse focus
briefly to exercise the browser and its native dialog.
"""
import ctypes
from ctypes import wintypes
import json
import sqlite3
import subprocess
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from page_keys_smoke import EXE, INPUT, ROOT, chord, keys, sol_window, user32, write_settings


BODY = b'Sol Save As regression\n'
PAGE = b'''<!doctype html><title>Sol Save As fixture</title>
<a style="position:fixed;inset:0;display:grid;place-items:center" href="/attachment">Download fixture</a>'''


def wait_for(check, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(.05)
    return None


def find_window(pid, matches):
    found = []
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    @callback
    def visit(hwnd, _):
        process = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(process))
        if process.value == pid and user32.IsWindowVisible(hwnd) and matches(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(visit, 0)
    return found[0] if len(found) == 1 else None


def save_dialog(pid, owner):
    def matches(hwnd):
        name = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(hwnd, name, len(name))
        return name.value == '#32770' and user32.GetAncestor(hwnd, 3) == owner

    return find_window(pid, matches)


def little_window(pid):
    def matches(hwnd):
        title = ctypes.create_unicode_buffer(64)
        user32.GetWindowTextW(hwnd, title, len(title))
        return title.value == 'Little Sol'

    return find_window(pid, matches)


def middle_click(window):
    keys((0x87, False), (0x87, True))  # F24, as in the existing shortcut smoke.
    user32.SetForegroundWindow(window)
    if not wait_for(lambda: user32.GetForegroundWindow() == window, 2):
        raise RuntimeError('The test window did not get the foreground')
    rect = wintypes.RECT()
    pointer = wintypes.POINT()
    user32.GetWindowRect(window, ctypes.byref(rect))
    user32.GetCursorPos(ctypes.byref(pointer))
    user32.SetCursorPos((rect.left + rect.right * 3) // 4, (rect.top + rect.bottom) // 2)
    try:
        batch = (INPUT * 2)()
        batch[0].mi.dwFlags, batch[1].mi.dwFlags = 0x20, 0x40
        if user32.GetForegroundWindow() != window or user32.SendInput(2, batch, ctypes.sizeof(INPUT)) != 2:
            raise RuntimeError('Could not middle-click the test window')
        time.sleep(.15)
    finally:
        user32.SetCursorPos(pointer.x, pointer.y)


def save_as(dialog, destination):
    if not wait_for(lambda: user32.GetForegroundWindow() == dialog, 3):
        raise RuntimeError('The Save As dialog did not get the foreground')
    # The Save As dialog initially focuses the file name edit. Verify before
    # typing so another control cannot receive the path.
    class GuiThreadInfo(ctypes.Structure):
        _fields_ = [('size', wintypes.DWORD), ('flags', wintypes.DWORD),
                    *((name, wintypes.HWND) for name in ('active', 'focus', 'capture', 'menu', 'move', 'caret')),
                    ('rect', wintypes.RECT)]

    info = GuiThreadInfo(size=ctypes.sizeof(GuiThreadInfo))
    name = ctypes.create_unicode_buffer(64)
    user32.GetGUIThreadInfo(user32.GetWindowThreadProcessId(dialog, None), ctypes.byref(info))
    user32.GetClassNameW(info.focus, name, len(name))
    if name.value != 'Edit' or not user32.IsChild(dialog, info.focus):
        raise RuntimeError(f'Unexpected Save As focus: {name.value}')
    chord(0x41)
    path = str(destination).encode('utf-16-le')
    batch = (INPUT * len(path))()
    for i in range(0, len(path), 2):
        for up in (0, 1):
            key = batch[i + up]
            key.type = 1
            key.ki.wScan = int.from_bytes(path[i:i + 2], 'little')
            key.ki.dwFlags = 4 | (2 if up else 0)
    if user32.GetForegroundWindow() != dialog or user32.SendInput(len(batch), batch, ctypes.sizeof(INPUT)) != len(batch):
        raise RuntimeError('Could not set the Save As filename')
    keys((0x0d, False), (0x0d, True))


def main():
    directory = ROOT / 'artifacts' / ('save-dialog-' + uuid.uuid4().hex[:8])
    profile, downloads = directory / 'profile', directory / 'downloads'
    (profile / 'Default').mkdir(parents=True)
    downloads.mkdir()
    write_settings(profile, {})
    with sqlite3.connect(profile / 'BrowserData/settings.sqlite3') as db:
        settings = json.loads(db.execute('SELECT value FROM preferences WHERE id=1').fetchone()[0])
        settings.update(askDownload=True, downloadDirectory=str(downloads))
        db.execute('UPDATE preferences SET value=? WHERE id=1', (json.dumps(settings),))

    served = threading.Event()
    requests = []

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            attachment = self.path in ('/attachment', '/external')
            requests.append(self.path)
            content = BODY if attachment else PAGE
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream' if attachment else 'text/html')
            self.send_header('Content-Length', str(len(content)))
            if attachment:
                self.send_header('Content-Disposition', 'attachment; filename="fixture.txt"')
            self.end_headers()
            self.wfile.write(content)
            if self.path == '/source':
                served.set()

    with ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server, (directory / 'process.log').open('w', encoding='utf-8') as log:
        threading.Thread(target=server.serve_forever, daemon=True).start()
        origin = f'http://localhost:{server.server_port}'
        command = [str(EXE), '--workspace', f'--profile-dir={profile}']
        process = subprocess.Popen([*command, origin + '/source'],
                                   cwd=EXE.parent, stdout=log, stderr=log)
        little = None
        try:
            window = wait_for(lambda: sol_window(process.pid), 30)
            if not window or not served.wait(25):
                raise RuntimeError('The test page did not open')
            time.sleep(1)
            middle_click(window)
            dialog = wait_for(lambda: save_dialog(process.pid, window))
            if not dialog:
                raise RuntimeError(f'Save As did not appear after middle-click; requests: {requests}')
            destination = downloads / 'chosen.txt'
            save_as(dialog, destination)
            if not wait_for(lambda: destination.exists() and destination.read_bytes() == BODY):
                raise RuntimeError('The download did not complete at the chosen path')

            def history_rows():
                with sqlite3.connect(profile / 'BrowserData/history.sqlite3') as history:
                    return history.execute('SELECT name, state FROM downloads ORDER BY id').fetchall()

            def saved_without_tab():
                with sqlite3.connect(profile / 'BrowserData/workspace.sqlite3') as workspace:
                    tabs = workspace.execute('SELECT url FROM nodes WHERE kind=0').fetchall()
                return history_rows() == [('chosen.txt', 'complete')] and tabs == [(origin + '/source',)]

            if not wait_for(saved_without_tab):
                raise RuntimeError('Download history or temporary tab cleanup did not converge')
            if requests.count('/attachment') != 1:
                raise RuntimeError(f'Expected one download request: {requests}')
            subprocess.run([*command, f'--little-sol-url={origin}/external'], cwd=EXE.parent, timeout=10, stdout=log, stderr=log)
            little = wait_for(lambda: little_window(process.pid))
            if not little:
                raise RuntimeError('External launch did not open Little Sol')
            dialog = wait_for(lambda: save_dialog(process.pid, little))
            if not dialog:
                raise RuntimeError('Little Sol did not show Save As')
            little_file = downloads / 'chosen-little.txt'
            save_as(dialog, little_file)
            if not wait_for(lambda: little_file.exists() and little_file.read_bytes() == BODY and not user32.IsWindow(little)):
                raise RuntimeError('Little Sol did not save the chosen file and close')
            if requests.count('/external') != 1 or not wait_for(lambda: ('chosen-little.txt', 'complete') in history_rows()):
                raise RuntimeError('External download duplicated or was missing from history')

            middle_click(window)
            dialog = wait_for(lambda: save_dialog(process.pid, window))
            if not dialog or not wait_for(lambda: user32.GetForegroundWindow() == dialog, 3):
                raise RuntimeError('The second middle-click did not show Save As')
            keys((0x1b, False), (0x1b, True))
            if not wait_for(lambda: any(state == 'canceled' for _, state in history_rows())):
                raise RuntimeError('Dismissing Save As did not cancel the download')
            if {path.name for path in downloads.iterdir()} != {'chosen.txt', 'chosen-little.txt'}:
                raise RuntimeError('Cancel left an extra file in the download directory')
            print(json.dumps({'passed': True, 'directory': str(directory), 'checks': ['Save As shown after middle-click and external launch',
                'chosen files saved with one request each', 'history recorded', 'temporary tab and Little Sol closed', 'cancel left no file']}))
        finally:
            for owner in (little, window):
                if owner and user32.IsWindow(owner):
                    dialog = save_dialog(process.pid, owner)
                    if dialog:
                        user32.PostMessageW(dialog, 0x10, 0, 0)
                    user32.PostMessageW(owner, 0x10, 0, 0)
            try:
                process.wait(timeout=7)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
            server.shutdown()


if __name__ == '__main__':
    main()
