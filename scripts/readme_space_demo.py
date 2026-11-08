"""Record the real native Space editor in an isolated, instrumented runtime.

Requires Pillow and a seeded readme_demo.py workspace. Binaries and original UI
remain untouched. Only the staged index.html loads a capture-only DOM driver.
"""
import argparse
from datetime import datetime, timezone
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
from threading import Thread
import time
import uuid
from PIL import Image, ImageDraw
from readme_demo import ROOT, PROFILE, page
from real_extensions_smoke import capture, windows_of


def composite(pid, directory):
    windows = [w for w in windows_of(pid) if w['visible']]
    roots = [w for w in windows if w['title'] == 'Sol']
    if len(roots) != 1:
        return None
    root = roots[0]
    target = directory/'root.bmp'
    if not capture(root['hwnd'], target):
        return None
    image = Image.open(target).convert('RGB')
    for panel in (w for w in windows if w['title'] == 'Sol — Panel'):
        target = directory/'panel.bmp'
        if not capture(panel['hwnd'], target):
            continue
        region = Image.open(target).convert('RGB')
        # Reassemble actual owned-window captures at their native screen bounds.
        # No drawn UI, invented controls or desktop-wide capture.
        mask = Image.new('L', region.size)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, region.width-1, region.height-1), radius=8, fill=255)
        image.paste(region, (panel['rect'][0]-root['rect'][0], panel['rect'][1]-root['rect'][1]), mask)
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Release/apps/desktop/Release/sol.exe')
    args = parser.parse_args()
    executable = args.exe.resolve()
    if not (PROFILE.parent/'scene.json').exists():
        raise SystemExit('Run python scripts/readme_demo.py --capture first.')
    directory = ROOT/'artifacts/readme-demo/captures'/('space-setup-'+uuid.uuid4().hex[:6])
    runtime, profile = directory/'runtime', directory/'profile'
    runtime.mkdir(parents=True)
    # Stage immutable engine/application files as hardlinks; COPY mutable UI.
    for source in executable.parent.rglob('*'):
        relative = source.relative_to(executable.parent)
        if relative.parts[0] == 'ui' or not source.is_file():
            continue
        target = runtime/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        os.link(source, target)
    shutil.copytree(executable.parent/'ui', runtime/'ui')
    source_index = executable.parent/'ui/index.html'
    original_hash = hashlib.sha256(source_index.read_bytes()).hexdigest()
    index = runtime/'ui/index.html'
    index.write_text(index.read_text().replace('</body>', '<script type="module" src="/assets/readme-space-driver.js"></script></body>'), encoding='utf-8')
    shutil.copyfile(ROOT/'scripts/readme-space-driver.js', runtime/'ui/assets/readme-space-driver.js')
    data = profile/'BrowserData'
    data.mkdir(parents=True)
    (profile/'Default').mkdir()
    with sqlite3.connect(PROFILE/'BrowserData/workspace.sqlite3') as source, sqlite3.connect(data/'workspace.sqlite3') as db:
        source.backup(db)
        db.execute("UPDATE spaces SET theme_noise=0 WHERE id=(SELECT active_space FROM state)")
        db.commit()
    report = directory/'native.jsonl'
    class Fixture(BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            body = (b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32"><rect width="32" height="32" rx="8" fill="#3f6351"/><text x="10" y="25" font-family="Georgia" font-style="italic" font-size="28" fill="#f8f6ef">f</text></svg>' if self.path == '/favicon.svg' else page(self.path))
            self.send_response(200)
            self.send_header('Content-Type', 'image/svg+xml' if self.path == '/favicon.svg' else 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
    frames = []
    with ThreadingHTTPServer(('127.0.0.1', 47631), Fixture) as server:
        Thread(target=server.serve_forever, daemon=True).start()
        try:
            with (directory/'process.log').open('w') as log:
                process = subprocess.Popen([str(runtime/executable.name), '--workspace', '--probe-input', f'--profile-dir={profile}', f'--probe-report={report}'], cwd=runtime, stdout=log, stderr=log)
                print(f'Capturing native Space editor: {directory}', flush=True)
                start = time.monotonic()
                opened = None
                while process.poll() is None and time.monotonic()-start < 600:
                    elapsed = time.monotonic()-start
                    rows=[json.loads(line) for line in report.read_text().splitlines() if line.endswith('}')] if report.exists() else []
                    if opened is None and any(row['event']=='panel_open' for row in rows):
                        opened=elapsed
                    if (opened is not None and elapsed > opened+.45) or (1.1 < elapsed < 1.4):
                        image = composite(process.pid, directory)
                        if image:
                            name = f'frame-{len(frames):03}.png'
                            image.save(directory/name)
                            frames.append(dict(file=name, seconds=round(elapsed, 3)))
                    time.sleep(.10)
                if process.poll() is None:
                    subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'], capture_output=True, check=True)
                    process.wait(timeout=10)
                    raise RuntimeError(f'Native Space capture timed out: {directory}')
        finally:
            server.shutdown()
    rows = [json.loads(line) for line in report.read_text().splitlines()]
    milestones = [r['details'] for r in rows if r['event']=='panel_rendered' and 'readmeSpace' in r.get('details', {})]
    with sqlite3.connect(data/'workspace.sqlite3') as db:
        saved = db.execute('SELECT name,icon,theme_mode,theme_noise,theme_palette FROM spaces WHERE id=(SELECT active_space FROM state)').fetchone()
    checks = dict(
        editor_sequence=[m['readmeSpace'] for m in milestones]==['editor','name','emoji-picker','icon','palette','rotation','brightness','dark','light'],
        editor_foreground=bool(milestones) and all(m['focused'] for m in milestones) and any(r['event']=='popup_presented' and r['details'].get('foreground') and r['details'].get('keyboard_focus') for r in rows),
        saved_name_icon=bool(saved) and saved[:2]==('Weekend','🧭'),
        saved_theme=bool(saved) and saved[2:4]==(0,8) and saved[4].startswith('2,3,') and saved[4].endswith(',90'),
        clean_shutdown=process.returncode==0 and any(r['event']=='shutdown' for r in rows),
        native_security={'shell','content'} <= {r['details'].get('role') for r in rows if r['event']=='renderer_token' and r['details'].get('restricted')} and
            all(any(r['event']==event for r in rows) for event in ('content_bridge_absent','ipc_rejections_pass')),
        original_ui_unchanged=hashlib.sha256(source_index.read_bytes()).hexdigest()==original_hash,
        no_shell_error=not any(r['event']=='probe_shell_error' for r in rows),
    )
    result = dict(scene='space-setup',captured_on=datetime.now(timezone.utc).isoformat(),passed=all(checks.values()),checks=checks,frames=frames,milestones=milestones,saved=saved)
    (directory/'capture.json').write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='frames'},indent=2),flush=True)
    if not result['passed']:
        raise SystemExit(f'Space capture failed: {directory}')


if __name__=='__main__':
    main()
