"""Exercise physical input in a disposable signed Spotify tab on Windows."""
import argparse
import ctypes
from ctypes import wintypes as wt
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import time

from page_keys_smoke import EXE, chord, click, keys, user32
from spotify_ecs_host import owner_pid, rect, until
from spotify_ecs_probe import ROOT, events


def exercise(helper):
    runner = subprocess.Popen(
        [sys.executable, str(ROOT / 'scripts/spotify_ecs_probe.py'), 'tab', '--electron', str(helper)],
        cwd=ROOT, stdout=subprocess.PIPE, text=True, env={**os.environ, 'SOL_ECS_SMOKE_SEARCH': '1'})
    root = root_pid = None
    initial_cursor = wt.POINT()
    user32.GetCursorPos(ctypes.byref(initial_cursor))
    try:
        directory_line = runner.stdout.readline().strip()
        if not directory_line:
            raise RuntimeError('Signed tab controller failed to start')
        directory = Path(directory_line)
        print(directory, flush=True)
        if 'Spotify is open in Sol' not in runner.stdout.readline():
            raise RuntimeError('Signed tab controller failed to attach')
        child = int(next(row['hwnd'] for row in events(directory) if row['event'] == 'host_ready'))
        root = user32.GetAncestor(child, 2)  # GA_ROOT
        root_pid = owner_pid(root)
        profile = directory / 'sol-profile'
        with sqlite3.connect((profile / 'BrowserData/workspace.sqlite3').as_uri() + '?mode=ro', uri=True) as db:
            slot, = db.execute("SELECT url FROM nodes WHERE title='Spotify'").fetchone()
        command = [str(EXE), '--workspace', f'--profile-dir={profile}', f'--probe-spotify-slot={slot}',
                   f'--probe-report={directory / "exercise-handoff.jsonl"}']
        launch = subprocess.run(command + ['https://open.spotify.com/'], cwd=EXE.parent, timeout=10)
        if launch.returncode not in (0, 24):
            raise RuntimeError(f'Could not open the real Spotify address ({launch.returncode})')
        until('visible Spotify tab', lambda: user32.IsWindowVisible(child))
        until('Spotify desktop controls', lambda: any(row['event'] == 'player_state' and row['desktopControls']
              for row in events(directory)), 90)

        def focused_click(search_sample=None):
            def hit_test():
                x, y, width, height = rect(child)
                if search_sample:
                    search = search_sample['searchPoint']
                    point = wt.POINT(x + round(width * search['x'] / search_sample['width']),
                                     y + round(height * search['y'] / search_sample['height']))
                    return point if user32.IsChild(child, user32.WindowFromPoint(point)) else None
                for column, row in ((3, 3), (3, 2), (2, 3), (2, 2), (1, 3)):
                    point = wt.POINT(x + width * column // 4, y + height * row // 4)
                    if user32.IsChild(child, user32.WindowFromPoint(point)):
                        return point
                return None

            point = until('foreground and hit-tested Spotify click', hit_test, 5)
            # The focus bridge confirms the cursor still hits the helper when
            # it handles the genuine mouse-down.
            user32.SetCursorPos(point.x, point.y)
            click(point.x, point.y)
            time.sleep(.25)

        def press(*key_events):
            if user32.GetForegroundWindow() != root:
                raise RuntimeError('Foreground unavailable for Spotify input')
            keys(*key_events)

        def shortcut(key):
            if user32.GetForegroundWindow() != root:
                raise RuntimeError('Foreground unavailable for a Sol shortcut')
            chord(key)

        user32.SetForegroundWindow(root)
        user32.SetWindowPos(root, 0, 0, 0, 0, 0, 0x13)  # Top of the ordinary window stack, without activation.
        time.sleep(.4)
        search = until('Spotify search field', lambda: next((
            row for row in reversed(events(directory)) if row.get('searchPoint')), None), 8)
        focused_click(search)
        focused_click(search)
        press((0x4A, False), (0x4A, True))  # Ordinary J key, never a real account field.
        until('physical text entry in Spotify search', lambda: any(
            row.get('input', {}).get('text') for row in events(directory)), 5)
        shortcut(0x09)  # Ctrl+Tab from the physically focused helper.
        until('ordinary CEF tab after Ctrl+Tab', lambda: not user32.IsWindowVisible(child), 5)
        shortcut(0x09)
        until('Spotify after Ctrl+Tab from CEF', lambda: user32.IsWindowVisible(child), 5)
        time.sleep(.7)
        x, y, width, height = rect(root)
        old_size = rect(child)[2:]
        user32.ShowWindow(root, 9)
        user32.SetWindowPos(root, 0, x, y, max(width - 80, 680), max(height - 60, 520), 0x14)
        until('resized Spotify view', lambda: rect(child)[2:] != old_size, 5)
        focused_click()
        before = sum(row['event'] == 'page_key' for row in events(directory))
        press((0x4A, False), (0x4A, True))
        until('restored keyboard input in the Spotify helper', lambda: sum(
            row['event'] == 'page_key' for row in events(directory)) > before, 5)
        # The CEF-owned origin strip can take keyboard focus without entering
        # the helper. The app's ordinary Ctrl+W must close the mixed tab.
        x, y, _, _ = rect(child)
        origin_point = wt.POINT(x + 40, y - 8)
        origin_target = user32.WindowFromPoint(origin_point)
        if not user32.IsChild(root, origin_target) or user32.IsChild(child, origin_target):
            raise RuntimeError('Could not hit-test the Sol-owned origin strip')
        click(origin_point.x, origin_point.y)
        shortcut(0x57)
        _, _ = runner.communicate(timeout=20)
        report = json.loads((directory / 'host.json').read_text(encoding='utf-8'))
        handoffs = {entry['action'] for entry in report.get('helper_shortcuts', []) if entry['handoff']}
        required = ('spotify_received_click', 'helper_received_key', 'spotify_search_received_text',
                    'restored_on_switch', 'resized_bounds', 'slot_closed', 'embedded_spotify_media_keys',
                    'desktop_player')
        if (runner.returncode != 0 or report['electron_exit'] != 0 or report['sol_exit'] != 0
                or 'next' not in handoffs or not all(report.get(key) for key in required)):
            raise RuntimeError('Partial Spotify tab result. See host.json for the individual checks.')
        print('Passed signed Spotify physical input, tab switching, resize and shutdown.')
    finally:
        user32.SetCursorPos(initial_cursor.x, initial_cursor.y)
        if runner.poll() is None and root and owner_pid(root) == root_pid:
            user32.PostMessageW(root, 0x10, 0, 0)
        if runner.poll() is None:
            runner.communicate(timeout=110)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--electron', type=Path, required=True)
    exercise(parser.parse_args().electron.resolve())
