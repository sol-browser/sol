"""Full history page (sol://ui/history.html) through actual CEF.

Visits /alpha and /beta, opens the page with Ctrl+Shift+H's path, checks it
runs as a Chrome-style tab in the trusted UI profile (not the web profile), that a shell
command from it is refused (403), that selecting both visits and "Delete
selected" removes them from the store, and that it cannot navigate away.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
from smoke import run

PAGES = {'/alpha': 'Alpha page', '/beta': 'Beta page'}


def main():
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            title = PAGES.get(self.path, 'Fixture')
            body = f"<!doctype html><html lang='en'><title>{title}</title><h1>{title}</h1>".encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        directory, rows = run(f'http://127.0.0.1:{server.server_port}/alpha', ['--probe-history=page'], allow_failure=True)
    finally:
        server.shutdown(); server.server_close()
    opened = next((r['details'] for r in rows if r['event'] == 'history_page_opened'), {})
    messages = [r['details']['message'] for r in rows if r['event'] == 'history_page_probe']
    steps = [r['details']['step'] for r in rows if r['event'] == 'history_probe_step']
    checks = {
        'opened_as_trusted_ui_tab': opened.get('chrome_style') is True and opened.get('ui_profile') is True and opened.get('registered') is True,
        'not_in_web_profile': opened.get('web_profile') is False,
        'shell_command_refused': 'RED_HISTORY_PAGE:shell_command:403' in messages,
        'rendered_visits': any(m.startswith('RED_HISTORY_PAGE:rows:') and int(m.rsplit(':', 1)[1]) >= 2 for m in messages),
        'deleted_selected_from_store': 'RED_HISTORY_PAGE:deleted:0' in messages and 'deleted_through_page' in steps,
        'navigation_locked': 'RED_HISTORY_PAGE:locked:sol://ui/history.html' in messages,
        'probe_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows),
    }
    result = {'passed': all(checks.values()), 'checks': checks, 'opened': opened, 'messages': messages}
    (directory / 'history_page.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    if not result['passed']: raise RuntimeError('History page smoke failed')


if __name__ == '__main__': main()
