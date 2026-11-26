"""Library through real CEF with a disposable workspace and local page."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
import json
import sqlite3

from smoke import run


def main():
    class Fixture(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            # A loud colour: the probe finds the page on the composited screen during the glide.
            body = (b'<!doctype html><title>Library fixture</title><style>html{background:#e0115f}</style>'
                    b'<input id="library-fixture" aria-label="Unsaved form">')
            self.send_response(200)
            self.send_header('Content-Type', 'text/html')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    with ThreadingHTTPServer(('127.0.0.1', 0), Fixture) as server:
        Thread(target=server.serve_forever, daemon=True).start()
        try:
            directory, rows = run(f'http://127.0.0.1:{server.server_port}/fixture', ['--probe-menu=library'], allow_failure=True)
        finally:
            server.shutdown()

    events = {row['event']: row.get('details', {}) for row in rows}
    assert events.get('library_result', {}).get('result') == 'pass', (directory, events.get('library_result'))
    assert (events['library_layout']['native_bounds'] and events['library_layout']['native_input'] and
            'library_form_preserved' in events), (directory, events['library_layout'])
    assert events.get('probe_result', {}).get('pass') and 'shutdown' in events, directory
    glides = [row.get('details', {}) for row in rows if row['event'] == 'library_rail_animation']
    assert glides and all(glide['frames'] >= 4 for glide in glides), (directory, glides)
    # The composited screen shows the page moving, not only the window being asked to move.
    shown = [row['details']['screen'] for row in rows if row['event'] == 'library_rail_frame' and row['details']['screen'] >= 0]
    assert len(set(shown)) >= 6, (directory, shown)
    # The page card and the page edge glide as one (user report 2026-11-26: the
    # card froze while the page moved on, up to 193 px apart). The probe paints
    # the card green; its edge shows on the screen only where it trails the page.
    starts = [row['details'] for row in rows if row['event'] == 'library_rail_start']
    late = {row['details']['id'] for row in rows if row['event'] == 'library_rail_late_card'}
    assert len(starts) >= 3 and all(s['acknowledged'] or s['id'] in late for s in starts), (directory, starts)
    gaps = [row['details']['screen'] - row['details']['card'] for row in rows if row['event'] == 'library_rail_frame'
            and row['details']['card'] >= 0 and row['details']['screen'] >= 0]
    scale = max([row['details']['scale'] for row in rows if row['event'] == 'library_rail_frame'] or [1])
    assert len(gaps) >= 20 and sorted(gaps)[len(gaps) // 2] <= 6 * scale and max(gaps) <= 40 * scale, (directory, gaps)
    traces = {trace['label']: trace for trace in (json.loads(row['details']['trace']) for row in rows
                                                  if row['event'] == 'library_shell_glide')}
    # The reopen glide held the shell's main thread (the card must not freeze with it).
    assert any(frame['ms'] >= 100 for frame in traces['reopen']['busy']), (directory, traces.get('reopen'))
    if events['library_layout']['foreground']:
        assert events['library_layout']['page_keyboard'], (directory, events['library_layout'])
    with sqlite3.connect(directory / 'profile/BrowserData/settings.sqlite3') as db:
        saved = json.loads(db.execute('SELECT value FROM preferences WHERE id=1').fetchone()[0])
        assert saved['linkRouting']['rules'][0]['pattern'] == 'example.test/team/'
    for section in ('spaces', 'archive', 'routes', 'editor'):
        image = directory / f'library-{section}.png'
        assert image.is_file() and image.stat().st_size > 1000, image
    report = {'passed': True, 'layout': events['library_layout'], 'glides': glides, 'directory': str(directory),
              'card_gap': {'frames': len(gaps), 'median': sorted(gaps)[len(gaps) // 2], 'max': max(gaps)}, 'starts': starts}
    (directory / 'library.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
