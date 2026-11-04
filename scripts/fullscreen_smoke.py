"""Page fullscreen acceptance through actual CEF in workspace mode.

The page enters fullscreen through the Fullscreen API (user gesture), native
code must make the window fullscreen and give the page the whole client area
without the rounded clip; Escape must restore the window and page bounds.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
from smoke import run

PAGE = b"<!doctype html><html lang='en'><title>Fullscreen fixture</title><body style='margin:0;background:#123'><h1 style='color:#fff'>Fullscreen fixture</h1>"


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
        directory, rows = run(f'http://127.0.0.1:{server.server_port}/fullscreen', ['--probe-fullscreen'], allow_failure=True)
    finally:
        server.shutdown(); server.server_close()
    events = [r for r in rows if r['event'] in ('page_fullscreen', 'fullscreen_entered', 'fullscreen_exited', 'fullscreen_page', 'fullscreen_checks_pass')]
    frames = [r['details'] for r in rows if r['event'] == 'window_frame_fullscreen']
    entered = next((r['details'] for r in rows if r['event'] == 'fullscreen_entered'), {})
    exited = next((r['details'] for r in rows if r['event'] == 'fullscreen_exited'), {})
    checks = {
        'entered_window_and_geometry': entered.get('geometry_ok') is True,
        'escape_restored_window_and_geometry': exited.get('geometry_ok') is True,
        'system_rounding_restored_after_fullscreen': entered.get('root_corners_ok') is True and exited.get('root_corners_ok') is True,
        'fullscreen_notification_visible_then_hidden': entered.get('notification_visible') is True and exited.get('notification_hidden') is True,
        'notification_painted_without_focus_change': any(r['event']=='notification_shown' and r['details']['kind']=='fullscreen' and r['details']['painted'] and r['details']['focus_preserved'] for r in rows),
        'notification_inside_page_top_right': any(r['event']=='notification_shown' and r['details']['kind']=='fullscreen' and r['details']['geometry_ok'] for r in rows),
        'page_saw_full_screen_then_normal': any(r['event'] == 'fullscreen_checks_pass' for r in rows),
        'frame_square_borderless_then_restored': [(f['fullscreen'], f['corners_ok'] and f['border_ok']) for f in frames] == [(True, True), (False, True)],
        'antialiased_corners_painted': any(r['event'] == 'page_corners' and r['details']['painted'] and r['details'].get('method') in ('composition', 'container-clip') for r in rows),
        'probe_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows),
    }
    result = {'passed': all(checks.values()), 'checks': checks, 'events': events}
    (directory / 'fullscreen.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    if not result['passed']: raise RuntimeError('Page fullscreen failed')


if __name__ == '__main__': main()
