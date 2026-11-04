"""Window close without prompts, renderer crash/retry and unclean-session recovery."""
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse
import json
import os
import sqlite3
import subprocess
import threading
import time
import uuid
from smoke import ROOT, run

A = '11111111-1111-4111-8111-111111111111'
FOLDER = '22222222-2222-4222-8222-222222222222'
B = '33333333-3333-4333-8333-333333333333'
PAGE = b'''<!doctype html><html lang="en"><meta charset="utf-8"><title>Owned lifecycle fixture</title>
<style>body{font:18px Segoe UI;background:#fff;color:#222;padding:24px}input{display:block;margin:20px 0;width:360px;padding:12px}</style>
<h1>Owned session reliability fixture</h1><p>Type here to protect this page with a real beforeunload handler.</p>
<input aria-label="Unsaved fixture text" value="initial" oninput="onbeforeunload=e=>{e.preventDefault();e.returnValue='';}">
<p>Each page uses its own hostname. Test data only.</p><button onclick="console.info('RED_LIFE:manual-crash')">Crash this test page</button></html>'''

def saved(database):
    with sqlite3.connect(database) as db:
        # last_active_at moves whenever the restored page becomes active again.
        columns = [c[1] for c in db.execute('PRAGMA table_info(nodes)') if c[1] != 'last_active_at']
        return {'nodes': db.execute(f"SELECT {','.join(columns)} FROM nodes ORDER BY id").fetchall(),
                'state': db.execute('SELECT * FROM state').fetchall()}

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--manual', action='store_true'); args = parser.parse_args()
    counts = Counter()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            if self.path in ('/a', '/b'): counts[self.path] += 1
            self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.send_header('Cache-Control', 'no-store'); self.send_header('Content-Length', str(len(PAGE))); self.end_headers()
            try: self.wfile.write(PAGE)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    profile = ROOT / 'artifacts' / ('lifecycle-profile-' + uuid.uuid4().hex[:8])
    database = profile / 'BrowserData/workspace.sqlite3'; database.parent.mkdir(parents=True)
    a_url = f'http://127.0.0.1:{server.server_port}/a'; b_url = f'http://localhost:{server.server_port}/b'
    with sqlite3.connect(database) as db:
        # Our own prior public schema; normal application migration owns v2.
        db.executescript('CREATE TABLE nodes(id TEXT PRIMARY KEY,parent_id TEXT,kind INTEGER,title TEXT,url TEXT,position INTEGER,pinned INTEGER,expanded INTEGER);CREATE TABLE state(singleton INTEGER PRIMARY KEY,revision INTEGER,active_id TEXT);PRAGMA application_id=1380271170;PRAGMA user_version=1;')
        db.executemany('INSERT INTO nodes VALUES(?,?,?,?,?,?,?,?)', [(A,None,0,'Protected A',a_url,0,0,1),(FOLDER,None,1,'Saved work','',1,1,1),(B,FOLDER,0,'Protected B',b_url,0,1,1)])
        db.execute('INSERT INTO state VALUES(1,1,?)', (A,))
    exe = os.environ.get('RED_BROWSER_TEST_EXE', str(ROOT / 'build/Release/apps/desktop/Release/sol.exe'))
    try:
        if args.manual:
            report = profile.parent / ('manual-lifecycle-' + uuid.uuid4().hex[:8]); report.mkdir()
            with (report / 'process.log').open('w') as log:
                process = subprocess.Popen([exe,'--workspace',f'--profile-dir={profile}',f'--probe-report={report / "native.jsonl"}','--probe-input','--probe-lifecycle=manual'],cwd=os.path.dirname(exe),stdout=log,stderr=log)
                print(json.dumps({'directory':str(report),'profile':str(profile),'url':a_url,'pid':process.pid}),flush=True)
                process.wait()
            return
        first, rows = run(a_url, ['--probe-lifecycle'], profile=profile)
        before = saved(database)
        decisions = [r['details']['leave'] for r in rows if r['event']=='lifecycle_real_unload_decision']
        crashes = [r['details'] for r in rows if r['event']=='renderer_terminated']
        starts = [r['details'] for r in rows if r['event']=='window_close_started']
        approvals = sum(r['event']=='window_close_unload_approved' for r in rows)
        # Window close never asks: both armed pages' beforeunload is accepted
        # natively, no dialog decision is requested, children close before the shell.
        assert decisions == [] and len(crashes)==1 and crashes[0]['recoverable'], (decisions,crashes)
        assert not any(r['event'] in ('window_close_cancelled','window_close_confirmation') for r in rows) and approvals==2, approvals
        assert len(starts)==3 and starts[-1]['shell'] and not any(r['shell'] for r in starts[:-1]), starts
        assert counts == {'/a':1,'/b':3}, dict(counts)
        result = {'passed':True,'run':first.name,'decisions':decisions,'crashes':crashes,'aggregate_unload_approvals':approvals,'requests':dict(counts)}
        (first/'lifecycle.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2),flush=True)
        second, _ = run(a_url,['--probe-lifecycle=restore'],profile=profile)
        assert saved(database)==before and counts=={'/a':2,'/b':3}, dict(counts)
        (second/'lifecycle.json').write_text(json.dumps({'passed':True,'same_saved_state':True,'requests':dict(counts)},indent=2)+'\n')
        hold = ROOT/'artifacts'/('lifecycle-unclean-'+uuid.uuid4().hex[:8]);hold.mkdir()
        report=hold/'native.jsonl'
        with (hold/'process.log').open('w') as log:
            process=subprocess.Popen([exe,f'--profile-dir={profile}',f'--probe-report={report}',f'--probe-url={a_url}','--probe-lifecycle=hold'],cwd=os.path.dirname(exe),stdout=log,stderr=log)
            ready=False
            try:
                deadline=time.monotonic()+20
                while time.monotonic()<deadline and process.poll() is None:
                    text=report.read_text() if report.exists() else ''
                    if '"event":"lifecycle_hold_ready"' in text: ready=True;break
                    time.sleep(.1)
                assert ready,'Owned unclean-run readiness timed out'
                committed=saved(database)
            finally:
                if process.poll() is None:
                    subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True)  # A child may already be gone; wait() below verifies the kill.
                process.wait(timeout=10)
        (hold/'lifecycle.json').write_text(json.dumps({'passed':ready,'expected_unclean_exit':True,'owned_pid':process.pid,'requests':dict(counts)},indent=2)+'\n')
        final, _ = run(a_url,['--probe-lifecycle=restore'],profile=profile)
        assert saved(database)==committed and counts=={'/a':4,'/b':4}, dict(counts)
        result={'passed':True,'full_run':first.name,'clean_restart':second.name,'unclean_run':hold.name,'unclean_restart':final.name,'same_committed_state':True,'requests':dict(counts),'profile':str(profile)}
        (final/'lifecycle.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2),flush=True)
    finally:
        server.shutdown();server.server_close()
        (profile/'requests.json').write_text(json.dumps(dict(counts),indent=2)+'\n')

if __name__=='__main__': main()
