"""Page and browser fullscreen acceptance through actual CEF in workspace mode.

The page enters fullscreen through the Fullscreen API (user gesture), native
code must make the window fullscreen and give the page the whole client area
without the rounded clip. Escape must restore the window and page bounds.
The browser then enters fullscreen through the normal F11 shortcut action.
Both routes must paint the same localized hint without stealing focus.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import sqlite3
import threading
from uuid import uuid4
from smoke import ROOT, run

PAGE = b"<!doctype html><html lang='en'><title>Fullscreen fixture</title><body style='margin:0;background:#123'><h1 style='color:#fff'>Fullscreen fixture</h1>"


def check(url, language, expected):
    profile = ROOT / 'artifacts' / f'fullscreen-{language}-{uuid4().hex[:8]}' / 'profile'
    database = profile / 'BrowserData' / 'settings.sqlite3'
    database.parent.mkdir(parents=True)
    with sqlite3.connect(database) as db:
        db.executescript('CREATE TABLE preferences(id INTEGER PRIMARY KEY, value TEXT NOT NULL);'
                         'PRAGMA application_id=1380271184; PRAGMA user_version=1;')
        preferences = dict(schema=1, searchEngine='google', downloadDirectory='', askDownload=False, startup='restore',
                           homepage='https://www.google.com/', shortcuts={}, interfaceLanguage=language)
        db.execute('INSERT INTO preferences VALUES(1,?)', (json.dumps(preferences),))
    directory, rows = run(url, ['--probe-fullscreen'], allow_failure=True, profile=profile)
    events = [r for r in rows if r['event'] in ('page_fullscreen', 'browser_fullscreen', 'fullscreen_entered',
                                               'fullscreen_exited', 'fullscreen_page', 'fullscreen_checks_pass')]
    frames = [r['details'] for r in rows if r['event'] == 'window_frame_fullscreen']
    entered = next((r['details'] for r in rows if r['event'] == 'fullscreen_entered'), {})
    exited = next((r['details'] for r in rows if r['event'] == 'fullscreen_exited'), {})
    hints = [r['details'] for r in rows if r['event'] == 'notification_shown' and r['details']['kind'] == 'fullscreen']
    checks = {
        'entered_window_and_geometry': entered.get('geometry_ok') is True,
        'escape_restored_window_and_geometry': exited.get('geometry_ok') is True,
        'system_rounding_restored_after_fullscreen': entered.get('root_corners_ok') is True and exited.get('root_corners_ok') is True,
        'fullscreen_notification_visible_then_hidden': entered.get('notification_visible') is True and exited.get('notification_hidden') is True,
        'both_notifications_painted_without_focus_change': len(hints) == 2 and all(h['painted'] and h['focus_preserved'] for h in hints),
        'both_notifications_inside_page_top_right': len(hints) == 2 and all(h['geometry_ok'] for h in hints),
        'both_notifications_localized': [(h.get('text'), h.get('key'), h.get('tail')) for h in hints] == [expected, expected],
        'page_and_browser_entered_and_exited': [(r['event'], r['details']['fullscreen']) for r in events
                                                if r['event'] in ('page_fullscreen', 'browser_fullscreen')] ==
            [('page_fullscreen', True), ('page_fullscreen', False), ('browser_fullscreen', True), ('browser_fullscreen', False)],
        'frame_square_borderless_then_restored': [(f['fullscreen'], f['corners_ok'] and f['border_ok']) for f in frames] ==
            [(True, True), (False, True)] * 2,
        'antialiased_corners_painted': any(r['event'] == 'page_corners' and r['details']['painted'] and r['details'].get('method') in ('composition', 'container-clip') for r in rows),
        'probe_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows),
    }
    result = {'passed': all(checks.values()), 'language': language, 'checks': checks, 'hints': hints, 'events': events}
    (directory / 'fullscreen.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2), flush=True)
    return result['passed']


def main():
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(PAGE)))
            self.end_headers(); self.wfile.write(PAGE)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        url = f'http://127.0.0.1:{server.server_port}/fullscreen'
        english = check(url, 'en', ('Press', 'Esc', 'to exit full screen'))
        ukrainian = check(url, 'uk', ('Щоб вийти з повноекранного режиму, натисніть', 'Esc', ''))
    finally:
        server.shutdown(); server.server_close()
    if not (english and ukrainian): raise RuntimeError('Fullscreen smoke failed')


if __name__ == '__main__': main()
