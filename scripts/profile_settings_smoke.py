"""Real Settings profile dialog, live Space assignment, beforeunload and export."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
import json
import sqlite3
from smoke import run


def main():
    class Fixture(BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            body=b'<!doctype html><title>Profile settings fixture</title><h1>Profile settings fixture</h1><input aria-label="Unsaved input">'
            self.send_response(200); self.send_header('Content-Type','text/html'); self.send_header('Content-Length',str(len(body)))
            self.end_headers(); self.wfile.write(body)
    with ThreadingHTTPServer(('127.0.0.1',0),Fixture) as server:
        Thread(target=server.serve_forever,daemon=True).start()
        try:
            directory,rows=run(f'http://127.0.0.1:{server.server_port}/profiles-settings',['--probe-organization'])
        finally: server.shutdown()
    observations=[row['details'] for row in rows if row['event']=='profile_settings_ui']
    assert [item['phase'] for item in observations]==['preview','saved'] and all(item['passed'] for item in observations),observations
    assert observations[0]['selected']==2,observations
    placement=next(row['details'] for row in rows if row['event']=='profile_editor_placement')
    assert placement['sidebar_clear'] and placement['y']>=31,placement
    box=observations[0]['dialog']; viewport=observations[0]['viewport']
    assert box['x']>=0 and box['y']>=0 and box['right']<=viewport['width'] and box['bottom']<=viewport['height'],observations
    for image in ('profile-dialog.png','profile-settings.png'): assert (directory/image).stat().st_size>1000,image
    if placement['presented']: assert (directory/'space-editor.png').stat().st_size>1000
    snapshot=next(row['details'] for row in rows if row['event']=='organization_checks_pass')
    assert not any(profile['name']=='Cancelled profile' for profile in snapshot['profiles']),snapshot['profiles']
    assert snapshot['organizationUnloadDecisions']==2,snapshot
    with sqlite3.connect(directory/'profile'/'BrowserData'/'workspace.sqlite3') as db:
        assert db.execute("SELECT count(*) FROM browsing_profiles WHERE name IN ('Studio','Confirmed profile')").fetchone()[0]==2
    print(json.dumps({'passed':True,'ui':observations,'editor_placement':placement,'screenshots':str(directory)},indent=2),flush=True)


if __name__=='__main__': main()
