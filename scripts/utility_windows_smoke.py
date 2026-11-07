"""Owned Settings/managers: monitor placement, commands and activation.

Runs only against disposable profiles and an empty manager; no OS auth UI.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
from smoke import run


def main():
    class Fixture(BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            body=b'<!doctype html><title>Utility windows fixture</title><h1>Utility windows fixture</h1>'
            self.send_response(200);self.send_header('Content-Type','text/html')
            self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    server=ThreadingHTTPServer(('127.0.0.1',0),Fixture)
    Thread(target=server.serve_forever,daemon=True).start()
    try:
        directory,rows=run(f'http://127.0.0.1:{server.server_port}/source?utility-windows',['--probe-link-headers=passwords'])
    finally: server.shutdown();server.server_close()
    setup=[r['details'] for r in rows if r['event']=='utility_probe_setup']
    assert setup and setup[-1]['geometry'],f'Negative-coordinate/small-workarea geometry failed: {directory}'
    windows=[r['details'] for r in rows if r['event']=='utility_probe_window']
    assert {r['name'] for r in windows}=={'settings','passwords','extensions','settings-reopened','settings-existing-moved'},f'Missing windows: {directory}'
    checks=('found','owned','sameDisplay','withinWorkArea','resizable','noMinimize','noMaximize','minDisabled','maxDisabled','systemCommandsBlocked','foreground')
    for window in windows:
        assert all(window.get(key) for key in checks),f'{window}: {directory}'
    fullscreen=[r['details'] for r in rows if r['event']=='utility_probe_fullscreen']
    assert len(fullscreen)==3 and all(r['normal'] for r in fullscreen),f'Fullscreen suppression failed: {directory}'
    closed=[r['details'] for r in rows if r['event']=='utility_probe_closed']
    assert len(closed)==5 and all(r['destroyed'] and r['correctForeground'] for r in closed),f'Close/activation failed: {closed}: {directory}'
    assert not any(r['event']=='probe_shell_error' for r in rows),f'Shell error: {directory}'
    if not setup[-1]['multipleDisplays']:
        print('Single display only: cross-monitor placement was not exercised.')
    print(f'Utility-window native behavior passed: {directory}')


if __name__=='__main__': main()
