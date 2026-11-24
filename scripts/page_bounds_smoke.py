"""Sol's layout owns every tab's native window: nothing else may move or resize it.

Real CEF run in workspace mode (the --probe-extension-popup sequence: /first,
/second, /third, back to /second, then the fixture's toolbar popup).

- Website: each page calls window.resizeTo(500, 600) and moveTo(0, 0) once it is
  visible; the tab must stay in place. CEF makes every hosted page a TYPE_POPUP
  Browser, which Chrome allows to move itself; measured on engine 158.1.41 the
  request moves nothing (guard, 2026-11-24).
- Extension: the owned popout-probe fixture pops out like Bitwarden
  (windows.create type popup, then windows.update(width 480) from the pop-out).
  Sol adopts the pop-out as a tab. Chrome then applies windows.update to the
  tab's own window through BrowserView::SetBounds, which pinned CEF offers no
  callback for (docs/cef-patches.md, "Extensions move Sol's page windows").
  Reported always; required only with --require-engine-bounds.

A watcher records every visible child window of the Sol window every 100 ms.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import shutil
import struct
import threading
import time
import uuid
import zlib
from smoke import ROOT, run

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
PAGE = """<!doctype html><html lang='en'><title>{path}</title><body style='background:#ffd'><h1>{path}</h1>
<script>
let done = false;
function go() {{
  if (done || document.visibilityState !== 'visible') return;
  done = true;
  setTimeout(() => {{ window.resizeTo(500, 600); window.moveTo(0, 0); }}, 300);
}}
document.addEventListener('visibilitychange', go); go();
</script>"""


def png(size: int) -> bytes:
    row = b'\x00' + b'\xd9\x43\x5f\xff' * size
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(row * size)) + chunk(b'IEND', b''))


def image_path(pid: int) -> str:
    handle = kernel32.OpenProcess(0x1000, False, pid)
    if not handle: return ''
    try:
        buffer = ctypes.create_unicode_buffer(1024); size = wt.DWORD(1024)
        return buffer.value if kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)) else ''
    finally: kernel32.CloseHandle(handle)


def page_windows(executable: str) -> list[dict]:
    """Visible Chrome widget children of each visible top-level 'Sol' window of this executable."""
    found = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(top, _):
        if not user32.IsWindowVisible(top): return True
        pid = wt.DWORD(); user32.GetWindowThreadProcessId(top, ctypes.byref(pid))
        title = ctypes.create_unicode_buffer(64); user32.GetWindowTextW(top, title, 64)
        if title.value != 'Sol' or image_path(pid.value).lower() != executable: return True
        frame = wt.RECT(); user32.GetWindowRect(top, ctypes.byref(frame))
        @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
        def child(handle, _):
            name = ctypes.create_unicode_buffer(64); user32.GetClassNameW(handle, name, 64)
            if user32.GetParent(handle) == top and user32.IsWindowVisible(handle) and name.value == 'Chrome_WidgetWin_1':
                rect = wt.RECT(); user32.GetWindowRect(handle, ctypes.byref(rect))
                found.append({'frame': [frame.left, frame.top, frame.right, frame.bottom],
                              'page': [rect.left, rect.top, rect.right, rect.bottom]})
            return True
        user32.EnumChildWindows(top, child, 0)
        return True
    user32.EnumWindows(each, 0)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--require-engine-bounds', action='store_true',
                        help='Also fail when an extension windows.update moves the tab (needs the engine patch)')
    args = parser.parse_args()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            body = PAGE.format(path=self.path).encode()
            self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    extension = ROOT / '.cache' / ('popout-probe-' + uuid.uuid4().hex[:8])
    shutil.copytree(ROOT / 'tests/extensions/popout-probe', extension)
    for size in (16, 48): (extension / f'icon{size}.png').write_bytes(png(size))
    executable = str(os.path.abspath(os.environ.get('RED_BROWSER_TEST_EXE',
                                                    ROOT / 'build/Release/apps/desktop/Release/sol.exe'))).lower()
    stop = threading.Event(); samples = []
    def watch():
        start = time.monotonic(); last = None
        while not stop.is_set():
            current = page_windows(executable)
            if current != last: samples.append({'t': round(time.monotonic() - start, 2), 'windows': current}); last = current
            time.sleep(0.1)
    watcher = threading.Thread(target=watch, daemon=True); watcher.start()
    try:
        directory, rows = run(f'http://127.0.0.1:{server.server_port}/first',
                              ['--probe-extension-popup', f'--load-extension={extension}'], allow_failure=True)
    finally:
        stop.set(); watcher.join()
        server.shutdown(); server.server_close()
        shutil.rmtree(extension, ignore_errors=True)
    def details(event): return [r.get('details', {}) for r in rows if r['event'] == event]
    # A page placed by Sol keeps its content-area size; a moved one has the requested size.
    def sized(width, height=None):
        return [s['t'] for s in samples for w in s['windows']
                if w['page'][2] - w['page'][0] == width and (height is None or w['page'][3] - w['page'][1] == height)]
    adopted = [d for d in details('foreign_browser_resolved')
               if d.get('outcome') == 'adopted' and 'uilocation=popout' in d.get('url', '')]
    website_moved = sized(500, 600)
    extension_moved = sized(480)
    checks = {
        'website_resize_left_tab_in_place': not website_moved,
        'popout_adopted_as_tab': len(adopted) > 0,
    }
    # Sol's own probe checks include the native input area, which fails while the
    # tab is out of place: required together with the engine checks.
    engine = {'extension_window_update_left_tab_in_place': not extension_moved,
              'probe_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows)}
    if args.require_engine_bounds: checks.update(engine)
    result = {'passed': all(checks.values()), 'checks': checks, 'engine': engine,
              'website_moved_at_s': website_moved[:5], 'extension_moved_at_s': extension_moved[:5]}
    (directory / 'page_windows.json').write_text(json.dumps(samples, indent=1) + '\n')
    (directory / 'page_bounds.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    if not result['passed']: raise RuntimeError('Page bounds smoke failed')


if __name__ == '__main__': main()
