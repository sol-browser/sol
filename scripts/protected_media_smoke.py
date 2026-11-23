"""Open the real Spotify player in a disposable Sol profile and check the native mount."""
import argparse
import ctypes
from ctypes import wintypes as wt
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
from threading import Thread
import time
import uuid

from page_keys_smoke import ROOT, chord, click, keys, sol_window, user32, write_settings
from spotify_ecs_host import GuiThreadInfo, owner_pid, rect, until


def records(path, event):
    if not path.exists():
        return []
    found = []
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        if row.get('event') == event:
            found.append(row.get('details', row))
    return found


def helper_window(root, sol_pid):
    found = []
    callback_type = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def visit(hwnd, _):
        parent = user32.GetParent(hwnd)
        if owner_pid(hwnd) != sol_pid and owner_pid(parent) == sol_pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumChildWindows(root, callback_type(visit), 0)
    return next(iter(found), None)


def client_box(hwnd):
    origin = wt.POINT()
    assert user32.ClientToScreen(hwnd, ctypes.byref(origin))
    return origin.x, origin.y, *rect(hwnd, client=True)[2:]


def check_viewport(child):
    host = client_box(user32.GetParent(child))
    outer = rect(child)
    inner = client_box(child)
    assert outer == host and inner == host, f'Host {host}, Electron frame {outer}, Electron client {inner}'
    gdi = ctypes.windll.gdi32
    region = gdi.CreateRectRgn(0, 0, 0, 0)
    try:
        assert user32.GetWindowRgn(child, region) == 3, 'Electron needs a rounded clipping region'
        assert not gdi.PtInRegion(region, 0, 0), 'Electron corner is square'
        assert gdi.PtInRegion(region, outer[2] // 2, outer[3] // 2), 'Electron region does not fill the page'
    finally:
        gdi.DeleteObject(region)


def check_edge_hit_test(child):
    send = user32.SendMessageTimeoutW
    send.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM, wt.UINT, wt.UINT, ctypes.POINTER(ctypes.c_size_t)]
    send.restype = wt.LPARAM
    x, y, width, height = rect(child)
    for left, top in ((x, y + height // 2), (x + width - 1, y + height // 2),
                      (x + width // 2, y + height - 1), (x + width - 1, y + height - 1)):
        point = (left & 0xffff) | ((top & 0xffff) << 16)
        result = ctypes.c_size_t()
        assert send(child, 0x84, 0, point, 0x2, 1000, ctypes.byref(result)), 'Electron did not answer WM_NCHITTEST'
        assert not 10 <= result.value <= 17, f'Electron exposes a resize edge at {(left, top)}'


def keyboard_target():
    info = GuiThreadInfo()
    info.size = ctypes.sizeof(info)
    return info.focus if user32.GetGUIThreadInfo(0, ctypes.byref(info)) else None


def in_foreground(root):
    return user32.GetAncestor(user32.GetForegroundWindow(), 2) == root


def activate_test_window(root):
    if not in_foreground(root):
        user32.SetForegroundWindow(root)
    try:
        until('foreground Sol test window', lambda: in_foreground(root), 5)
    except RuntimeError as exc:
        foreground = user32.GetForegroundWindow()
        raise RuntimeError(f'{exc}; expected {root}, foreground {foreground} (PID {owner_pid(foreground)})') from exc


def focus_view(root, child):
    until('foreground Sol test window', lambda: in_foreground(root), 5)
    targets = []

    def hit():
        x, y, width, height = rect(child)
        targets.clear()
        for column, row in ((3, 2), (2, 2), (3, 3), (2, 3)):
            point = wt.POINT(x + width * column // 4, y + height * row // 4)
            target = user32.WindowFromPoint(point)
            targets.append((target, owner_pid(target)))
            if (target == child or user32.IsChild(child, target)) and in_foreground(root):
                return point
        return None

    try:
        point = until('the Electron tab receives native clicks', hit, 5)
    except RuntimeError as exc:
        raise RuntimeError(f'{exc}; Electron {child}, area {rect(child)}, targets {targets}') from exc
    previous_focus = keyboard_target()
    click(point.x, point.y)
    try:
        until('keyboard focus follows the native click into Electron',
              lambda: user32.IsChild(child, keyboard_target()), 5)
    except RuntimeError as exc:
        raise RuntimeError(f'{exc}; focus before {previous_focus}, after {keyboard_target()}, Electron {child}') from exc


class Page(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.end_headers()
        self.wfile.write(b'<title>Ordinary Sol tab</title>')


def main(exe, foreground):
    directory = ROOT / 'artifacts' / ('protected-media-' + uuid.uuid4().hex[:8])
    profile = directory / 'profile'
    (profile / 'Default').mkdir(parents=True)
    write_settings(profile, {})
    print(directory, flush=True)
    report = directory / 'sol.jsonl'
    base = [str(exe), '--workspace', f'--profile-dir={profile}']
    spotify = 'https://open.spotify.com/'
    server = ThreadingHTTPServer(('127.0.0.1', 0), Page)
    Thread(target=server.serve_forever, daemon=True).start()
    ordinary = f'http://127.0.0.1:{server.server_port}/'
    handle = None
    process = subprocess.Popen(base + [f'--probe-report={report}', '--probe-input', spotify], cwd=exe.parent)
    try:
        def window():
            if process.poll() is not None:
                raise RuntimeError(f'Sol exited before opening a window ({process.returncode})')
            return sol_window(process.pid)

        handle = until('Sol window', window, 25)

        def ready():
            rows = records(report, 'protected_media_tab')
            if rows and rows[-1]['failed']:
                raise RuntimeError('The Protected Media tab failed to load')
            return next((row for row in rows if row['committed'] and row['mounted']), None)

        loaded = until('signed Electron Spotify page', ready, 90)
        assert loaded['profile'] == 'default'
        until('Spotify favicon decoded by Sol', lambda: records(report, 'protected_media_favicon'), 30)
        child = until('visible Electron child of Sol', lambda: helper_window(handle, process.pid), 10)
        helper_pid = owner_pid(child)
        assert 0 < helper_pid != process.pid
        check_viewport(child)
        check_edge_hit_test(child)
        if foreground:
            activate_test_window(handle)
            focus_view(handle, child)
        initial_box = rect(child)
        user32.ShowWindow(handle, 9)
        x, y, width, height = rect(handle)
        user32.SetWindowPos(handle, 0, x, y, max(680, width - 100), max(500, height - 75), 0x14)
        until('Electron on new Sol bounds', lambda: rect(child) != initial_box, 8)
        check_viewport(child)

        # The new ordinary tab arrives through the same OS handoff as an external link.
        handoff = subprocess.run(base + [ordinary], cwd=exe.parent, timeout=15)
        assert handoff.returncode in (0, 24), handoff.returncode
        until('Electron hidden behind ordinary tab', lambda: not user32.IsWindowVisible(child), 15)
        assert user32.IsWindow(child), 'Switching away destroyed the Spotify tab'
        assert (profile / 'BrowserData/ProtectedMedia/Partitions/spotify-default').is_dir()
        if foreground:
            activate_test_window(handle)
            chord(0x09)  # Ctrl+Tab to the previously used Spotify tab.
            until('the same Electron child restored', lambda: user32.IsWindowVisible(child), 5)
            focus_view(handle, child)
            check_viewport(child)
            time.sleep(.25)
            assert in_foreground(handle)
            keys((0x11, False))
            try:
                time.sleep(.06)
                assert in_foreground(handle)
                keys((0x57, False), (0x57, True))  # Ctrl+W with physically focused Electron content.
            finally:
                keys((0x11, True))
            until('Ctrl+W closed the Electron child', lambda: not user32.IsWindow(child), 8)
        user32.PostMessageW(handle, 0x10, 0, 0)
        assert process.wait(timeout=25) == 0
        until('Electron child closed', lambda: not user32.IsWindow(child), 8)
        print('Passed Spotify navigation, favicon, rounded viewport, resize, tab visibility, profile partition and shutdown.')
    finally:
        server.shutdown()
        server.server_close()
        if process.poll() is None and handle and owner_pid(handle) == process.pid:
            user32.PostMessageW(handle, 0x10, 0, 0)
        if process.poll() is None:
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=10)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--foreground', action='store_true', help='Exercise tab switches and Ctrl+W with physical keyboard input')
    args = parser.parse_args()
    main(args.exe.resolve(), args.foreground)
