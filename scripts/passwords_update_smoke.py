"""Real synthetic password change, native consent, persistence and autofill.

One loopback origin and disposable profile span three browser launches. No
real credentials, secret-reading WebUI calls or OS authentication automation.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
from urllib.parse import parse_qs, urlsplit
import sqlite3
from pathlib import Path
from smoke import run, ROOT


def encrypted_row(profile):
    database=profile/'Default'/'Login Data'
    with sqlite3.connect(database.as_uri()+'?mode=ro',uri=True) as connection:
        rows=connection.execute('select id,password_value from logins where blacklisted_by_user=0').fetchall()
    assert len(rows)==1, 'Expected one stored credential'
    row_id,ciphertext=rows[0]
    assert len(ciphertext)>24 and all(value not in ciphertext for value in (b'Synthetic-password-42!',b'Synthetic-updated-73!')), 'Password was not encrypted'
    return row_id,ciphertext


def result(rows,phase):
    values=[r['details'] for r in rows if r['event']=='passwords_probe_result' and r['details'].get('phase')==phase]
    assert values,f'Missing native result: {phase}'
    return values[-1]


def main(seed_profile=None,port=0):
    submissions=[]
    class Fixture(BaseHTTPRequestHandler):
        changed=False
        def log_message(self,*_): pass
        def do_GET(self):
            path=urlsplit(self.path)
            style='<style>input,button{position:absolute;left:10px;width:220px;height:40px;box-sizing:border-box}#username{top:10px}#password,#old-password{top:60px}#new-password{top:110px}#confirm-password{top:160px}button{top:110px}form.change button{top:210px}</style>'
            if path.path=='/source' and path.query=='passwords-update':
                content="""<form class='change' action='/change' method='post'>
<input id='username' name='username' autocomplete='username' value='sol-synthetic' readonly>
<input id='old-password' name='old-password' type='password' autocomplete='current-password'>
<input id='new-password' name='new-password' type='password' autocomplete='new-password'>
<input id='confirm-password' name='confirm-password' type='password' autocomplete='new-password'>
<button>Change password</button></form>"""
            elif path.path=='/source':
                content="""<form action='/done' method='post'><input id='username' name='username' autocomplete='username'>
<input id='password' name='password' type='password' autocomplete='current-password'><button>Sign in</button></form>"""
            else: content='<h1>Password changed</h1>' if self.changed else '<h1>Signed in</h1>'
            body=('<!doctype html><meta charset="utf-8"><title>Synthetic password-change fixture</title>'+style+content).encode()
            self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8')
            self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
        def do_POST(self):
            data=parse_qs(self.rfile.read(int(self.headers.get('Content-Length',0))).decode())
            if self.path=='/change':
                valid=data=={'username':['sol-synthetic'],'old-password':['Synthetic-password-42!'],
                             'new-password':['Synthetic-updated-73!'],'confirm-password':['Synthetic-updated-73!']}
                submissions.append(('change',valid))
                if valid:type(self).changed=True
            else:
                valid=data=={'username':['sol-synthetic'],'password':['Synthetic-password-42!']}
                submissions.append(('login',valid))
            if not valid:self.send_response(400);self.end_headers();return
            self.send_response(303);self.send_header('Location','/done');self.end_headers()
    server=ThreadingHTTPServer(('127.0.0.1',port),Fixture)
    Thread(target=server.serve_forever,daemon=True).start()
    try:
        origin=f'http://127.0.0.1:{server.server_port}'
        if seed_profile:
            profile=seed_profile.resolve()
            assert profile.is_relative_to((ROOT/'artifacts').resolve()) and port, 'Resume requires a disposable artifacts profile and its original loopback port'
            saved=profile.parent
            expected_submissions=[]
        else:
            saved,rows=run(origin+'/source?passwords-save',['--probe-link-headers=passwords'])
            assert submissions==[('login',True)],f'Initial login was not submitted: {saved}'
            stored=result(rows,'store')
            assert stored['count']==1 and stored['synthetic']==1,f'Original login was not saved: {saved}'
            profile=saved/'profile'
            expected_submissions=[('login',True)]
        original=encrypted_row(profile)
        updated,rows=run(origin+'/source?passwords-update',['--probe-link-headers=passwords'],profile=profile)
        assert submissions==expected_submissions+[('change',True)] and Fixture.changed,f'Password-change form failed: {updated}'
        typed=result(rows,'change_input')
        assert all(typed[key] for key in ('old','updated','confirmed')),f'Change form was not filled correctly: {updated}'
        prompts=[r['details'] for r in rows if r['event']=='passwords_probe_update_prompt']
        assert prompts and prompts[-1]['visible'] and prompts[-1]['updateTitle'],f'Chromium did not offer Update password: {updated}'
        accepted=[r['details'] for r in rows if r['event']=='passwords_probe_update_accept']
        assert accepted and accepted[-1]['invoked'],f'Update consent was not invoked: {updated}'
        filled=result(rows,'updated_autofill');stored=result(rows,'updated_store')
        assert filled['username'] and filled['updated'] and not filled['old'],f'Updated password did not autofill immediately: {updated}'
        assert stored['count']==1 and stored['synthetic']==1 and stored['noBridge'],f'Update duplicated or exposed the credential: {updated}'
        changed=encrypted_row(profile)
        assert original[0]==changed[0] and original[1]!=changed[1],f'Existing encrypted row was not updated: {updated}'
        restarted,rows=run(origin+'/source?passwords-updated',['--probe-link-headers=passwords'],profile=profile)
        filled=result(rows,'updated_autofill');stored=result(rows,'updated_store')
        assert filled['username'] and filled['updated'] and not filled['old'],f'Restart autofill used the wrong password: {restarted}'
        assert stored['count']==1 and stored['synthetic']==1 and stored['noBridge'],f'Credential changed after restart: {restarted}'
        assert encrypted_row(profile)[0]==original[0],f'Credential identity changed after restart: {restarted}'
        print(f'Native password change, same encrypted row and restart autofill passed: {saved}, {updated}, {restarted}')
    finally:server.shutdown();server.server_close()


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser()
    parser.add_argument('--seed-profile',type=Path,help='Resume a disposable fixture after its original save succeeded')
    parser.add_argument('--port',type=int,default=0,help='Original loopback port when resuming')
    args=parser.parse_args()
    main(args.seed_profile,args.port)
