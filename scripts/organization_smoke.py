"""Arc-inspired behavior acceptance through actual React, CEF and v1 migration."""
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import sqlite3
import threading
import uuid
from smoke import ROOT, run

# The legacy fixture must migrate to the current workspace schema.
CURRENT_SCHEMA = 12


def main():
    requests = Counter()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            if self.path.startswith('/fixture'): requests[self.path] += 1
            body = b"<!doctype html><html lang='en'><title>Organization fixture</title><h1>Real native page</h1><label>Unsaved text<input value='initial'></label><a href='/fixture?browsed'>Browse somewhere</a>"
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    profile = ROOT/'artifacts'/('organization-profile-'+uuid.uuid4().hex[:8])
    database = profile/'BrowserData/workspace.sqlite3'
    database.parent.mkdir(parents=True)
    url = f'http://127.0.0.1:{server.server_port}/fixture'
    # Our old public app schema, not any Arc format. Exercise migration in CEF.
    legacy_start = '11111111-1111-4111-8111-111111111111'
    legacy_folder = '22222222-2222-4222-8222-222222222222'
    with sqlite3.connect(database) as db:
        db.executescript('CREATE TABLE nodes(id TEXT PRIMARY KEY,parent_id TEXT,kind INTEGER,title TEXT,url TEXT,position INTEGER,pinned INTEGER,expanded INTEGER);'
            'CREATE TABLE state(singleton INTEGER PRIMARY KEY,revision INTEGER,active_id TEXT); PRAGMA application_id=1380271170; PRAGMA user_version=1;')
        db.execute('INSERT INTO nodes VALUES(?,NULL,0,?,?,0,0,1)', (legacy_start,'Start',url))
        db.execute("INSERT INTO nodes VALUES(?,NULL,1,'Legacy folder','',1,0,0)", (legacy_folder,))
        db.execute('INSERT INTO state VALUES(1,1,?)', (legacy_start,))
    def saved():
        with sqlite3.connect(database) as db:
            return {'schema': db.execute('PRAGMA user_version').fetchone()[0],
                'nodes': db.execute('SELECT * FROM nodes ORDER BY id').fetchall(),
                'state': db.execute('SELECT * FROM state').fetchall()}
    try:
        first, rows = run(url, ['--probe-organization'], profile=profile)
        before = saved()
        decisions = [r['details']['leave'] for r in rows if r['event'] == 'organization_beforeunload_decision']
        result = {'passed': False, 'first_run': first.name, 'decisions': decisions, 'requests_first': dict(requests)}
        expected = {'/fixture': 1, '/fixture?saved': 2, '/fixture?away': 1, '/fixture?new': 1, '/fixture?browsed': 3, '/fixture?protected': 1, '/fixture?favorite': 1}
        command_inputs = [r['details'] for r in rows if r['event'] == 'command_input_probe']
        assert len(command_inputs) == 1 and command_inputs[0]['focused'] and command_inputs[0]['selected'] and command_inputs[0]['initial'] == '' and command_inputs[0]['nativeRejected'] == 8, command_inputs
        assert command_inputs[0]['predictionChecks'] == 4, command_inputs
        geometry = [r['details'] for r in rows if r['event'] == 'command_layout_probe']
        assert len(geometry) == 1 and geometry[0]['centeredX'] and geometry[0]['anchoredY'], geometry
        assert any(r['event'] == 'command_closed' for r in rows), 'Owned command browser did not close'
        # Scope lifecycle assertions to the tested selection. Other menus may be
        # opened and cancelled independently while the visible probe is running.
        rename_index = next(i for i,r in enumerate(rows) if r['event'] == 'sidebar_menu_action' and r['details']['action'] == 'rename')
        open_index = max(i for i in range(rename_index) if rows[i]['event'] == 'sidebar_menu_open')
        close_index = next(i for i in range(rename_index + 1, len(rows)) if rows[i]['event'] == 'sidebar_menu_closed')
        assert sum(r['event'] == 'sidebar_menu_shown' for r in rows[open_index:close_index + 1]) == 1
        assert not any(r['event'] == 'sidebar_menu_open' for r in rows[rename_index + 1:close_index])
        organization_state = next(r['details'] for r in rows if r['event'] == 'organization_checks_pass')
        favorite = next(n['id'] for n in organization_state['nodes'] if n['title'] == 'Mail')
        saved_page = next(n['id'] for n in organization_state['nodes'] if n['title'] == 'Saved page')
        assert any(r['event'] == 'organization_tab_drag' and
                   r['details'] == {'id': favorite, 'active': favorite, 'previous': saved_page} for r in rows)
        renamed_id = next(n['id'] for n in organization_state['nodes'] if n['title'] == 'Project reference')
        assert rows[open_index]['details']['id'] == renamed_id
        if decisions != [False, True] or requests != expected:
            (first/'organization.json').write_text(json.dumps(result,indent=2)+'\n')
            raise RuntimeError(f'Unexpected page lifecycle or beforeunload: {result}')
        second, _ = run(url, ['--probe-workspace'], profile=profile)
        after = saved()
        with sqlite3.connect(database) as db:
            legacy = db.execute('SELECT id,pinned,expanded FROM nodes WHERE id=?', (legacy_folder,)).fetchone()
            start = db.execute('SELECT id FROM nodes WHERE id=?', (legacy_start,)).fetchone()
        result.update({'second_run': second.name, 'passed': before == after and before['schema'] == CURRENT_SCHEMA and
            legacy == (legacy_folder,1,0) and start == (legacy_start,) and requests == {**expected,'/fixture?saved':3},
            'same_saved_state_after_restart': before == after, 'requests_total': dict(requests), 'saved': after})
        (second/'organization.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2),flush=True)
        if not result['passed']: raise RuntimeError('Migration, restart or lazy restore failed')
    finally:
        server.shutdown(); server.server_close()

if __name__ == '__main__': main()
