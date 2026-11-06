"""Owned real navigation/link/popup acceptance; --manual leaves a disposable UI open."""
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
from urllib.parse import urlsplit
from smoke import ROOT, run

PAGE = b'''<!doctype html><html lang="en"><meta charset="utf-8"><title>Owned navigation fixture</title>
<style>body{font:16px Segoe UI;background:#fff;color:#222}h1{position:absolute;left:24px;top:5px;font-size:20px}input{position:absolute;left:24px;top:48px;width:300px;height:32px}a,button{position:absolute;left:24px;width:240px;height:28px;font:16px Segoe UI}#middle{top:96px}#ctrl{top:146px}#blank{top:196px}#popup{top:246px}#same{top:296px}</style>
<h1>Owned navigation fixture</h1><input aria-label="Unsaved fixture text" value="initial">
<a id="middle" href="/middle">Middle-click background link</a><a id="ctrl" href="/ctrl">Ctrl-click background link</a>
<a id="blank" href="/blank" target="_blank" rel="noopener">Open a foreground tab</a>
<button id="popup" onclick="window.open('/popup','red-owned-popup','popup,width=480,height=360')">Open owned popup</button>
<a id="same" href="/next">Navigate in this tab</a>
<script>addEventListener('message',e=>{if(e.origin===location.origin&&e.data==='owned-popup'){fetch('/opener-report');setTimeout(()=>e.source.postMessage('close',e.origin),700)}});</script></html>'''
POPUP = b'''<!doctype html><title>Owned popup fixture</title><h1>Owned popup</h1><p>Testing opener communication. This window closes itself.</p>
<script>fetch('/popup-report?opener='+!!opener+'&bridge='+typeof window.cefQuery);if(opener)opener.postMessage('owned-popup',location.origin);addEventListener('message',e=>{if(e.origin===location.origin&&e.data==='close')window.close()});</script>'''

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--manual', action='store_true'); parser.add_argument('--observe', action='store_true'); args=parser.parse_args()
    counts=Counter()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*_args): pass
        def do_GET(self):
            counts[self.path]+=1
            path=urlsplit(self.path).path
            if path=='/slow': time.sleep(3)
            if path=='/reload': time.sleep(.15)
            body=POPUP if path=='/popup' else PAGE
            self.send_response(200); self.send_header('Content-Type','text/html; charset=utf-8'); self.send_header('Cache-Control','no-store'); self.send_header('Content-Length',str(len(body))); self.end_headers()
            try:self.wfile.write(body)
            except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass  # Expected for Stop.
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
    url=f'http://127.0.0.1:{server.server_port}/fixture'
    try:
        if args.manual:
            directory=ROOT/'artifacts'/('manual-navigation-'+uuid.uuid4().hex[:8]);database=directory/'profile/BrowserData/workspace.sqlite3';database.parent.mkdir(parents=True)
            with sqlite3.connect(database) as db:
                db.executescript('CREATE TABLE nodes(id TEXT PRIMARY KEY,parent_id TEXT,kind INTEGER,title TEXT,url TEXT,position INTEGER,pinned INTEGER,expanded INTEGER);CREATE TABLE state(singleton INTEGER PRIMARY KEY,revision INTEGER,active_id TEXT);PRAGMA application_id=1380271170;PRAGMA user_version=1;')
                node=str(uuid.uuid4());db.execute('INSERT INTO nodes VALUES(?,NULL,0,?,?,0,0,1)',(node,'Navigation fixture',url));db.execute('INSERT INTO state VALUES(1,1,?)',(node,))
            exe=os.environ.get('RED_BROWSER_TEST_EXE',str(ROOT/'build/Release/apps/desktop/Release/sol.exe'))
            extra=[f'--probe-report={directory / "native.jsonl"}','--probe-input'] if args.observe else []
            with (directory/'process.log').open('w') as log:
                process=subprocess.Popen([exe,'--workspace',f'--profile-dir={directory / "profile"}',*extra],stdout=log,stderr=log,cwd=os.path.dirname(exe))
                print(json.dumps({'directory':str(directory),'url':url,'pid':process.pid}),flush=True);process.wait()
        else:
            directory,rows=run(url,['--probe-browsing'])
            required=['browsing_checks_pass','browsing_source_form_preserved','popup_bridge_absent','popup_closed']
            passed=all(any(r['event']==event for r in rows) for event in required)
            passed=passed and all(counts[p]==1 for p in ['/fixture','/fixture?saved','/middle','/ctrl','/blank','/popup','/typed'])
            passed=passed and counts['/popup-report?opener=true&bridge=undefined']==1 and counts['/opener-report']==1
            passed=passed and counts['/reload']==3 and counts['/slow']==1
            visibility=[r['details']['pass'] for r in rows if r['event']=='browsing_background_visibility']
            passed=passed and visibility==[True,True]
            passed=passed and [r['details']['pass'] for r in rows if r['event']=='browsing_input_ready']==[True,True]
            command_inputs=[r['details'] for r in rows if r['event']=='command_input_probe']
            passed=passed and len(command_inputs)==1 and command_inputs[0]['focused'] and command_inputs[0]['selected'] and command_inputs[0]['nativeRejected']==8
            passed=passed and command_inputs[0]['predictionChecks']==4
            geometry=[r['details'] for r in rows if r['event']=='command_layout_probe']
            passed=passed and len(geometry)==1 and geometry[0]['centeredX'] and geometry[0]['anchoredY']
            passed=passed and any(r['event']=='command_closed' for r in rows)
            passed=passed and all(any(r['event']==event and r['details']['pass'] for r in rows)
                                  for event in ['copy_focus_checked','notification_refresh_checked','notification_expiry_checked','command_cancel_focus_checked'])
            toasts=[r['details'] for r in rows if r['event']=='notification_shown' and r['details']['kind']=='copy']
            passed=passed and len(toasts)==2 and all(
                t['painted'] and t['focus_preserved'] and t['geometry_ok'] and
                t['animated']==toasts[0]['animated'] and
                abs(t['top_inset_dip']-12)<=1 and abs(t['right_inset_dip']-12)<=1 for t in toasts)
            passed=passed and any(r['event']=='renderer_token' and r['details'].get('role')=='command' and r['details'].get('restricted') and r['details'].get('integrity_rid')==0 for r in rows)
            result={'passed':passed,'requests':dict(counts),'run':directory.name,'checks':required}
            (directory/'navigation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2),flush=True)
            if not passed:raise RuntimeError('Navigation lifecycle, document counts or popup communication failed')
    finally:
        server.shutdown();server.server_close()
        if 'directory' in locals():(directory/'requests.json').write_text(json.dumps(dict(counts),indent=2)+'\n')
if __name__=='__main__':main()
