"""History and download persistence through actual CEF, across a restart.

Run 1 (--probe-history=record): visit /alpha and /beta, download notes.txt,
remove "Alpha page" through the real history popover.
Run 2 (same profile, --probe-history=restart): "Beta page" persisted, "Alpha
page" stays removed, the finished download is still saved (page only), then
"Clear history: All time" through the popover empties history.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
import uuid
from smoke import ROOT, run

PAGES = {'/alpha': 'Alpha page', '/beta': 'Beta page', '/start': 'Start page'}


def main():
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            if self.path == '/notes.txt':
                body = b'history smoke notes\n'
                self.send_response(200)
                self.send_header('Content-Type', 'text/plain')
                self.send_header('Content-Disposition', 'attachment; filename="notes.txt"')
            else:
                title = PAGES.get(self.path, 'Fixture')
                body = f"<!doctype html><html lang='en'><title>{title}</title><h1>{title}</h1>".encode()
                self.send_response(200)
                self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    work = ROOT / 'artifacts' / ('history-' + uuid.uuid4().hex[:8])
    profile, downloads = work / 'profile', work / 'downloads'
    downloads.mkdir(parents=True)
    try:
        first, rows1 = run(base + '/alpha', ['--probe-history=record', f'--probe-downloads-dir={downloads}'], profile=profile, allow_failure=True)
        second, rows2 = run(base + '/start', ['--probe-history=restart', f'--probe-downloads-dir={downloads}'], profile=profile, allow_failure=True)
    finally:
        server.shutdown(); server.server_close()
    steps = lambda rows: [r['details']['step'] for r in rows if r['event'] == 'history_probe_step']
    restored = next((r['details']['names'] for r in rows2 if r['event'] == 'downloads_saved_at_start'), [])
    checks = {
        'recorded_visits': 'recorded' in steps(rows1),
        'removed_through_popover': 'removed_through_popover' in steps(rows1),
        'download_finished': any(r['event'] == 'download_state' and r['details']['state'] == 'complete' for r in rows1),
        'run1_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows1),
        'persisted_after_restart': 'persisted' in steps(rows2),
        'download_restored_after_restart': 'notes.txt' in restored,
        'cleared_through_popover': 'cleared_through_popover' in steps(rows2) and any(r['event'] == 'history_checks_pass' for r in rows2),
        'run2_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows2),
        'no_probe_failure': not any(r['event'] == 'history_probe_failed' for r in rows1 + rows2),
    }
    result = {'passed': all(checks.values()), 'checks': checks, 'restored': restored,
              'failures': [r['details'] for r in rows1 + rows2 if r['event'] == 'history_probe_failed'],
              'runs': [str(first), str(second)]}
    (second / 'history.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    if not result['passed']: raise RuntimeError('History smoke failed')


if __name__ == '__main__': main()
