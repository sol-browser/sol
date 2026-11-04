"""Browser data import smoke against synthetic profiles only (never a real one).

1. Seeds a disposable Red profile A with passwords from the synthetic Chrome CSV
   (exercises Chromium's own password store through passwordsPrivate.addPassword).
2. Builds a synthetic "Chrome" User Data folder from A's real, Chromium-written
   Local State + Login Data (v10 DPAPI/AES-GCM) and the fixture Bookmarks,
   Sessions and History (visit times moved to the last hour).
3. Imports it into a fresh profile B, then repeats the import (nothing twice),
   then imports the synthetic Zen/Firefox logins (NSS key4.db).
4. Decrypts B's Login Data and checks no fixture password reached any log.

Usage: python scripts/browser_import_smoke.py [--exe path\\to\\sol.exe]
"""
import argparse, json, os, shutil, sqlite3, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / 'tests' / 'fixtures' / 'import'
SECRETS = ['p,w"1', 'correct-horse-1', 'aes-secret', 'des-ëxtra', 'line1']


def probe(exe: Path, profile: Path, report: Path, spec: str) -> dict:
    subprocess.run([str(exe), '--workspace', f'--profile-dir={profile}', f'--probe-report={report}', f'--probe-import={spec}'],
                   cwd=exe.parent, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=180)
    events = [json.loads(line) for line in report.read_text(encoding='utf-8').splitlines() if line.strip()]
    found = [e['details'] for e in events if e.get('event') == 'browser_import_probe']
    return found[-1] if found else {'error': 'no probe result'}


def favicon_hosts(profile: Path) -> set[str]:
    """Hosts with a stored site icon in Sol's own history database."""
    hosts: set[str] = set()
    for db in (profile / 'BrowserData').glob('*.sqlite3'):
        with sqlite3.connect(f'file:{db}?mode=ro', uri=True) as c:
            if c.execute("SELECT 1 FROM sqlite_master WHERE name='favicons'").fetchone():
                hosts |= {row[0] for row in c.execute('SELECT host FROM favicons')}
    return hosts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', default=os.environ.get('RED_BROWSER_TEST_EXE', ROOT / 'build/Release/apps/desktop/Release/sol.exe'))
    args = parser.parse_args()
    exe = Path(args.exe).resolve(); counter = exe.parent / 'browser_import_test.exe'
    out = ROOT / 'artifacts' / f'import-smoke-{int(time.time())}'; out.mkdir(parents=True)
    checks: dict[str, bool] = {}

    seed = probe(exe, out / 'profileA', out / 'a.jsonl', f'csv|{FIXTURES / "exports" / "chrome.csv"}|passwords')
    checks['seed_added_2'] = seed.get('report', {}).get('passwords', {}).get('added') == 2

    synth = out / 'chrome-synth' / 'User Data'; (synth / 'Default').mkdir(parents=True)
    shutil.copy(out / 'profileA' / 'Local State', synth)
    shutil.copy(out / 'profileA' / 'Default' / 'Login Data', synth / 'Default')
    for name in ('Bookmarks', 'History', 'Favicons'): shutil.copy(FIXTURES / 'chromium' / name, synth / 'Default')
    shutil.copytree(FIXTURES / 'chromium' / 'Sessions', synth / 'Default' / 'Sessions')
    (synth / 'Default' / 'Preferences').write_text('{}')
    recent = (int(time.time() * 1000) - 3600_000 + 11644473600000) * 1000
    with sqlite3.connect(synth / 'Default' / 'History') as db:
        db.execute('UPDATE visits SET visit_time=?+id*60000000 WHERE url!=3', (recent,))

    spec = f'chrome|{synth / "Default"}|tabs,bookmarks,history,passwords'
    first = probe(exe, out / 'profileB', out / 'b1.jsonl', spec)
    r = first.get('report', {})
    checks['chrome_tree'] = r.get('tabs') == 2 and r.get('bookmarks') == 3 and r.get('spaces') == 1
    checks['chrome_history'] = (r.get('history') or 0) >= 1
    checks['chrome_favicons'] = r.get('favicons') == 1 and favicon_hosts(out / 'profileB') >= {'docs.example.test'}
    checks['chrome_passwords_added_2'] = r.get('passwords', {}).get('added') == 2 and not r.get('errors')

    again = probe(exe, out / 'profileB', out / 'b2.jsonl', spec).get('report', {})
    checks['repeat_kept'] = again.get('passwords', {}).get('existing') == 2 and again.get('passwords', {}).get('added') == 0 and \
        any('already imported' in e for e in again.get('errors', []))

    zen = probe(exe, out / 'profileB', out / 'b3.jsonl', f'zen|{FIXTURES / "firefox"}|passwords').get('report', {})
    checks['zen_passwords_added_2'] = zen.get('passwords', {}).get('added') == 2

    # Full Zen path (tabs/Spaces/bookmarks, history, cookies, passwords) in profile C.
    zsrc = out / 'zen-source'; zsrc.mkdir()
    for name in ('logins.json', 'key4.db', 'favicons.sqlite'): shutil.copy(FIXTURES / 'firefox' / name, zsrc)
    tab = lambda key, space, **extra: {'entries': [{'url': f'https://zen.example.test/{key}', 'title': key}], 'index': 1, 'zenSyncId': key, 'zenWorkspace': space, **extra}
    session = {'windows': [{'spaces': [{'uuid': 'work', 'name': 'Zen Work'}, {'uuid': 'home', 'name': 'Zen Home'}],
        'folders': [{'id': 'project', 'name': 'Project', 'workspaceId': 'work'}],
        'tabs': [tab('favorite', 'work', zenEssential=True, pinned=True), tab('nested', 'work', groupId='project', pinned=True),
                 tab('today', 'work'), tab('home', 'home'), tab('private', 'home', isPrivate=True)]}],
        'cookies': [{'host': 'zen.example.test', 'name': 'red_session', 'value': 'session-token', 'path': '/', 'sameSite': 1, 'originAttributes': {}}]}
    raw = json.dumps(session).encode(); ext = bytearray(); rest = len(raw) - 15
    while rest >= 255: ext.append(255); rest -= 255
    ext.append(rest)
    (zsrc / 'sessionstore.jsonlz4').write_bytes(b'mozLz40\0' + len(raw).to_bytes(4, 'little') + b'\xf0' + ext + raw)
    with sqlite3.connect(zsrc / 'cookies.sqlite') as db:
        db.execute('CREATE TABLE moz_cookies(host,name,value,path,isSecure,isHttpOnly,sameSite,expiry,originAttributes,isPartitionedAttributeSet,lastAccessed)')
        db.execute('INSERT INTO moz_cookies VALUES(?,?,?,?,?,?,?,?,?,?,?)', ('zen.example.test', 'red_zen', 'cookie-token', '/', 1, 1, 1, int(time.time()) + 86400, '', 0, 1))
    with sqlite3.connect(zsrc / 'places.sqlite') as db:
        db.executescript('CREATE TABLE moz_places(id INTEGER PRIMARY KEY,url,title); CREATE TABLE moz_bookmarks(id,parent,type,title,fk,position); CREATE TABLE moz_historyvisits(place_id,visit_date);'
                         "INSERT INTO moz_places VALUES(1,'https://zen.example.test/bookmark','Bookmark');"
                         "INSERT INTO moz_bookmarks VALUES(1,0,2,'Root',NULL,0),(2,1,2,'Toolbar',NULL,0),(3,2,1,'Bookmark',1,0);")
        db.execute('INSERT INTO moz_historyvisits VALUES(1,?)', (int((time.time() - 120) * 1000000),))
    zen_full = probe(exe, out / 'profileC', out / 'c1.jsonl', f'zen|{zsrc}|tabs,history,cookies,passwords').get('report', {})
    checks['zen_full'] = zen_full.get('tabs') == 4 and zen_full.get('bookmarks') == 1 and zen_full.get('spaces') == 2 and \
        zen_full.get('history') == 1 and zen_full.get('cookies', {}).get('written') == 2 and zen_full.get('passwords', {}).get('added') == 2 and zen_full.get('favicons') == 1

    stored = json.loads(subprocess.run([str(counter), '--count-logins', str(out / 'profileB')], capture_output=True, text=True, encoding='utf-8').stdout)
    checks['store_has_4_synthetic'] = stored.get('count') == 4 and all(l['synthetic_password'] for l in stored['logins'])

    leaks = []
    for path in out.rglob('*'):
        if path.is_file() and (path.suffix in ('.jsonl', '.log') or path.name == 'cef.log'):
            text = path.read_text(encoding='utf-8', errors='ignore')
            leaks += [f'{path.name}:{s}' for s in SECRETS if s in text]
    checks['no_password_in_logs'] = not leaks

    result = {'directory': str(out), 'checks': checks, 'stored': stored, 'first': first, 'repeat': again, 'zen': zen, 'leaks': leaks}
    (out / 'result.json').write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps({'directory': str(out), 'checks': checks}, indent=2))
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
