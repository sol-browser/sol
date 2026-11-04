"""Duplicate, mute and reopen-closed-tab through actual CEF in workspace mode."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
from smoke import run


def main():
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            body = b"<!doctype html><html lang='en'><title>Alpha page</title><h1>Alpha page</h1>"
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        directory, rows = run(f'http://127.0.0.1:{server.server_port}/alpha', ['--probe-tab-actions'], allow_failure=True)
    finally:
        server.shutdown(); server.server_close()
    steps = [r['details']['step'] for r in rows if r['event'] == 'tab_actions_step']
    checks = {
        'duplicated_current_page': 'duplicated' in steps,
        'muted_and_unmuted': 'muted_and_unmuted' in steps,
        'closed_tab_archived': 'closed' in steps,
        'reopened_with_ctrl_shift_t_path': 'reopened' in steps,
        'no_failure': not any(r['event'] == 'tab_actions_failed' for r in rows),
        'probe_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows),
    }
    result = {'passed': all(checks.values()), 'checks': checks,
              'failures': [r['details'] for r in rows if r['event'] == 'tab_actions_failed']}
    (directory / 'tab_actions.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    if not result['passed']: raise RuntimeError('Tab actions smoke failed')


if __name__ == '__main__': main()
