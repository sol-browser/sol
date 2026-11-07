"""Synthetic localhost saving, encrypted persistence and real Chromium autofill.
Uses a disposable root profile; never interacts with the daily Sol process.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
import sqlite3
from smoke import run


def check_dismissed(origin):
    directory,rows=run(origin+'/source?passwords-dismiss',['--probe-link-headers=passwords'])
    closed=[r['details'] for r in rows if r['event']=='passwords_probe_tab_closed']
    stored=[r['details'] for r in rows if r['event']=='passwords_probe_result' and r['details'].get('phase')=='store']
    assert closed and closed[-1]['closed'] and stored and stored[-1]['count']==0, f'Closing a pending save failed: {directory}'
    print(f'Pending save dismissal and subsequent browsing passed: {directory}')


def check_manager_link(origin):
    directory,rows=run(origin+'/source?passwords-link',['--probe-link-headers=passwords'])
    links=[r['details'] for r in rows if r['event']=='passwords_probe_manager_link']
    stored=[r['details'] for r in rows if r['event']=='passwords_probe_result' and r['details'].get('phase')=='store']
    assert links and links[-1]['invoked'], f'Native footer hyperlink was not invoked: {directory}'
    assert stored and stored[-1]['ui'] and stored[-1]['noBridge'] and stored[-1]['count']==0, f'Footer did not open the isolated native manager: {directory}'
    assert any(r['event']=='foreign_browser_resolved' and r['details']['outcome']=='password_manager' for r in rows), f'Footer routing not observed: {directory}'
    assert not any(r['event']=='probe_shell_error' for r in rows), f'Unexpected chrome UI error: {directory}'
    print(f'Native password-bubble footer routing passed: {directory}')


def check_bubble_movement(origin):
    directory,rows=run(origin+'/source?passwords-move',['--probe-link-headers=passwords'])
    positions={r['details']['phase']:r['details'] for r in rows if r['event']=='passwords_probe_bubble_position'}
    assert set(positions)=={'before','moved','resized'} and all(r['visible'] for r in positions.values()), f'Bubble disappeared during movement: {positions}: {directory}'
    for phase in ('moved','resized'):
        assert all(abs(positions[phase][axis]-positions['before'][axis])<=3 for axis in ('right','top')), f'Bubble did not follow its anchor: {positions}: {directory}'
    stored=[r['details'] for r in rows if r['event']=='passwords_probe_result' and r['details'].get('phase')=='store']
    assert stored and stored[-1]['synthetic']==1, f'Moving the bubble broke saving: {directory}'
    print(f'Native save bubble follows window movement/resizing: {directory}')


def main(dismiss_only=False,link_only=False,move_only=False):
    submissions = []
    class Fixture(BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            page = b"""<!doctype html><title>Synthetic password fixture</title>
<style>input,button{position:absolute;left:10px;width:220px;height:40px;box-sizing:border-box}#username{top:10px}#password{top:60px}button{top:110px}</style>
<form action='/done' method='post'><input id='username' name='username' autocomplete='username'><input id='password' name='password' type='password' autocomplete='current-password'><button>Sign in</button></form>""" if self.path.startswith('/source') else b'<!doctype html><title>Signed in</title><h1>Signed in</h1>'
            self.send_response(200)
            self.send_header('Content-Type','text/html; charset=utf-8')
            self.send_header('Content-Length',str(len(page)))
            self.end_headers(); self.wfile.write(page)
        def do_POST(self):
            from urllib.parse import parse_qs
            data=parse_qs(self.rfile.read(int(self.headers.get('Content-Length',0))).decode())
            submissions.append(data=={'username':['sol-synthetic'],'password':['Synthetic-password-42!']})
            self.send_response(303);self.send_header('Location','/done');self.end_headers()
    server=ThreadingHTTPServer(('127.0.0.1',0),Fixture)
    Thread(target=server.serve_forever,daemon=True).start()
    try:
        origin=f'http://127.0.0.1:{server.server_port}'
        if move_only:
            check_bubble_movement(origin);return
        if link_only:
            check_manager_link(origin);return
        if dismiss_only:
            check_dismissed(origin);return
        directory,rows=run(origin+'/source?passwords-save',['--probe-link-headers=passwords'])
        assert submissions==[True], 'Synthetic form was not submitted'
        prompts=[r['details'] for r in rows if r['event']=='passwords_probe_save_prompt']
        assert prompts and prompts[-1]['invoked'], f'Native save prompt was not usable: {directory}'
        stored=[r['details'] for r in rows if r['event']=='passwords_probe_result' and r['details'].get('phase')=='store']
        assert stored and stored[-1]['synthetic']==1 and stored[-1]['ui'], f'Password was not saved in Chromium: {directory}'
        database=directory/'profile'/'Default'/'Login Data'
        with sqlite3.connect(database.as_uri()+'?mode=ro',uri=True) as connection:
            values=connection.execute('select password_value from logins').fetchall()
        assert len(values)==1 and len(values[0][0])>24 and b'Synthetic-password-42!' not in values[0][0], 'Credential was not encrypted'
        again,rows=run(origin+'/source?passwords-check',['--probe-link-headers=passwords'],profile=directory/'profile')
        filled=[r['details'] for r in rows if r['event']=='passwords_probe_result' and r['details'].get('phase')=='autofill']
        assert filled and filled[-1]['username'] and filled[-1]['password'], f'Autofill after restart failed: {again}'
        isolation=[r['details'] for r in rows if r['event']=='passwords_probe_result' and r['details'].get('phase')=='isolation']
        assert isolation and all(isolation[-1][key] for key in ('empty','added','removed','noBridge')), f'Profile isolation or credential operations failed: {again}'
        stored=[r['details'] for r in rows if r['event']=='passwords_probe_result' and r['details'].get('phase')=='store']
        assert stored and stored[-1]['count']==1 and stored[-1]['synthetic']==1 and stored[-1]['noBridge'], f'Default credentials changed across profiles: {again}'
        settings=[r['details'] for r in rows if r['event']=='passwords_probe_result' and r['details'].get('phase')=='settings']
        assert settings and settings[-1]['profileChoices']==2 and settings[-1]['selected'] and settings[-1]['wrongProfile']==409 and settings[-1]['extraField']==400 and settings[-1]['noSecrets'], f'Settings integration or validation failed: {again}'
        check_dismissed(origin)
        check_manager_link(origin)
        check_bubble_movement(origin)
        print(f'Password save and restart autofill passed: {directory}, {again}')
    finally: server.shutdown();server.server_close()

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser()
    parser.add_argument('--dismiss-only',action='store_true')
    parser.add_argument('--link-only',action='store_true')
    parser.add_argument('--move-only',action='store_true')
    args=parser.parse_args()
    main(args.dismiss_only,args.link_only,args.move_only)
