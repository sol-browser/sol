"""Record README animations of the real Sol (animated WebP).

Each scene starts Sol on a copy of a seeded demo profile (no personal data, no
sign-ins), drives it with real mouse and keyboard input and records the window
from the screen, cursor included. Element positions come from UI Automation
(scripts/readme_uia.ps1), so nothing in Sol is changed for the recording.

Recording takes the foreground: run it while nobody uses the PC.

  python scripts/readme_record.py --prepare        # build and warm the demo profile
  python scripts/readme_record.py spaces split     # record scenes
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import json
import math
import shutil
import sqlite3
import subprocess
import threading
import time
import uuid
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'artifacts' / 'readme-record'
BASE = WORK / 'profile'
OUTPUT = ROOT / 'assets' / 'readme'
WINDOW = dict(x=120, y=90, width=1280, height=800)
WIDTH = 1120  # published animation width

user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
user32.SetProcessDPIAware()
# Handles are pointer-sized; ctypes' default int return type truncates them on x64.
H = ctypes.c_void_p
user32.GetDC.restype = H; user32.GetDC.argtypes = [H]
user32.ReleaseDC.argtypes = [H, H]
gdi32.CreateCompatibleDC.restype = H; gdi32.CreateCompatibleDC.argtypes = [H]
gdi32.CreateCompatibleBitmap.restype = H; gdi32.CreateCompatibleBitmap.argtypes = [H, ctypes.c_int, ctypes.c_int]
gdi32.SelectObject.restype = H; gdi32.SelectObject.argtypes = [H, H]
gdi32.DeleteObject.argtypes = [H]; gdi32.DeleteDC.argtypes = [H]
gdi32.BitBlt.argtypes = [H, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, H, ctypes.c_int, ctypes.c_int, wt.DWORD]
gdi32.GetDIBits.argtypes = [H, H, wt.UINT, wt.UINT, ctypes.c_void_p, ctypes.c_void_p, wt.UINT]
user32.DrawIconEx.argtypes = [H, ctypes.c_int, ctypes.c_int, H, ctypes.c_int, ctypes.c_int, wt.UINT, H, wt.UINT]
user32.WindowFromPoint.restype = H; user32.GetAncestor.restype = H; user32.GetAncestor.argtypes = [H, wt.UINT]
user32.GetForegroundWindow.restype = H
user32.GetWindowThreadProcessId.argtypes = [H, ctypes.POINTER(wt.DWORD)]

# --- Demo workspace ---------------------------------------------------------------------
# theme_mode: 0 light, 1 dark, 2 system (ThemeMode in app/theme.h). Mostly dark, at the user's request.
SPACES = [
    dict(key='work', name='Work', icon='\U0001F4BC', hue=228, intensity=120, mode=1, noise=10),     # briefcase
    dict(key='studio', name='Studio', icon='\U0001F3A7', hue=300, intensity=130, mode=1, noise=14),  # headphones
    dict(key='travel', name='Travel', icon='\U0001F9ED', hue=158, intensity=120, mode=1, noise=10),   # compass
]
FAVORITES = [('GitHub', 'https://github.com/'), ('Wikipedia', 'https://www.wikipedia.org/'),
             ('YouTube', 'https://www.youtube.com/'), ('Figma', 'https://www.figma.com/')]
TREE = {  # Space -> (pinned entries, Today tabs); a folder is (title, icon, [(title, url)]).
    'work': ([('Product', 'rocket', [('Apple', 'https://www.apple.com/uk/'), ('iPhone', 'https://www.apple.com/uk/iphone/')]),
              ('Sol on GitHub', 'https://github.com/sol-browser/sol')],
             [('MacBook Pro', 'https://www.apple.com/uk/macbook-pro/'), ('Web browsers', 'https://en.wikipedia.org/wiki/Web_browser'),
              ('Vision Pro', 'https://www.apple.com/uk/apple-vision-pro/')]),
    'studio': ([('Inspiration', 'palette', [('Figma', 'https://www.figma.com/'), ('Dribbble', 'https://dribbble.com/')]),
                ('Design', 'https://en.wikipedia.org/wiki/Graphic_design')],
               [('Apple Music', 'https://www.apple.com/uk/apple-music/')]),
    'travel': ([('Trips', 'plane', [('Iceland', 'https://en.wikipedia.org/wiki/Iceland'), ('Japan', 'https://en.wikipedia.org/wiki/Japan')])],
               [('Kyoto', 'https://en.wikipedia.org/wiki/Kyoto')]),
}


def seed(database: Path):
    with sqlite3.connect(database) as db:
        first = db.execute('SELECT active_space FROM state').fetchone()[0]
        db.execute('DELETE FROM nodes')
        db.execute('DELETE FROM spaces WHERE id<>?', (first,))
        ids = {}
        for position, s in enumerate(SPACES):
            sid = first if position == 0 else str(uuid.uuid4())
            ids[s['key']] = sid
            if position == 0:
                db.execute('UPDATE spaces SET name=?,icon=?,theme_hue=?,theme_intensity=?,theme_mode=?,theme_noise=? WHERE id=?',
                           (s['name'], s['icon'], s['hue'], s['intensity'], s['mode'], s['noise'], sid))
            else:
                db.execute('INSERT INTO spaces(id,name,icon,position,theme_hue,theme_intensity,theme_mode,theme_noise) VALUES(?,?,?,?,?,?,?,?)',
                           (sid, s['name'], s['icon'], position, s['hue'], s['intensity'], s['mode'], s['noise']))
        now = int(time.time() * 1000)
        top = [0]  # Positions of top-level entries are one sequence across sections and Spaces.

        def add(title, url='', *, parent=None, folder=False, pinned=True, favorite=False, space='', icon='', position=None):
            node = str(uuid.uuid4())
            if position is None:
                position, top[0] = top[0], top[0] + 1
            db.execute('INSERT INTO nodes(id,parent_id,kind,title,url,position,pinned,expanded,favorite,archived,page_title,custom_title,'
                       'space_id,last_active_at,icon) VALUES(?,?,?,?,?,?,?,1,?,0,?,1,?,?,?)',
                       (node, parent, int(folder), title, url, position, int(pinned), int(favorite), '' if folder else title,
                        '' if favorite or parent else space, now, icon))
            return node
        for title, url in FAVORITES:
            add(title, url, favorite=True)
        active = {}
        for key, (pinned, today) in TREE.items():
            for entry in pinned:
                if len(entry) == 3:
                    folder = add(entry[0], folder=True, space=ids[key], icon=entry[1])
                    for child, (title, url) in enumerate(entry[2]):
                        add(title, url, parent=folder, position=child)
                else:
                    add(entry[0], entry[1], space=ids[key])
            for title, url in today:
                node = add(title, url, pinned=False, space=ids[key])
                active.setdefault(key, node)
        for key, node in active.items():
            db.execute('UPDATE spaces SET active_id=? WHERE id=?', (node, ids[key]))
        db.execute('UPDATE state SET active_space=?,active_id=?,sidebar_width=256,revision=revision+1', (ids['work'], active['work']))
        (WORK / 'scene.json').write_text(json.dumps(dict(spaces=ids)), encoding='utf-8')


def write_settings(data: Path):
    # Every required field, or Sol discards the file (and the interface follows the system language).
    preferences = dict(schema=1, searchEngine='google', downloadDirectory='', askDownload=False, startup='restore',
                       homepage='https://www.apple.com/uk/', shortcuts={}, backdrop='none', interfaceLanguage='en',
                       translationTarget='en', translationOffers=False)  # no "Translate this page?" over the demo
    with sqlite3.connect(data / 'settings.sqlite3') as settings:
        settings.executescript('PRAGMA application_id=1380271184; PRAGMA user_version=1; '
                               'CREATE TABLE IF NOT EXISTS preferences(id INTEGER PRIMARY KEY CHECK(id=1),value TEXT NOT NULL);')
        settings.execute('INSERT OR REPLACE INTO preferences VALUES(1,?)', (json.dumps(preferences),))
    (data / 'window.json').write_text(json.dumps(dict(WINDOW, maximized=False)))


# --- Windows, input and capture -------------------------------------------------------------
class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        class MI(ctypes.Structure):
            _fields_ = [('dx', wt.LONG), ('dy', wt.LONG), ('mouseData', wt.DWORD), ('dwFlags', wt.DWORD), ('time', wt.DWORD), ('extra', ctypes.c_size_t)]
        class KI(ctypes.Structure):
            _fields_ = [('wVk', wt.WORD), ('wScan', wt.WORD), ('dwFlags', wt.DWORD), ('time', wt.DWORD), ('extra', ctypes.c_size_t)]
        _fields_ = [('mi', MI), ('ki', KI), ('pad', ctypes.c_byte * 32)]
    _anonymous_ = ('u',)
    _fields_ = [('type', wt.DWORD), ('u', _U)]


def _send(*inputs):
    array = (INPUT * len(inputs))(*inputs)
    user32.SendInput(len(inputs), array, ctypes.sizeof(INPUT))


def _mouse(flags, x=0, y=0, data=0):
    i = INPUT(type=0)
    i.mi.dx, i.mi.dy, i.mi.mouseData, i.mi.dwFlags = x, y, data, flags
    return i


def _abs(x, y):
    left, top = user32.GetSystemMetrics(76), user32.GetSystemMetrics(77)
    width, height = user32.GetSystemMetrics(78), user32.GetSystemMetrics(79)
    return int((x - left) * 65535 / (width - 1)), int((y - top) * 65535 / (height - 1))


def cursor():
    point = wt.POINT(); user32.GetCursorPos(ctypes.byref(point)); return point.x, point.y


def move(x, y, seconds=0.55):
    """Ease the real cursor to (x, y) like a hand would."""
    sx, sy = cursor(); steps = max(2, int(seconds * 90))
    for i in range(1, steps + 1):
        t = i / steps; e = 1 - (1 - t) ** 3 if t < 1 else 1
        _send(_mouse(0x8001 | 0x4000, *_abs(sx + (x - sx) * e, sy + (y - sy) * e)))
        time.sleep(seconds / steps)


SOL_PID = [0]  # the Sol process this run started; every click must land in one of its windows


class Abort(RuntimeError):
    pass


def _owner(x, y):
    hwnd = user32.WindowFromPoint(wt.POINT(int(x), int(y)))
    root = user32.GetAncestor(hwnd, 2) if hwnd else None  # GA_ROOT
    pid = wt.DWORD()
    if root: user32.GetWindowThreadProcessId(root, ctypes.byref(pid))
    return pid.value


def guard(x, y, foreground=True):
    """Refuse to press anything outside the Sol this run started (stray clicks hit the user's desktop)."""
    if not SOL_PID[0] or _owner(x, y) != SOL_PID[0]:
        raise Abort(f'Point {x},{y} is not over the recorded Sol window; stopping')
    if foreground:
        pid = wt.DWORD(); user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(pid))
        if pid.value != SOL_PID[0]:
            raise Abort('Sol is not in the foreground; stopping')


def click(x=None, y=None, seconds=0.55, pause=0.12, foreground=True):
    if x is not None: move(x, y, seconds)
    guard(*cursor(), foreground)
    _send(_mouse(0x0002)); time.sleep(0.07); _send(_mouse(0x0004)); time.sleep(pause)


def drag(x0, y0, x1, y1, seconds=0.9):
    move(x0, y0); guard(x0, y0); guard(x1, y1); _send(_mouse(0x0002)); time.sleep(0.15)
    move(x0 + (x1 - x0) * 0.08, y0 + (y1 - y0) * 0.08, 0.15)  # past the drag threshold
    move(x1, y1, seconds); time.sleep(0.35); _send(_mouse(0x0004)); time.sleep(0.2)


def drag_live(x0, y0, locate, seconds=1.0):
    """Drag to a target looked up after the drag starts: drag mode can reflow and scroll the view."""
    move(x0, y0); guard(x0, y0); _send(_mouse(0x0002)); time.sleep(0.15)
    move(x0 + 10, y0 + 6, 0.2); time.sleep(0.45)
    x1, y1 = locate(); guard(x1, y1); move(x1, y1, seconds); time.sleep(0.45)
    x2, y2 = locate()
    if abs(x2 - x1) + abs(y2 - y1) > 6: guard(x2, y2); move(x2, y2, 0.25); time.sleep(0.3)
    _send(_mouse(0x0004)); time.sleep(0.2)


def key(*vks, hold=0.06):
    guard(*cursor())
    for vk in vks: i = INPUT(type=1); i.ki.wVk = vk; _send(i)
    time.sleep(hold)
    for vk in reversed(vks): i = INPUT(type=1); i.ki.wVk = vk; i.ki.dwFlags = 2; _send(i)
    time.sleep(0.08)


def sol_window(pid):
    found = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(hwnd, _):
        owner = wt.DWORD(); user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        title = ctypes.create_unicode_buffer(64); user32.GetWindowTextW(hwnd, title, 64)
        if owner.value == pid and title.value == 'Sol' and user32.IsWindowVisible(hwnd): found.append(hwnd)
        return True
    user32.EnumWindows(each, 0)
    return found[0] if found else None


def window_rect(hwnd):
    rect = wt.RECT(); ctypes.windll.dwmapi.DwmGetWindowAttribute(hwnd, 9, ctypes.byref(rect), ctypes.sizeof(rect))  # extended frame
    return rect.left, rect.top, rect.right, rect.bottom


class CURSORINFO(ctypes.Structure):
    _fields_ = [('cbSize', wt.DWORD), ('flags', wt.DWORD), ('hCursor', wt.HANDLE), ('pt', wt.POINT)]


class ICONINFO(ctypes.Structure):
    _fields_ = [('fIcon', wt.BOOL), ('xHotspot', wt.DWORD), ('yHotspot', wt.DWORD), ('hbmMask', wt.HBITMAP), ('hbmColor', wt.HBITMAP)]


class Recorder:
    """Grabs a screen rectangle (the composited desktop, so animations are complete) with the cursor."""
    def __init__(self, rect, fps=30, cursor=True):
        self.rect, self.fps, self.frames, self.running, self.cursor = rect, fps, [], False, cursor

    def grab(self):
        left, top, right, bottom = self.rect; width, height = right - left, bottom - top
        screen = user32.GetDC(0); memory = gdi32.CreateCompatibleDC(screen)
        bitmap = gdi32.CreateCompatibleBitmap(screen, width, height); old = gdi32.SelectObject(memory, bitmap)
        gdi32.BitBlt(memory, 0, 0, width, height, screen, left, top, 0x00CC0020 | 0x40000000)  # SRCCOPY | CAPTUREBLT
        info = CURSORINFO(cbSize=ctypes.sizeof(CURSORINFO))
        if self.cursor and user32.GetCursorInfo(ctypes.byref(info)) and info.flags & 1:
            icon = ICONINFO(); user32.GetIconInfo(info.hCursor, ctypes.byref(icon))
            user32.DrawIconEx(memory, info.pt.x - left - icon.xHotspot, info.pt.y - top - icon.yHotspot, info.hCursor, 0, 0, 0, None, 3)
            if icon.hbmMask: gdi32.DeleteObject(icon.hbmMask)
            if icon.hbmColor: gdi32.DeleteObject(icon.hbmColor)
        header = (ctypes.c_uint32 * 10)(40, width, (-height) & 0xffffffff, 1 | (32 << 16), 0, 0, 0, 0, 0, 0)
        buffer = ctypes.create_string_buffer(width * height * 4)
        gdi32.GetDIBits(memory, bitmap, 0, height, buffer, header, 0)
        gdi32.SelectObject(memory, old); gdi32.DeleteObject(bitmap); gdi32.DeleteDC(memory); user32.ReleaseDC(0, screen)
        return Image.frombuffer('RGBA', (width, height), buffer, 'raw', 'BGRA', 0, 1).convert('RGB')

    def _run(self):
        period = 1 / self.fps; start = time.monotonic(); next_at = start
        self.errors = []
        while self.running:
            try:
                image = self.grab()
                scale = WIDTH / image.width
                self.frames.append((time.monotonic() - start, image.resize((WIDTH, round(image.height * scale)), Image.LANCZOS)))
            except Exception as error:  # one bad frame must not end the recording
                if not self.errors: print('capture error:', repr(error), flush=True)
                self.errors.append(repr(error))
            next_at += period
            time.sleep(max(0, next_at - time.monotonic()))

    def __enter__(self):
        self.running = True; self.thread = threading.Thread(target=self._run, daemon=True); self.thread.start(); return self

    def __exit__(self, *_):
        self.running = False; self.thread.join()


def save_webp(frames, target: Path, trim_start=0.0, hold_end=1.2):
    frames = [(t, f) for t, f in frames if t >= trim_start]
    images = [f for _, f in frames]
    durations = [max(20, int(round((frames[i + 1][0] - frames[i][0]) * 1000))) for i in range(len(frames) - 1)] + [int(hold_end * 1000)]
    images[0].save(target, save_all=True, append_images=images[1:], duration=durations, loop=0, quality=82, method=6)
    return dict(file=str(target.relative_to(ROOT)).replace('\\', '/'), frames=len(images), seconds=round(sum(durations) / 1000, 1),
                bytes=target.stat().st_size, size=list(images[0].size))


# --- UI Automation lookups ------------------------------------------------------------------
class UIA:
    def __init__(self):
        self.process = subprocess.Popen(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(ROOT / 'scripts/readme_uia.ps1')],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding='utf-8')
        json.loads(self.process.stdout.readline())

    def find(self, pid, name, type=None, window=None, timeout=20.0):
        deadline = time.monotonic() + timeout
        hwnd = sol_window(pid)
        within = list(window_rect(hwnd)) if hwnd and window in (None, 'Sol') else None  # only on-screen parts of this Sol
        while True:
            self.process.stdin.write(json.dumps(dict(pid=pid, name=name, type=type, window=window, within=within)) + '\n'); self.process.stdin.flush()
            answer = json.loads(self.process.stdout.readline())
            if answer.get('found'): return answer
            if time.monotonic() > deadline: raise RuntimeError(f'UI element not found: {name} ({type}) {answer.get("error", "")}')
            time.sleep(0.25)

    def center(self, *args, **kwargs):
        left, top, right, bottom = self.find(*args, **kwargs)['rect']
        return (left + right) // 2, (top + bottom) // 2

    def close(self):
        self.process.stdin.close(); self.process.wait(timeout=5)


# --- Running Sol ----------------------------------------------------------------------------
def launch(executable: Path, profile: Path, extra=()):
    process = subprocess.Popen([str(executable), '--workspace', f'--profile-dir={profile}', *extra], cwd=executable.parent)
    SOL_PID[0] = process.pid
    for _ in range(200):
        hwnd = sol_window(process.pid)
        if hwnd: return process, hwnd
        time.sleep(0.1)
    raise RuntimeError('Sol window did not appear')


def ready(uia, pid):
    """Chromium builds its accessibility tree on the first request; wait until the sidebar is in it."""
    uia.find(pid, '^Library$', 'Button', timeout=30)


def bring_forward(hwnd):
    for _ in range(30):  # the window may still be moving to its saved place
        left, top, right, bottom = window_rect(hwnd)
        if right - left >= WINDOW['width'] - 4: break
        time.sleep(0.2)
    # A real click on an empty part of the top bar gives Sol the foreground; only if Sol is there.
    click(right - 220, top + 14, seconds=0.2, foreground=False)  # empty strip left of the window buttons
    time.sleep(0.4)
    pid = wt.DWORD(); user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(pid))
    if pid.value != SOL_PID[0]: raise Abort('Sol did not come to the foreground; stopping')


def close(process):
    try:
        user32.PostMessageW(sol_window(process.pid), 0x0010, 0, 0)  # WM_CLOSE
        process.wait(timeout=15)
    except Exception:
        subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)


def scene_profile(name):
    directory = WORK / 'scenes' / f'{name}-{uuid.uuid4().hex[:6]}'
    shutil.copytree(BASE, directory / 'profile', ignore=shutil.ignore_patterns('Singleton*', 'lockfile', '*.lock'))
    write_settings(directory / 'profile' / 'BrowserData')
    return directory


def prepare(executable: Path):
    """Create the demo profile, seed it, and visit every page once so favicons are cached."""
    if BASE.exists(): shutil.rmtree(BASE)
    data = BASE / 'BrowserData'; data.mkdir(parents=True); (BASE / 'Default').mkdir()
    # Sites get English (Accept-Language), whatever the PC's language; Chromium keeps these prefs.
    (BASE / 'Default' / 'Preferences').write_text(json.dumps(
        {'intl': {'accept_languages': 'en-US,en', 'selected_languages': 'en-US,en'}}), encoding='utf-8')
    write_settings(data)
    process, hwnd = launch(executable, BASE)
    time.sleep(6); close(process)
    seed(data / 'workspace.sqlite3')
    process, hwnd = launch(executable, BASE)
    uia = UIA()
    try:
        ready(uia, process.pid); bring_forward(hwnd); time.sleep(3)
        scene = json.loads((WORK / 'scene.json').read_text())
        missed = []
        def visit(title):
            try:
                click(*uia.center(process.pid, f'^{title}$', 'Button', timeout=6), seconds=0.2); time.sleep(4)
            except RuntimeError:
                missed.append(title)
            key(0x1B)  # Esc: close anything a site or Sol opened over the page
        for space in SPACES:
            click(*uia.center(process.pid, f"^{space['name']}$", 'Button'), seconds=0.25); time.sleep(1.5)
            pinned, today = TREE[space['key']]
            for title in [t for e in pinned for t in ([c[0] for c in e[2]] if len(e) == 3 else [e[0]])] + [t for t, _ in today]:
                visit(title)
        for title, _ in FAVORITES:
            visit(title)
        click(*uia.center(process.pid, f"^{SPACES[0]['name']}$", 'Button'), seconds=0.25); time.sleep(2)
        if missed: print('Not visited:', ', '.join(missed))
    finally:
        uia.close(); close(process)
    print('Demo profile ready:', BASE)


# --- Scenes ---------------------------------------------------------------------------------
def load_spaces(uia, pid, finish='Work'):
    """Restored tabs start unloaded: open each Space's current tab so switching shows real pages."""
    for space in SPACES:
        click(*uia.center(pid, f"^{space['name']}$", 'Button'), seconds=0.25); time.sleep(1.2)
        first_today = TREE[space['key']][1][0][0]
        click(*uia.center(pid, f'^{first_today}$', 'Button'), seconds=0.25); time.sleep(3.5)
    click(*uia.center(pid, f'^{finish}$', 'Button'), seconds=0.25); time.sleep(2.5)


def scene_spaces(executable, uia):
    directory = scene_profile('spaces')
    process, hwnd = launch(executable, directory / 'profile')
    try:
        ready(uia, process.pid); bring_forward(hwnd); load_spaces(uia, process.pid)
        rect = window_rect(hwnd)
        studio = uia.center(process.pid, 'Studio', 'Button')
        travel = uia.center(process.pid, 'Travel', 'Button')
        work = uia.center(process.pid, 'Work', 'Button')
        move(rect[0] + 130, rect[1] + 560, 0.2)  # empty sidebar space: no link under the cursor
        with Recorder(rect) as recorder:
            time.sleep(0.8)
            click(*studio, seconds=0.8, pause=1.6)
            click(*travel, seconds=0.6, pause=1.6)
            click(*work, seconds=0.6, pause=1.4)
        return save_webp(recorder.frames, OUTPUT / 'spaces.webp')
    finally:
        close(process)


def rest(rect, seconds=0.6):
    """End a scene with the cursor on empty sidebar space, so no link preview shows at the bottom."""
    move(rect[0] + 130, rect[1] + 590, seconds)


def start_scene(executable, uia, name, load=True):
    directory = scene_profile(name)
    process, hwnd = launch(executable, directory / 'profile')
    ready(uia, process.pid); bring_forward(hwnd)
    if load: load_spaces(uia, process.pid)
    rect = window_rect(hwnd)
    move(rect[0] + 130, rect[1] + 560, 0.2)  # rest the cursor on empty sidebar space
    return process, hwnd, rect


def scene_split(executable, uia):
    process, hwnd, rect = start_scene(executable, uia, 'split')
    pid = process.pid
    try:
        sidebar_right = rect[0] + 264
        middle_y = (rect[1] + rect[3]) // 2
        web = uia.center(pid, '^Web browsers$', 'Button')
        with Recorder(rect) as recorder:
            time.sleep(0.8)
            drag(*web, rect[2] - 60, middle_y, seconds=1.1)          # to the page's right edge: two panes
            time.sleep(1.8)
            vision = uia.center(pid, '^Vision Pro$', 'Button')
            right_half = sidebar_right + (rect[2] - sidebar_right) * 3 // 4
            drag(*vision, right_half, rect[3] - 55, seconds=1.1)     # to the right pane's bottom edge: three panes
            rest(rect); time.sleep(1.8)
        return save_webp(recorder.frames, OUTPUT / 'split-view.webp')
    finally:
        close(process)


def scene_peek(executable, uia):
    process, hwnd, rect = start_scene(executable, uia, 'peek')
    pid = process.pid
    try:
        click(*uia.center(pid, '^Sol on GitHub$', 'Button'), seconds=0.25); time.sleep(5)
        link = uia.center(pid, '^sol-browser\\.app$', 'Hyperlink')
        move(rect[0] + 130, rect[1] + 560, 0.2)
        with Recorder(rect) as recorder:
            time.sleep(0.8)
            click(*link, seconds=0.9, pause=2.6)                        # the link opens in Link Peek
            split = uia.center(pid, 'split view', 'Button')
            click(*split, seconds=0.7, pause=0.6)                       # and joins the page in a split
            rest(rect); time.sleep(1.6)
        return save_webp(recorder.frames, OUTPUT / 'link-peek.webp')
    finally:
        close(process)


def orbit(points, seconds=1.6):
    """Press at points[0], glide the cursor through the rest, release (a curved drag)."""
    x0, y0 = points[0]; move(x0, y0)
    for x, y in points: guard(x, y)
    _send(_mouse(0x0002)); time.sleep(0.15)
    for x, y in points[1:]: move(x, y, seconds / (len(points) - 1))
    time.sleep(0.3); _send(_mouse(0x0004)); time.sleep(0.25)


def scene_theme(executable, uia):
    process, hwnd, rect = start_scene(executable, uia, 'theme')
    pid = process.pid
    try:
        edit = uia.center(pid, '^Edit space$', 'Button')
        with Recorder(rect) as recorder:
            time.sleep(0.8)
            click(*edit, seconds=0.8, pause=0.8)  # opens the space menu
            click(*uia.center(pid, '^Edit Theme', 'MenuItem'), seconds=0.5, pause=1.2)
            click(*uia.center(pid, '^3 colors$', 'Button'), seconds=0.7, pause=1.0)
            for preset in ('Rose', 'Tide'):
                click(*uia.center(pid, f'^{preset}$', 'Button'), seconds=0.55, pause=1.0)
            # The colour field sits between the count buttons (left/bottom) and the rotate dial (right/top).
            count = uia.find(pid, '^Number of colors$', 'Group')['rect']; dial = uia.find(pid, '^Rotate palette$', 'Slider')['rect']
            cx = (count[0] + dial[2] + 8) / 2; cy = (dial[1] - 6 + count[1] - 12) / 2
            x, y = uia.center(pid, r'^Primary color\.', 'Button')
            radius = min(cy - dial[1] + 6, cx - count[0]) * 0.62; start = math.atan2(y - cy, x - cx)
            orbit([(x, y)] + [(round(cx + radius * math.cos(start + a)), round(cy + radius * math.sin(start + a)))
                   for a in [i * math.radians(150) / 14 for i in range(15)]], seconds=2.2)
            time.sleep(0.6)
            left, top, right, bottom = uia.find(pid, '^Brightness$', 'Slider')['rect']
            y = (top + bottom) // 2
            drag(left + (right - left) * 55 // 100, y, left + (right - left) * 25 // 100, y, seconds=0.8)
            time.sleep(0.5)
            click(*uia.center(pid, '^Light$', 'Button'), seconds=0.6, pause=1.3)
            click(*uia.center(pid, '^Dark$', 'Button'), seconds=0.6, pause=1.3)
            click(*uia.center(pid, '^(Done|Save)$', 'Button'), seconds=0.6, pause=0.3)
            rest(rect); time.sleep(1.0)
        return save_webp(recorder.frames, OUTPUT / 'space-setup.webp')
    finally:
        close(process)


def scene_library(executable, uia):
    process, hwnd, rect = start_scene(executable, uia, 'library')
    pid = process.pid
    try:
        click(*uia.center(pid, '^Archive Web browsers$', 'Button'), seconds=0.4, pause=0.8)  # something for the Archive view
        rest(rect); time.sleep(0.6)
        with Recorder(rect) as recorder:
            time.sleep(0.8)
            click(*uia.center(pid, '^Library$', 'Button'), seconds=0.8, pause=1.6)
            click(*uia.center(pid, '^Spaces$', 'Button'), seconds=0.7, pause=1.4)
            def below_apple_music():
                left, top, right, bottom = uia.find(pid, '^Apple Music$', 'Button')['rect']
                return (left + right) // 2, bottom + 14
            drag_live(*uia.center(pid, '^Vision Pro$', 'Button'), below_apple_music, seconds=1.1); time.sleep(1.0)
            drag_live(*uia.center(pid, '^Kyoto$', 'Button'), lambda: uia.center(pid, '^Trips$', 'Button'), seconds=0.9); time.sleep(1.2)
            click(*uia.center(pid, '^Close Library$', 'Button'), seconds=0.8, pause=0.4)
            rest(rect); time.sleep(1.2)
        return save_webp(recorder.frames, OUTPUT / 'library.webp')
    finally:
        close(process)


COVER_WINDOW = dict(x=120, y=90, width=1440, height=940)  # larger than the animations: the cover is a 1600 px still


def scene_cover(executable, uia):
    """Full-resolution stills for the README cover: Studio in dark, Work switched to light."""
    directory = scene_profile('cover')
    data = directory / 'profile' / 'BrowserData'
    (data / 'window.json').write_text(json.dumps(dict(COVER_WINDOW, maximized=False)))
    with sqlite3.connect(data / 'workspace.sqlite3') as db:
        db.execute("UPDATE spaces SET theme_mode=0,theme_intensity=100 WHERE name='Work'")
    target = WORK / 'cover'; target.mkdir(parents=True, exist_ok=True)
    process, hwnd = launch(executable, directory / 'profile')
    pid = process.pid
    try:
        ready(uia, pid); bring_forward(hwnd); load_spaces(uia, pid)
        rect = window_rect(hwnd); camera = Recorder(rect, cursor=False)
        def shot(name):
            move(rect[2] + 60, rect[1] + 400, 0.3); time.sleep(1.5)  # off the window: no hover states
            camera.grab().save(target / f'{name}.png')
        click(*uia.center(pid, '^Apple$', 'Button'), seconds=0.3); time.sleep(4); shot('light')
        click(*uia.center(pid, '^Studio$', 'Button'), seconds=0.3); time.sleep(2.5); shot('dark')
        from build_readme_media import framed  # the README's frame: gradient background, shadow, rounded corners
        framed(Image.open(target / 'light.png')).save(OUTPUT / 'overview.webp', quality=94, method=6)
        framed(Image.open(target / 'dark.png'), dark=True).save(OUTPUT / 'cover-dark.webp', quality=94, method=6)
        return {name: (OUTPUT / name).stat().st_size for name in ('overview.webp', 'cover-dark.webp')}
    finally:
        close(process)


SCENES = {'spaces': scene_spaces, 'split': scene_split, 'peek': scene_peek, 'theme': scene_theme, 'library': scene_library, 'cover': scene_cover}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('scenes', nargs='*', choices=list(SCENES))
    parser.add_argument('--exe', type=Path, default=ROOT / 'build/Release/apps/desktop/Release/sol.exe')
    parser.add_argument('--prepare', action='store_true')
    args = parser.parse_args()
    executable = args.exe.resolve(); WORK.mkdir(parents=True, exist_ok=True)
    if args.prepare: prepare(executable)
    uia = UIA()
    try:
        results = {name: SCENES[name](executable, uia) for name in args.scenes}
    finally:
        uia.close()
    if results:
        (WORK / 'record.json').write_text(json.dumps(results, indent=2))
        print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
