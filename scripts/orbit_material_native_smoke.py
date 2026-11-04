"""Check v2 tint CSS on the actual CEF shell for Solid/Mica/Acrylic.

Owns a disposable profile; does not synthesize focus or claim DWM blur pixels.
"""
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import argparse
import json
from pathlib import Path
import sqlite3
import threading
import uuid
from smoke import ROOT,run

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--brightness',type=int,choices=range(101),default=65);args=parser.parse_args()
    base=ROOT/'artifacts'/('orbit-material-native-'+uuid.uuid4().hex[:8]);profile=base/'profile';base.mkdir()
    class Page(BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def do_GET(self):
            body=b'<!doctype html><title>Orbit material fixture</title><h1>Material tint acceptance</h1>'
            self.send_response(200);self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    server=ThreadingHTTPServer(('127.0.0.1',0),Page);threading.Thread(target=server.serve_forever,daemon=True).start();url=f'http://127.0.0.1:{server.server_port}/theme'
    results=[]
    try:
        initial,_=run(url,['--workspace'],profile=profile)
        encoded=f'2,3,380,66,0,0,{args.brightness},32,850,43,154,650,38,274,950,37,90'
        dbpath=profile/'BrowserData/workspace.sqlite3'
        with sqlite3.connect(dbpath) as db:
            db.execute('UPDATE spaces SET theme_hue=32,theme_intensity=42,theme_mode=1,theme_noise=31,theme_palette=? WHERE id=(SELECT active_space FROM state)',(encoded,))
            db.execute('UPDATE nodes SET url=? WHERE kind=0',(url,))
        (profile/'BrowserData/theme-cache').write_text('32 42 1 31\n'+encoded+'\n',encoding='utf8')
        for material,alpha in [('none',1),('mica',.5),('acrylic',.6)]:
            directory,rows=run(url,['--probe-theme-preview','--probe-backdrop='+material],profile=profile)
            shell=[r['details'] for r in rows if r['event']=='theme_preview_shell']
            saved=[r['details']['theme'] for r in rows if r['event']=='theme_preview_saved']
            checks={'actual_cef_crossfade':len(shell)==1 and shell[0].get('passed') is True and shell[0].get('spatial') is True,
                    'material_alpha':len(shell)==1 and shell[0].get('materialAlpha')==alpha and shell[0].get('backdrop')==material,
                    'controls_opaque':len(shell)==1 and shell[0].get('surfaceOpacity')=='1',
                    'v2_saved':len(saved)==1 and saved[0].get('palette',{}).get('version')==2 and saved[0]['palette'].get('rotation')==90 and saved[0]['palette']['circles'][0]['hue']==356,
                    'brightness_saved':len(saved)==1 and saved[0].get('palette',{}).get('fill')==args.brightness}
            with sqlite3.connect(f'file:{dbpath.as_posix()}?mode=ro',uri=True) as db:
                stored=db.execute('SELECT theme_palette FROM spaces WHERE id=(SELECT active_space FROM state)').fetchone()[0]
            checks['sqlite_saved']=stored==f'2,3,380,66,0,0,{args.brightness},356,850,43,154,650,38,274,950,37,90'
            result={'material':material,'passed':all(checks.values()),'checks':checks,'shell':shell,'directory':str(directory)}
            results.append(result)
            if not result['passed']:raise RuntimeError('Material checks failed: '+json.dumps(result))
    finally:
        receipt={'passed':len(results)==3 and all(r['passed'] for r in results),'brightness':args.brightness,'results':results,'directory':str(base),'limitations':['No active-window DWM blur pixel claim; browser CSS/compositor tint is verified separately from OS blur.']}
        (base/'result.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2),flush=True)
        server.shutdown();server.server_close()

if __name__=='__main__':main()
