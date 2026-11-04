"""Real React/native folder confirmation, unload veto, closed pin and restart."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import sqlite3
import threading
import uuid
from smoke import ROOT, run

def main():
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*_): pass
        def do_GET(self):
            body=b"<!doctype html><title>Folder removal fixture</title><h1>Folder removal fixture</h1><input value='unsaved'>"
            self.send_response(200); self.send_header('Content-Type','text/html'); self.send_header('Content-Length',str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    profile=ROOT/'artifacts'/('folder-removal-profile-'+uuid.uuid4().hex[:8])
    database=profile/'BrowserData/workspace.sqlite3'; database.parent.mkdir(parents=True)
    url=f'http://127.0.0.1:{server.server_port}/folder-removal'
    initial='11111111-1111-4111-8111-111111111111'
    with sqlite3.connect(database) as db:
        db.executescript('CREATE TABLE nodes(id TEXT PRIMARY KEY,parent_id TEXT,kind INTEGER,title TEXT,url TEXT,position INTEGER,pinned INTEGER,expanded INTEGER);'
                        'CREATE TABLE state(singleton INTEGER PRIMARY KEY,revision INTEGER,active_id TEXT); PRAGMA application_id=1380271170; PRAGMA user_version=1;')
        db.execute('INSERT INTO nodes VALUES(?,NULL,0,?,?,0,0,1)',(initial,'Start',url))
        db.execute('INSERT INTO state VALUES(1,0,?)',(initial,))
    def saved():
        with sqlite3.connect(database) as db: return db.execute('SELECT id,parent_id,pinned,archived FROM nodes ORDER BY id').fetchall()
    try:
        first,rows=run(url,['--probe-organization'],profile=profile)
        if not any(row['event']=='organization_checks_pass' for row in rows): raise RuntimeError('Folder interaction checks missing')
        decisions=[row['details']['leave'] for row in rows if row['event']=='organization_beforeunload_decision']
        if decisions!=[False,True]: raise RuntimeError('Real beforeunload cancellation/acceptance missing')
        before=saved()
        second,_=run(url,['--workspace'],profile=profile)
        after=saved()
        result={'passed':before==after,'first_run':str(first),'restart':str(second),'profile':str(profile),
                'beforeunload':decisions,'saved_rows':after}
        (second/'folder-removal.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2))
        if not result['passed']: raise RuntimeError('Removal did not survive restart')
    finally: server.shutdown(); server.server_close()

if __name__=='__main__': main()
