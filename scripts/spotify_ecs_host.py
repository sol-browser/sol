"""Windows-only half of the ECS probe. Both participating processes are disposable."""
import ctypes
from ctypes import wintypes as wt
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import secrets
import subprocess
from threading import Thread
import time

from page_keys_smoke import EXE, chord, click, keys, sol_window, user32, write_settings
from spotify_ecs_probe import events


def until(description, probe, seconds=12):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        value = probe()
        if value:
            return value
        time.sleep(.08)
    raise RuntimeError('Timeout waiting for ' + description)


def rect(hwnd, client=False):
    value = wt.RECT()
    func = user32.GetClientRect if client else user32.GetWindowRect
    if not func(hwnd, ctypes.byref(value)):
        raise ctypes.WinError()
    return value.left, value.top, value.right - value.left, value.bottom - value.top


def owner_pid(hwnd):
    pid = wt.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return pid.value


class GuiThreadInfo(ctypes.Structure):
    _fields_ = [('size', wt.DWORD), ('flags', wt.DWORD), ('active', wt.HWND),
                ('focus', wt.HWND), ('capture', wt.HWND), ('menu_owner', wt.HWND),
                ('move_size', wt.HWND), ('caret', wt.HWND), ('caret_rect', wt.RECT)]


def focus_clicked_surface(root, child, pid):
    if user32.GetForegroundWindow() != root or not user32.IsWindowVisible(child):
        return
    point = wt.POINT()
    if not user32.GetCursorPos(ctypes.byref(point)):
        return
    target = user32.WindowFromPoint(point)
    if not user32.IsChild(child, target) or owner_pid(target) != pid:
        return
    focus = GuiThreadInfo()
    focus.size = ctypes.sizeof(focus)
    if user32.GetGUIThreadInfo(0, ctypes.byref(focus)) and focus.focus == target:
        return
    # The reparented Chrome renderer accepts mouse input before it becomes
    # the native keyboard target. Attach only long enough to focus the
    # verified HWND in response to its own mouse-down.
    caller = ctypes.windll.kernel32.GetCurrentThreadId()
    thread = user32.GetWindowThreadProcessId(target, None)
    if not user32.AttachThreadInput(caller, thread, True):
        return
    try:
        if user32.GetForegroundWindow() == root and user32.WindowFromPoint(point) == target:
            user32.SetFocus(target)
    finally:
        user32.AttachThreadInput(caller, thread, False)


def find_slot(root, pid, size):
    candidates = []
    callback_type = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    @callback_type
    def collect(hwnd, _):
        if (user32.GetParent(hwnd) == root and owner_pid(hwnd) == pid and
                user32.IsWindowVisible(hwnd) and rect(hwnd, client=True)[2:] == size):
            candidates.append(hwnd)
        return True

    user32.EnumChildWindows(root, collect, 0)
    return candidates[0] if len(candidates) == 1 else None


def exercise_host(directory, electron, live=False, exercise_playback=False):
    sizes = {}
    slot_path = '/spotify-tab-' + secrets.token_hex(16) if live else '/slot'
    origin = None
    allowed_origins = {'https://open.spotify.com', 'https://accounts.spotify.com'}

    class Page(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            if live and self.headers.get('Host') != f'127.0.0.1:{self.server.server_port}':
                self.send_error(421)
                return
            pieces = self.path.split('/')
            if len(pieces) == 5 and pieces[1] == 'size' and all(p.isdecimal() for p in pieces[3:]):
                if pieces[2] not in (slot_path[1:], 'other'):
                    self.send_error(404)
                    return
                sizes[pieces[2]] = tuple(map(int, pieces[3:]))
                self.send_response(204)
                self.end_headers()
                return
            if live and self.path == slot_path + '/origin':
                body = (origin or 'Waiting for verified Spotify page').encode()
                self.send_response(200)
                self.send_header('Cache-Control', 'no-store')
                self.send_header('Content-Type', 'text/plain')
                self.end_headers()
                self.wfile.write(body)
                return
            if self.path not in (slot_path, '/other'):
                self.send_error(404)
                return
            body = b"""<!doctype html><title>Sol host fixture</title><p>CEF tab</p><script>
              function report() { fetch('/size'+location.pathname+'/'+Math.round(innerWidth*devicePixelRatio)
                +'/'+Math.round(innerHeight*devicePixelRatio)) }
              report(); addEventListener('resize', report);
            </script>"""
            if live and self.path == slot_path:
                body = b"""<!doctype html><meta charset="utf-8"><title>Spotify in Sol</title>
                  <style>body{margin:0;color:#fff;background:#1c1a24;font:13px system-ui}
                  header{height:34px;line-height:34px;padding:0 12px;white-space:nowrap}
                  strong{margin-right:10px}</style>
                  <header><strong>Spotify helper</strong><span id="committed">Waiting for verified Spotify page</span></header>
                  <script>
                    function report() { fetch('/size'+location.pathname+'/'+Math.round(innerWidth*devicePixelRatio)
                      +'/'+Math.round(innerHeight*devicePixelRatio)) }
                    async function committed() {
                      const response = await fetch(location.pathname + '/origin');
                      document.getElementById('committed').textContent = await response.text();
                    }
                    report(); addEventListener('resize', report);
                    committed(); setInterval(committed, 300);
                  </script>"""
            self.send_response(200)
            self.send_header('Content-Type', 'text/html')
            self.send_header('Referrer-Policy', 'no-referrer')
            self.send_header('Content-Security-Policy', "default-src 'none'; script-src 'unsafe-inline'; "
                             "style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'")
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = ThreadingHTTPServer(('127.0.0.1', 0), Page)
    Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    profile = directory / 'sol-profile'
    (profile / 'Default').mkdir(parents=True)
    write_settings(profile, {})
    command = [str(EXE), '--workspace', f'--profile-dir={profile}']
    if live:
        command.append(f'--probe-spotify-slot={base}{slot_path}')
    sol = None
    root = child = None
    evidence = {}

    def strip_height():
        return round(34 * user32.GetDpiForWindow(slot) / 96) if live else 0

    def fit():
        _, _, width, height = rect(slot, client=True)
        offset = strip_height()
        flags = 0x50 if live else 0x70  # Show without activation. The fixture also recalculates its frame.
        if not user32.SetWindowPos(child, 0, 0, offset, width, max(1, height - offset), flags):
            raise ctypes.WinError()
        x, y, width, height = rect(slot)
        return rect(child) == (x, y + offset, width, height - offset)

    try:
        def child_handle():
            if electron.poll() is not None:
                if electron.returncode == 4:
                    raise RuntimeError('The selected ECS profile is already open in another helper')
                raise RuntimeError(f'Electron exited before creating its window ({electron.returncode})')
            return next((int(row['hwnd']) for row in events(directory) if row['event'] == 'host_ready'), None)
        child = until('Electron fixture', child_handle)
        if owner_pid(child) != electron.pid:
            raise RuntimeError('Electron HWND owner mismatch')
        initial_url = 'https://open.spotify.com/' if live else base + slot_path
        starting_command = command + ([f'--probe-report={directory / "sol.jsonl"}'] if live else [])
        sol = subprocess.Popen(starting_command + [initial_url], cwd=EXE.parent)
        def sol_handle():
            if sol.poll() is not None:
                raise RuntimeError(f'Sol exited before creating its window ({sol.returncode})')
            return sol_window(sol.pid)

        root = until('disposable Sol window', sol_handle, 25)
        slot = until('unique Sol page matching the fixture viewport',
                     lambda: find_slot(root, sol.pid, sizes.get(slot_path[1:])))
        # Reparent only our frameless ECS fixture. Never move a CEF window.
        style = user32.GetWindowLongW(child, -16)
        user32.SetWindowLongW(child, -16, (style & ~0x80cf0000) | 0x40000000)
        user32.SetParent(child, slot)
        evidence['parent_is_sol_tab'] = user32.GetParent(child) == slot
        if not evidence['parent_is_sol_tab']:
            raise RuntimeError('SetParent failed')
        (directory / 'attached').touch()
        until('Electron show acknowledgement', lambda: any(r['event'] == 'shown' for r in events(directory)))
        evidence['initial_bounds'] = fit()
        if live:
            handoff_command = command + [f'--probe-report={directory / "handoff.jsonl"}']
            launch = subprocess.run(handoff_command + [base + '/other'], cwd=EXE.parent, timeout=12)
            if launch.returncode not in (0, 24):
                raise RuntimeError(f'Sol URL handoff failed ({launch.returncode})')
            until('adjacent CEF tab', lambda: 'other' in sizes and not user32.IsWindowVisible(child))
            evidence['hidden_on_switch'] = True
            evidence['committed_origins'] = []
            launch = subprocess.run(handoff_command + [initial_url], cwd=EXE.parent, timeout=12)
            if launch.returncode not in (0, 24):
                raise RuntimeError(f'Sol Spotify return failed ({launch.returncode})')
            until('return to the signed Spotify tab', lambda: user32.IsWindowVisible(child))
            print('Spotify is open in Sol. Close the Spotify tab to finish.', flush=True)
            if exercise_playback:
                print('Start a full track from the beginning. The probe will switch tabs and monitor its progress.',
                      flush=True)
            cursor = 0
            last_size = rect(slot, client=True)[2:]
            last_visible = False
            audible_since = None
            background_since = None
            playback_phase = 'starting'
            playback_deadline = time.monotonic() + 480
            progress = None
            while sol.poll() is None and electron.poll() is None and user32.IsWindow(slot):
                rows = events(directory)
                for row in rows[cursor:]:
                    if row['event'] == 'committed_origin':
                        origin = row['origin'] if row['origin'] in allowed_origins else None
                        if origin and origin not in evidence['committed_origins']:
                            evidence['committed_origins'].append(origin)
                    elif row['event'] == 'tab_shortcut':
                        if row['action'] in ('next', 'previous', 'close') and user32.GetForegroundWindow() == root:
                            action = subprocess.run(handoff_command + ['--probe-spotify-action=' + row['action']],
                                                    cwd=EXE.parent, timeout=8)
                            evidence.setdefault('helper_shortcuts', []).append({
                                'action': row['action'], 'handoff': action.returncode in (0, 24)})
                    elif row['event'] == 'page_key':
                        evidence['helper_received_key'] = True
                    elif row['event'] == 'surface_mouse_down' and time.time() * 1000 - row['time'] < 200:
                        focus_clicked_surface(root, child, electron.pid)
                    elif row['event'] == 'spotify_preflight':
                        evidence['embedded_spotify_media_keys'] = row['mediaKeys']
                        evidence['desktop_player'] = row['bundle'] == 'web-player'
                    elif row['event'] == 'player_state':
                        if sum(row['clicks'].values()):
                            evidence['spotify_received_click'] = True
                        if row.get('input', {}).get('text'):
                            evidence['spotify_search_received_text'] = True
                        if row.get('input', {}).get('ended'):
                            evidence['spotify_media_ended'] = True
                        sample = row.get('playback')
                        if sample:
                            duration = sample['durationSeconds']
                            position = sample['positionSeconds']
                            if progress and (duration != progress['duration'] or position < progress['last'] - 3):
                                if progress['start'] <= 3 and progress['last'] >= progress['duration'] - 4:
                                    evidence['full_track_seconds'] = progress['duration']
                                progress = None
                            if not progress:
                                progress = {'duration': duration, 'start': position, 'last': position}
                            elif position - progress['last'] > 8:
                                progress = {'duration': duration, 'start': position, 'last': position}
                            else:
                                progress['last'] = position
                        elif progress and evidence.get('spotify_media_ended'):
                            if progress['start'] <= 3 and progress['last'] >= progress['duration'] - 4:
                                evidence['full_track_seconds'] = progress['duration']
                    elif row['event'] == 'spotify_drm_request' and row['method'] == 'POST':
                        evidence.setdefault('licence_post_statuses', []).append(row['status'])
                    elif row['event'] == 'audio':
                        if row['audible']:
                            evidence['audible'] = True
                            audible_since = row['time']
                        elif audible_since is not None:
                            evidence.setdefault('audible_intervals_ms', []).append(row['time'] - audible_since)
                            audible_since = None
                cursor = len(rows)
                visible = bool(user32.IsWindowVisible(slot))
                size = rect(slot, client=True)[2:]
                if visible and (not last_visible or size != last_size):
                    resized = fit()
                    if not last_visible:
                        evidence['restored_on_switch'] = resized
                    if size != last_size:
                        evidence['resized_bounds'] = resized
                    last_size = size
                now = time.time()
                if not visible and audible_since is not None:
                    if background_since is None:
                        background_since = now
                    evidence['longest_background_audible_seconds'] = max(
                        evidence.get('longest_background_audible_seconds', 0), round(now - background_since, 2))
                else:
                    background_since = None
                if exercise_playback:
                    if time.monotonic() >= playback_deadline:
                        raise RuntimeError('Full signed playback was not observed before the test deadline')
                    if playback_phase == 'starting' and audible_since and now * 1000 - audible_since > 6000:
                        launch = subprocess.run(handoff_command + [base + '/other'], cwd=EXE.parent, timeout=10)
                        if launch.returncode not in (0, 24):
                            raise RuntimeError(f'Could not switch playback into the background ({launch.returncode})')
                        playback_phase = 'background'
                    elif playback_phase == 'background' and background_since and now - background_since > 6:
                        launch = subprocess.run(handoff_command + [initial_url], cwd=EXE.parent, timeout=10)
                        if launch.returncode not in (0, 24):
                            raise RuntimeError(f'Could not return to the playing Spotify tab ({launch.returncode})')
                        playback_phase = 'returning'
                    elif playback_phase == 'returning' and visible and audible_since:
                        evidence['audible_after_return'] = True
                        playback_phase = 'returned'
                    elif (playback_phase == 'returned' and evidence.get('full_track_seconds') and
                          200 in evidence.get('licence_post_statuses', [])):
                        evidence['automated_playback_passed'] = True
                        break
                last_visible = visible
                time.sleep(.08)
            evidence['slot_closed'] = not bool(user32.IsWindow(slot))
            if audible_since is not None:
                evidence['audible_at_last_sample_ms'] = round(time.time() * 1000) - audible_since
            if exercise_playback and not evidence.get('automated_playback_passed'):
                raise RuntimeError('Signed Spotify playback ended before completing the test')
            return
        x, y, w, h = rect(root)
        old_size = rect(slot)[2:]
        user32.ShowWindow(root, 9)  # Restore before explicit resize.
        user32.SetWindowPos(root, 0, x, y, max(680, w - 173), max(520, h - 97), 0x14)
        until('Sol content resize', lambda: rect(slot)[2:] != old_size)
        evidence['resized_bounds'] = fit()

        # Test keyboard delivery while the local fixture owns the surface.
        def target_point():
            # CEF can raise its own renderer during a tab reveal. The caller
            # explicitly restores the ECS child's stacking and latest bounds.
            fit()
            x, y, w, h = rect(child)
            point = wt.POINT(x + w // 2, y + h // 2)
            target = user32.WindowFromPoint(point)
            return point if target == child or user32.IsChild(child, target) else None

        user32.SetForegroundWindow(root)
        time.sleep(.3)
        # A physical click may activate the test window even if foreground
        # policy refused SetForegroundWindow. Only click if Windows reports
        # that the exact screen point belongs to the owned Electron subtree.
        point = target_point()
        if not point and user32.GetForegroundWindow() == root:
            point = until('clickable local input fixture', target_point, 5)
        if point:
            click(point.x, point.y)
            if user32.GetForegroundWindow() in (root, child):
                keys((0x4A, False), (0x4A, True))  # Ordinary text into the fixture input.
                time.sleep(.35)
                seen = {r['event'] for r in events(directory)}
                evidence['electron_received_click'] = 'fixture-click' in seen
                evidence['electron_received_key'] = 'fixture-key' in seen

        (directory / 'load-spotify').touch()

        def loaded():
            if electron.poll() is not None:
                raise RuntimeError('Electron exited before the embedded Spotify check')
            return next((r for r in events(directory) if r['event'] == 'spotify_preflight'), None)

        result = until('Spotify MediaKeys in attached Electron window', loaded, 90)
        evidence['embedded_spotify_media_keys'] = result['mediaKeys']
        evidence['desktop_player'] = result['bundle'] == 'web-player'
        evidence['spotify_visible'] = bool(user32.IsWindowVisible(child)) and rect(child) == rect(slot)

        # Opening the adjacent tab uses Sol's normal external URL entry point.
        launch = subprocess.run(command + [base + '/other'], cwd=EXE.parent, timeout=12)
        # Chromium returns RESULT_CODE_NORMAL_EXIT_PROCESS_NOTIFIED on handoff.
        if launch.returncode not in (0, 24):
            raise RuntimeError(f'Sol URL handoff failed ({launch.returncode})')
        until('adjacent CEF tab', lambda: 'other' in sizes and not user32.IsWindowVisible(child))
        evidence['hidden_on_switch'] = True
        user32.SetForegroundWindow(root)
        time.sleep(.3)
        if user32.GetForegroundWindow() == root:
            chord(0x09)
            until('return to ECS tab', lambda: user32.IsWindowVisible(child), 5)
            evidence['restored_on_switch'] = True
            # Sol makes the incoming CEF tab visible off-screen until its first
            # frame is ready. Wait for the final on-screen placement.
            point = until('clickable Electron surface', target_point, 5)
            evidence['hit_test'] = True
            if user32.GetForegroundWindow() == root:
                chord(0x09)
                time.sleep(.8)
                evidence['ctrl_tab_after_return'] = not bool(user32.IsWindowVisible(child))
        else:
            evidence['foreground_unavailable'] = True
    finally:
        if child and owner_pid(child) == electron.pid:
            user32.PostMessageW(child, 0x10, 0, 0)
            try:
                electron.wait(timeout=5)
            except subprocess.TimeoutExpired:
                electron.kill()
                electron.wait()
        if sol:
            if root and owner_pid(root) == sol.pid:
                user32.PostMessageW(root, 0x10, 0, 0)
            try:
                sol.wait(timeout=6)
            except subprocess.TimeoutExpired:
                sol.kill()
                sol.wait()
        server.shutdown()
        server.server_close()
        evidence['electron_exit'] = electron.poll()
        evidence['sol_exit'] = sol and sol.poll()
        (directory / 'host.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
        print(json.dumps(evidence, indent=2), flush=True)
    if not all(evidence.get(key) for key in ('initial_bounds', 'resized_bounds', 'hidden_on_switch',
                                              'spotify_visible', 'embedded_spotify_media_keys', 'desktop_player',
                                              'electron_received_click', 'electron_received_key',
                                              'restored_on_switch', 'ctrl_tab_after_return')):
        raise RuntimeError('Partial host result. See host.json for the individual checks.')
