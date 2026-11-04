"""Site settings acceptance through actual CEF in workspace mode.

Drives the real site popover (Arc-style site controls): Location -> Block
(Chromium stores BLOCK, the page sees "denied"), Camera -> Allow and a stream
from Chromium's fake camera (no prompt; the tab and address-bar indicator data
turn on, then off when the stream stops), Reset (the page sees "prompt").
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
from smoke import run

PAGE = b"<!doctype html><html lang='en'><title>Site settings fixture</title><body style='font:20px Segoe UI;padding:40px'><h1>Site settings fixture</h1>"


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
        # localhost is a secure context, so camera and location are available.
        directory, rows = run(f'http://localhost:{server.server_port}/site', ['--probe-site-settings'], allow_failure=True)
    finally:
        server.shutdown(); server.server_close()
    names = [r['event'] for r in rows]
    rendered = [r['details'] for r in rows if r['event'] == 'panel_rendered']
    changes = [r['details'] for r in rows if r['event'] == 'site_setting_changed']
    media = [r['details'] for r in rows if r['event'] == 'media_access']
    checks = {
        'popover_rendered_rows': any(r.get('kind') == 'site' and r.get('rows', 0) >= 1 for r in rendered),
        'location_blocked_through_popover': {'key': 'location', 'value': 'block'} in changes and 'site_location_blocked' in names,
        'camera_allowed_through_popover': {'key': 'camera', 'value': 'allow'} in changes,
        'camera_indicator_on_then_off': 'site_camera_indicator' in names and any(m['camera'] for m in media) and not media[-1]['camera'] if media else False,
        'reset_through_popover': 'site_settings_reset' in names,
        'page_saw_denied_then_prompt': 'site_checks_pass' in names,
        'no_probe_failure': 'site_probe_failed' not in names,
        'probe_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows),
    }
    result = {'passed': all(checks.values()), 'checks': checks,
              'failures': [r['details'] for r in rows if r['event'] == 'site_probe_failed'],
              'events': [r for r in rows if r['event'].startswith(('site_', 'media_access', 'panel_'))]}
    (directory / 'site_settings.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    if not result['passed']: raise RuntimeError('Site settings failed')


if __name__ == '__main__': main()
