"""Full downloads page (sol://ui/downloads.html) through actual CEF.

Downloads notes.txt, opens the page via the Ctrl+J path, checks it is a
trusted-UI tab (Chrome style, UI profile), that shell and history commands from it are
refused (403), that it lists the file, that "Remove from list" deletes the saved
entry (the file stays), and that it cannot navigate away.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
import uuid
from smoke import ROOT, run


def main():
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            if self.path == '/notes.txt':
                body = b'downloads page notes\n'
                self.send_response(200)
                self.send_header('Content-Type', 'text/plain')
                self.send_header('Content-Disposition', 'attachment; filename="notes.txt"')
            else:
                body = b"<!doctype html><html lang='en'><title>Alpha page</title><h1>Alpha</h1>"
                self.send_response(200)
                self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    downloads = ROOT / 'artifacts' / ('downloads-page-' + uuid.uuid4().hex[:8])
    downloads.mkdir(parents=True)
    try:
        directory, rows = run(f'http://127.0.0.1:{server.server_port}/alpha', ['--probe-downloads-page', f'--probe-downloads-dir={downloads}'], allow_failure=True)
    finally:
        server.shutdown(); server.server_close()
    opened = next((r['details'] for r in rows if r['event'] == 'downloads_page_opened'), {})
    messages = [r['details']['message'] for r in rows if r['event'] == 'downloads_page_probe']
    store = next((r['details'] for r in rows if r['event'] == 'downloads_page_store'), {})
    checks = {
        'opened_as_trusted_ui_tab': opened.get('chrome_style') is True and opened.get('ui_profile') is True and opened.get('registered') is True,
        'shell_command_refused': 'RED_DOWNLOADS_PAGE:shell_command:403' in messages,
        'history_command_refused': 'RED_DOWNLOADS_PAGE:history_command:403' in messages,
        'listed_download': any(m.startswith('RED_DOWNLOADS_PAGE:rows:') and int(m.rsplit(':', 1)[1]) >= 1 for m in messages),
        'removed_from_store': 'RED_DOWNLOADS_PAGE:removed:0' in messages and store.get('saved') == 0,
        'file_kept': (downloads / 'notes.txt').exists(),
        'navigation_locked': 'RED_DOWNLOADS_PAGE:locked:sol://ui/downloads.html' in messages,
        'probe_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows),
    }
    result = {'passed': all(checks.values()), 'checks': checks, 'opened': opened, 'messages': messages}
    (directory / 'downloads_page.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    if not result['passed']: raise RuntimeError('Downloads page smoke failed')


if __name__ == '__main__': main()
