"""Real CEF theme-preview acceptance, including a large disposable workspace.

Exercises controller -> native material -> trusted renderer -> final save and
restart. Does not synthesize foreground input or assert physical drag FPS.
Use palette_smoke.py separately for focus-gated interactive editor acceptance.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import sqlite3
import statistics
import threading
import uuid
from smoke import ROOT, run


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tabs',type=int,default=300)
    args=parser.parse_args()
    if not 0<=args.tabs<=1000:parser.error('--tabs must be between 0 and 1000')
    base=ROOT/'artifacts'/('theme-preview-acceptance-'+uuid.uuid4().hex[:8])
    profile=base/'profile'
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*_):pass
        def do_GET(self):
            body=b'<!doctype html><title>Theme preview fixture</title><body style="background:white">Theme preview fixture'
            self.send_response(200);self.send_header('Content-Type','text/html')
            self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        url=f'http://127.0.0.1:{server.server_port}/theme'
        initialized,_=run(url,['--workspace','--probe-backdrop=acrylic'],profile=profile)
        database=profile/'BrowserData/workspace.sqlite3'
        with sqlite3.connect(database) as db:
            space=db.execute('SELECT active_space FROM state').fetchone()[0]
            for i in range(args.tabs):
                db.execute('INSERT INTO nodes(id,parent_id,kind,title,url,position,pinned,expanded,space_id) VALUES(?,NULL,0,?,?,?,0,0,?)',
                           (str(uuid.uuid4()),f'Theme fixture {i}',url,i+1,space))
            db.commit()
            before=db.execute('SELECT count(*) FROM nodes').fetchone()[0]
        measured,rows=run(url,['--probe-theme-preview','--probe-backdrop=acrylic'],profile=profile)
        timings=[r['details'] for r in rows if r['event']=='theme_preview_timing']
        shell=[r['details'] for r in rows if r['event']=='theme_preview_shell']
        saved=[r['details'] for r in rows if r['event']=='theme_preview_saved']
        with sqlite3.connect(database) as db:
            theme=db.execute('SELECT theme_hue,theme_intensity,theme_mode,theme_noise,theme_palette FROM spaces WHERE id=?',(space,)).fetchone()
            after=db.execute('SELECT count(*) FROM nodes').fetchone()[0]
        restarted,restart_rows=run(url,['--probe-theme-preview','--probe-backdrop=acrylic'],profile=profile)
        restored=[r['details'].get('theme') for r in restart_rows if r['event']=='theme_preview_startup']
        checks={'native_clean_runs':True,'ninety_previews':len(timings)==90,
                'workspace_size_retained':before==after==args.tabs+1,
                'renderer_applied_final_color':len(shell)==1 and shell[0].get('passed') is True,
                'renderer_updated_before_save':max((r['tick_ms'] for r in rows if r['event']=='theme_preview_shell'),default=1e30)<min((r['tick_ms'] for r in rows if r['event']=='theme_preview_saved'),default=0),
                'final_save_completed':len(saved)==1 and saved[0].get('steps')==90,
                'exact_sqlite_save':theme==(356,42,1,31,'1,3,580,68,0,0,76'),
                'exact_startup_restoration':len(restored)==1 and len(saved)==1 and restored[0]==saved[0].get('theme')}
        def stats(key):
            values=sorted(t[key] for t in timings)
            return {'p50':statistics.median(values),'p95':values[int(len(values)*.95)],'max':max(values)} if values else None
        result={'passed':all(checks.values()),'checks':checks,'nodes':after,
                'timings_ms':{k:stats(k) for k in ['apply_ms','publish_ms','snapshot_ms']},
                'runs':[str(p) for p in [initialized,measured,restarted]],'directory':str(base),
                'limitations':['No physical pointer/FPS claim; interactive palette acceptance is separate.']}
        (base/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2),flush=True)
        if not result['passed']:raise RuntimeError('Theme preview acceptance failed')
    finally:server.shutdown();server.server_close()


if __name__=='__main__':main()
