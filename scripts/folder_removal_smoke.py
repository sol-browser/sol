"""Real React/native folder confirmation, unload veto, closed pin and restart."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import sqlite3
import threading
import time
import uuid
from smoke import ROOT, run

def main():
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*_): pass
        def do_GET(self):
            media = self.path.startswith('/late-image')
            target = self.path.partition('?')[2]
            slow = target in ('first', 'closed-pin') and not media
            if target == 'abandoned':
                time.sleep(2.2)
            if slow:
                time.sleep(2.2 if target == 'closed-pin' else .65)
            if media:
                time.sleep(1)
                body=b'<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"/>'
            else:
                color = {'first': '#163e52', 'second': '#783b3c', 'closed-pin': '#2d6542'}.get(target, '#374d76')
                body = f"<!doctype html><title>Folder removal fixture</title><style>html{{background:{color};color:white}}</style>".encode()
                if target == 'closed-pin':
                    body += b"<body><script>setTimeout(() => document.body.append('Late text'), 850)</script>"
                else:
                    body += b"<h1>Folder removal fixture</h1><input value='unsaved'>"
                if slow:
                    body += b'<img src="/late-image?' + target.encode() + b'" alt="" hidden>'
            self.send_response(200); self.send_header('Content-Type','image/svg+xml' if media else 'text/html'); self.send_header('Content-Length',str(len(body)))
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
        separation=[row['details'] for row in rows if row['event']=='organization_split_separated']
        if separation!=[{'single_visible_page':True,'full_bounds':True}]:
            raise RuntimeError(f'Separate left stale native pane geometry: {separation}')
        # The two slow fixture pages, found by address: a time threshold also
        # caught ordinary pages that loaded slowly on a busy machine.
        def browser_for(target):
            at = next(i for i, row in enumerate(rows) if row['event']=='address_change' and row['details']['url'].endswith('?'+target))
            return next(row for row in reversed(rows[:at]) if row['event']=='embedded_activated')['details']['browser_id']
        slow_pages = {browser_for('first'), browser_for('closed-pin')}
        cold = [r for r in rows if r['event']=='page_transition_content_ready' and r['details']['browser_id'] in slow_pages and
                r['details']['ready_ms']>=400 and not r['details']['load_finished']]
        split_exits = {r['details']['browser_id'] for r in rows if r['event']=='embedded_activated' and r['details'].get('left_split')}
        assert len(cold)==2 and sum(r['details']['browser_id'] in split_exits for r in cold)==1, cold
        for r in cold:
            browser_id = r['details']['browser_id']
            activated = next(x for x in rows if x['event']=='embedded_activated' and x['details']['browser_id']==browser_id)
            gradient = next(x for x in reversed(rows[:rows.index(activated)]) if x['event']=='page_transition_gradient_shown')
            painted = next(x for x in rows if x['event']=='page_transition_painted' and x['details']['browser_id']==browser_id)
            shown = next(x for x in rows if x['event']=='page_transition_finished' and x['details']['browser_id']==browser_id and x['details']['revealed'])
            load_end = next(x for x in rows if x['event']=='load_end' and x['details']['browser_id']==browser_id)
            assert 0 <= activated['tick_ms'] - gradient['tick_ms'] < 500, (gradient, activated)
            assert r['tick_ms'] <= painted['tick_ms'] <= shown['tick_ms'] < load_end['tick_ms'], (r, painted, shown, load_end)
            if browser_id in split_exits:
                assert r['details']['source'] == 'firstPaint' and r['details']['ready_ms'] >= 1900, r
                assert load_end['tick_ms'] - shown['tick_ms'] > 550, (shown, load_end)
        canceled = [row['details']['browser_id'] for row in rows if row['event']=='page_transition_finished' and
                    row['details']['browser_id'] >= 0 and not row['details']['revealed']]
        assert any(not any(row['event'] in ('page_transition_painted', 'page_transition_content_ready') and
                           row['details']['browser_id'] == browser_id for row in rows) for browser_id in canceled), canceled
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
