"""Serve owned README demo pages and open Sol with an isolated showcase profile.

Use --capture for automatic isolated setup and capture. Never reads the daily profile.
The fixture is intentionally a website, not a recreation of the browser UI.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sqlite3
import subprocess
import json
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / 'artifacts' / 'readme-demo' / 'profile'
ORIGIN = 'http://127.0.0.1:47631'
STYLE = """*{box-sizing:border-box}body{margin:0;background:#f8f6ef;color:#292f29;font:15px 'Segoe UI',sans-serif}
header{display:flex;align-items:center;justify-content:space-between;padding:28px 6%;border-bottom:1px solid #deded1}
.brand{font:italic 30px Georgia,serif;letter-spacing:-1.2px}nav{display:flex;gap:28px}a{color:inherit;text-decoration:none}
main{padding:52px 6% 28px}.eyebrow{font-size:10px;letter-spacing:2.3px;text-transform:uppercase;color:#667563}
h1{font:normal clamp(48px,5.7vw,90px)/1.02 Georgia,serif;letter-spacing:-3px;margin:22px 0}
.intro{display:flex;justify-content:space-between;gap:30px;align-items:end;margin-bottom:34px}.intro p{font-size:14px;line-height:1.8;max-width:240px;color:#626b5f}
.landscape{height:300px;border-radius:5px;overflow:hidden;background:#b2c2aa}.landscape svg{width:100%;height:100%;object-fit:cover}
.caption{display:flex;justify-content:space-between;font-size:10px;letter-spacing:1.4px;padding:14px 0;color:#687361}
footer{display:flex;justify-content:space-between;border-top:1px solid #deded1;margin:10px 6% 0;padding:18px 0;font-size:10px;color:#787b70}
.note{background:#eee2d9}.note main{padding-top:44px}.note h1{font-size:clamp(46px,5vw,72px);max-width:600px}
.note p{font:16px/1.8 Georgia,serif;color:#72645d;max-width:530px}.card{margin:32px 0 20px;padding:25px;border-top:1px solid #bdae9e;border-bottom:1px solid #bdae9e}
.card div{padding:14px 0;border-bottom:1px solid #d7c7ba}.card div:last-child{border:0}.number{font:11px 'Segoe UI';margin-right:22px;color:#927361}
@media(max-width:700px){header{padding:24px 7%}nav{gap:16px;font-size:12px}.intro{display:block}.intro p{max-width:360px}.landscape{height:260px}main{padding-top:38px}.caption{font-size:9px}}
"""
LANDSCAPE = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1200 500" preserveAspectRatio="xMidYMid slice">
<defs><linearGradient id="sky" x2="0" y2="1"><stop stop-color="#b6c6ae"/><stop offset="1" stop-color="#e2dec1"/></linearGradient>
<linearGradient id="hill" x2="0" y2="1"><stop stop-color="#607e66"/><stop offset="1" stop-color="#2f5147"/></linearGradient></defs>
<path fill="url(#sky)" d="M0 0h1200v500H0z"/><circle cx="865" cy="140" r="69" fill="#f4eaca"/>
<path d="M0 300 160 173 270 235 420 110 650 300 820 220 1030 130 1200 260V500H0" fill="#8fa48c"/>
<path d="m0 350 230-130 170 100 170-120 230 170 180-100 220 80v150H0" fill="#78927b"/>
<path d="M0 390Q190 270 390 380T780 350T1200 350V500H0" fill="url(#hill)"/>
<path d="M690 500Q450 420 620 390T785 350" fill="none" stroke="#d5cba6" stroke-width="17"/>
<path d="M0 455Q190 380 410 465L500 500H0" fill="#294b40"/></svg>"""


def page(path):
    notes = path.startswith(('/notes', '/target'))
    title = 'Notes for the road' if notes else 'The slow travel guide' if path.startswith('/guide') else 'Fieldnotes — a little room to wander'
    main = ("""<div class="eyebrow">A notebook for your next chapter</div><h1>Leave room for<br>the unplanned.</h1>
<p>The best part of a trip rarely makes the itinerary. A quiet street. A long lunch. One more page before you go.</p>
<div class="card"><div><span class="number">01</span>Take the scenic route</div><div><span class="number">02</span>Find a place to pause</div><div><span class="number">03</span>Bring a little curiosity</div></div>
<p>Less to check off. More to take in.</p>""" if notes else f"""<div class="eyebrow">A little room to wander · Volume 01</div>
<div class="intro"><h1>Go somewhere<br>worth getting lost.</h1><p>Small adventures, open roads,<br>and a slower kind of weekend.<br><a href="/notes">Open the notebook ↗</a></p></div>
<div class="landscape">{LANDSCAPE}</div><div class="caption"><span>THE LONG WAY HOME</span><a href="/guide">Read the slow travel guide ↗</a></div>""")
    return f'''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{title}</title><link rel="icon" href="/favicon.svg"><style>{STYLE}</style><body class="{'note' if notes else ''}">
<header><a class="brand" href="{'http://localhost:47631/notes' if '/source' in path else '/'}">fieldnotes.</a><nav><a href="/guide">Journal</a><a href="/notes">The notebook ↗</a></nav></header>
<main>{main}</main><footer><span>Made for the moments in between.</span><span>Sol demo website · No personal data</span></footer></body></html>'''.encode()


def seed():
    database = PROFILE / 'BrowserData' / 'workspace.sqlite3'
    if not database.exists():
        raise SystemExit('Run without --seed first, close its Sol window, then rerun with --seed.')
    with sqlite3.connect(database) as db:
        if db.execute('SELECT count(*) FROM nodes').fetchone()[0] > 1:
            raise SystemExit('Showcase already populated; leaving it unchanged.')
        explore = db.execute('SELECT active_space FROM state').fetchone()[0]
        studio = str(uuid.uuid4())
        db.execute("UPDATE spaces SET name='Explore',icon='🌿',theme_hue=145,theme_intensity=70,theme_noise=12")
        db.execute("INSERT INTO spaces(id,name,icon,position,theme_hue,theme_intensity,theme_mode,theme_noise) VALUES(?, 'Studio','✳',1,275,90,1,18)", (studio,))
        db.execute('DELETE FROM nodes')
        now = int(time.time() * 1000)
        def add(title, url='', *, parent=None, folder=False, pinned=True, favorite=False, space=explore, position=0):
            node = str(uuid.uuid4())
            db.execute('INSERT INTO nodes(id,parent_id,kind,title,url,position,pinned,expanded,favorite,archived,page_title,custom_title,space_id,last_active_at) VALUES(?,?,?,?,?,?,?,1,?,0,?,1,?,?)',
                       (node,parent,int(folder),title,url,position,int(pinned),int(favorite),title,'' if favorite or parent else space,now))
            return node
        add('Fieldnotes', ORIGIN, favorite=True)
        add('MDN', 'https://developer.mozilla.org/', favorite=True, position=1)
        add('GitHub', 'https://github.com/sol-browser/sol', favorite=True, position=2)
        trip = add('A weekend away', folder=True)
        guide = add('The slow travel guide', ORIGIN+'/guide', parent=trip)
        add('Places to pause', ORIGIN+'/notes?places', parent=trip, position=1)
        add('A reading list', ORIGIN+'/notes?reading', position=1)
        home = add('A little room to wander', ORIGIN+'/journal', pinned=False)
        notes = add('Notes for the road', ORIGIN+'/notes', pinned=False, position=1)
        add('Ideas worth keeping', ORIGIN+'/notes?ideas', space=studio)
        add('Design references', ORIGIN+'/guide?design', space=studio, position=1)
        studio_home = add('The notebook', ORIGIN+'/notes', space=studio, pinned=False)
        # Positions are contiguous per parent across every section and Space.
        for position, (node,) in enumerate(db.execute('SELECT id FROM nodes WHERE parent_id IS NULL ORDER BY rowid').fetchall()):
            db.execute('UPDATE nodes SET position=? WHERE id=?',(position,node))
        db.execute('UPDATE state SET active_id=?,sidebar_width=256,revision=revision+1', (home,))
        db.execute('UPDATE spaces SET active_id=? WHERE id=?', (home,explore))
        db.execute('UPDATE spaces SET active_id=? WHERE id=?', (studio_home,studio))
        (PROFILE.parent/'scene.json').write_text(json.dumps(dict(explore=explore,studio=studio,home=home,notes=notes,guide=guide)),encoding='utf-8')
        print('Created synthetic Spaces, Favorites, folders and tabs.', flush=True)


def capture_scenes(executable, names):
    # Purpose-built repository capture harness, also used by native smoke tests.
    # No desktop-wide screenshots or injected OS input; only windows of our PID.
    from real_extensions_smoke import windows_of, capture
    from PIL import Image
    scene = json.loads((PROFILE.parent/'scene.json').read_text())
    capture_root = ROOT/'artifacts'/'readme-demo'/'captures'
    capture_root.mkdir(exist_ok=True)
    for name in names:
        directory = capture_root / (name+'-'+uuid.uuid4().hex[:6])
        profile = directory/'profile'
        data = profile/'BrowserData'
        data.mkdir(parents=True)
        (profile/'Default').mkdir()
        with sqlite3.connect(PROFILE/'BrowserData/workspace.sqlite3') as source, sqlite3.connect(data/'workspace.sqlite3') as db:
            source.backup(db)
            if name == 'split-view':
                db.execute('INSERT INTO splits VALUES(?,?,?,5000,0,0)',(scene['home'],scene['notes'],'Wander & wonder'))
            if name == 'dark-space':
                active=db.execute('SELECT active_id FROM spaces WHERE id=?',(scene['studio'],)).fetchone()[0]
                db.execute('UPDATE state SET active_space=?,active_id=?',(scene['studio'],active))
            if name in ('themes','link-peek'):
                db.execute('UPDATE nodes SET url=? WHERE id=?',(ORIGIN+('/theme' if name=='themes' else '/source?peek'),scene['home']))
            db.commit()
        (data/'window.json').write_text(json.dumps(dict(x=80,y=65,width=1440,height=940,maximized=False)))
        if name not in ('themes','link-peek'):
            homepage=ORIGIN+'/notes' if name=='dark-space' else ORIGIN+'/journal'
            preferences=dict(schema=1,searchEngine='google',downloadDirectory='',askDownload=False,startup='homepage',homepage=homepage,shortcuts={},backdrop='none')
            with sqlite3.connect(data/'settings.sqlite3') as settings:
                settings.executescript('PRAGMA application_id=1380271184; PRAGMA user_version=1; CREATE TABLE preferences(id INTEGER PRIMARY KEY CHECK(id=1),value TEXT NOT NULL);')
                settings.execute('INSERT INTO preferences VALUES(1,?)',(json.dumps(preferences),))
        report = directory/'native.jsonl'
        probe = name in ('themes','link-peek')
        command = [str(executable),'--workspace',f'--profile-dir={profile}']
        if probe: command += [f'--probe-report={report}']
        if name=='themes': command += ['--probe-theme']
        if name=='link-peek': command += ['--probe-link-headers']
        with (directory/'process.log').open('w') as log:
            process = subprocess.Popen(command,cwd=executable.parent,stdout=log,stderr=log)
            print(f'Capturing {name}: {directory}',flush=True)
            start = time.monotonic()
            frames=[]
            while process.poll() is None and time.monotonic()-start < (40 if probe else 8):
                elapsed=time.monotonic()-start
                if elapsed >= (.7 if name=='link-peek' else 2):
                    windows=[w for w in windows_of(process.pid) if w['title']=='Sol' and w['visible']]
                    if len(windows)==1:
                        bitmap=directory/'frame.bmp'
                        if capture(windows[0]['hwnd'],bitmap):
                            image=Image.open(bitmap).convert('RGB')
                            if name=='themes' or (name=='link-peek' and elapsed<=6):
                                target=directory/f'frame-{len(frames):03}.png'
                                image.save(target)
                                frames.append(dict(file=target.name,seconds=round(elapsed,2)))
                            if name!='themes' and 4 <= elapsed <= 6:
                                image.save(directory/'screen.png')
                time.sleep(.12 if name=='link-peek' else .16 if name=='themes' else .4)
            if process.poll() is None:
                subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True,check=True)
                process.wait(timeout=10)
                if probe: raise RuntimeError(f'Showcase exceeded its deadline: {name}')
        rows=[json.loads(line) for line in report.read_text().splitlines()] if report.exists() else []
        summary=dict(scene=name,exit_code=process.returncode,shutdown=any(r['event']=='shutdown' for r in rows),capture_only=not probe,frames=frames,
                     probe_results=[r['details'] for r in rows if r['event']=='probe_result'],
                     peek=[r['details'] for r in rows if r['event']=='link_headers_opened'],
                     theme_steps=[r['details'] for r in rows if r['event']=='theme_probe_step'])
        (directory/'capture.json').write_text(json.dumps(summary,indent=2))
        print(json.dumps({k:v for k,v in summary.items() if k!='frames'}),flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'build/Release/apps/desktop/Release/sol.exe')
    parser.add_argument('--seed', action='store_true')
    parser.add_argument('--capture', action='store_true',help='Capture real application scenes using the repository native harness (requires Pillow).')
    parser.add_argument('--scenes', nargs='+',choices=('overview','split-view','dark-space','themes','link-peek'),default=('overview','split-view','dark-space','themes','link-peek'))
    args = parser.parse_args()
    if args.seed:
        seed()
    class Fixture(BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            body = (b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32"><rect width="32" height="32" rx="8" fill="#3f6351"/><text x="10" y="25" font-family="Georgia" font-style="italic" font-size="28" fill="#f8f6ef">f</text></svg>' if self.path == '/favicon.svg' else page(self.path))
            self.send_response(200)
            self.send_header('Content-Type', 'image/svg+xml' if self.path == '/favicon.svg' else 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
    PROFILE.mkdir(parents=True, exist_ok=True)
    # Skip onboarding only in this synthetic profile.
    (PROFILE / 'Default').mkdir(exist_ok=True)
    with ThreadingHTTPServer(('127.0.0.1', 47631), Fixture) as server:
        if args.capture:
            threading.Thread(target=server.serve_forever,daemon=True).start()
            try:
                if not (PROFILE.parent/'scene.json').exists():
                    if not (PROFILE/'BrowserData/workspace.sqlite3').exists():
                        report=PROFILE.parent/'initialize.jsonl'
                        process=subprocess.Popen([str(args.exe.resolve()),'--workspace',f'--profile-dir={PROFILE}',f'--probe-report={report}',f'--probe-url={ORIGIN}'],cwd=args.exe.resolve().parent)
                        try: code=process.wait(timeout=40)
                        except subprocess.TimeoutExpired:
                            subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True,check=True)
                            raise RuntimeError('Showcase initialization timed out')
                        rows=[json.loads(line) for line in report.read_text().splitlines()]
                        if code or not any(row['event']=='shutdown' for row in rows) or not any(row['event']=='probe_result' and row['details']['pass'] for row in rows):
                            raise RuntimeError('Showcase initialization failed; inspect artifacts/readme-demo/initialize.jsonl')
                    seed()
                capture_scenes(args.exe.resolve(), args.scenes)
            finally: server.shutdown()
            return
        process = subprocess.Popen([str(args.exe.resolve()), '--workspace', f'--profile-dir={PROFILE}', ORIGIN], cwd=args.exe.resolve().parent)
        print(f'Showcase PID {process.pid}; profile {PROFILE}', flush=True)
        server.timeout = .5
        while process.poll() is None:
            server.handle_request()
        print(f'Sol exited: {process.returncode}', flush=True)


if __name__ == '__main__':
    main()
