"""Drive the real CEF Settings picker with keyboard input at minimum width."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading

from smoke import run


def main():
    class Page(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.end_headers()
            self.wfile.write(b'<!doctype html><title>Settings picker fixture</title>')

    with ThreadingHTTPServer(('127.0.0.1', 0), Page) as server:
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            directory, rows = run(f'http://127.0.0.1:{server.server_port}/', ['--probe-menu=settings-picker'])
        finally:
            server.shutdown()

    def message(prefix):
        return next((json.loads(row['details']['message'].removeprefix(prefix)) for row in rows
                     if row['event'] == 'settings_console' and row['details']['message'].startswith(prefix)), {})

    picker = message('RED_SETTINGS_PICKER:')
    escape = message('RED_PICKER_ESCAPE:')
    saved = next((row['details']['target'] for row in rows if row['event'] == 'settings_picker_saved'), '')
    passed = (all(picker.get(key) for key in ('open', 'custom', 'graphite', 'visible'))
              and 0 < picker['width'] <= 720 and picker['options'] > 25
              and escape == dict(closed=True, focus=True, value='system')
              and bool(saved) and saved != 'system')
    result = dict(passed=passed, picker=picker, escape=escape, saved=saved)
    (directory / 'settings-picker.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    if not passed:
        raise SystemExit(f'Settings picker failed: {directory}')


if __name__ == '__main__':
    main()
