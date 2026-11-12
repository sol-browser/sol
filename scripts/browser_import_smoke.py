"""Browser data import smoke against synthetic profiles only (never a real one).

1. Seeds a disposable Red profile A with passwords from the synthetic Chrome CSV
   (exercises Chromium's own password store through passwordsPrivate.addPassword).
2. Builds a synthetic "Chrome" User Data folder from A's real, Chromium-written
   Local State + Login Data (v10 DPAPI/AES-GCM) and the fixture Bookmarks,
   Sessions and History (visit times moved to the last hour).
3. Imports it into a fresh profile B, then repeats the import (nothing twice),
   then imports the synthetic Zen/Firefox logins (NSS key4.db).
4. Imports Arc from an MSIX-shaped folder and from a file, checks profile-scoped
   favicon caches and fills a missing icon when the workspace already exists.
5. Decrypts B's Login Data and checks no fixture password reached any log.

Usage: python scripts/browser_import_smoke.py [--exe path\\to\\sol.exe]
"""
import argparse, hashlib, json, os, shutil, sqlite3, subprocess, sys, time
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


def arc_favicon_db(profile: Path, shared_width: int) -> bytes:
    db = profile / 'Favicons'
    shutil.copy(FIXTURES / 'chromium' / 'Favicons', db)
    with sqlite3.connect(db) as c:
        c.execute("INSERT INTO icon_mapping(page_url,icon_id) VALUES('https://arc.example.test/',1)")
        c.execute('DELETE FROM favicon_bitmaps WHERE icon_id=1 AND width<>?', (shared_width,))
        return c.execute('SELECT image_data FROM favicon_bitmaps WHERE icon_id=1').fetchone()[0]


def stored_favicons(db: Path) -> dict[str, bytes]:
    if not db.is_file():
        return {}
    with sqlite3.connect(db.as_uri() + '?mode=ro', uri=True) as c:
        return dict(c.execute('SELECT host,png FROM favicons'))


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

    arc_root = out / 'Packages' / 'TheBrowserCompany.Arc_fixture' / 'LocalCache' / 'Local' / 'Arc'
    shutil.copytree(FIXTURES / 'arc', arc_root)
    arc_profile = arc_root / 'User Data' / 'Profile 1'
    arc_profile.mkdir(parents=True)
    arc_default = arc_profile.parent / 'Default'
    arc_default.mkdir()
    work_icon = arc_favicon_db(arc_profile, 32)
    home_icon = arc_favicon_db(arc_default, 16)
    sidebar_path = arc_root / 'StorableSidebar.json'
    sidebar = json.loads(sidebar_path.read_text(encoding='utf-8'))
    items = sidebar['sidebar']['containers'][3]['items']
    for item in items[1::2]:
        if item['id'] in ('paper', 'work-fav'):
            host = 'sidebar-icon.example.test' if item['id'] == 'paper' else 'favorite-icon.example.test'
            item['data']['tab']['savedURL'] = f'https://{host}/'
    sidebar_path.write_text(json.dumps(sidebar, ensure_ascii=False), encoding='utf-8')
    archive_path = arc_root / 'StorableArchiveItems.json'
    archive = json.loads(archive_path.read_text(encoding='utf-8'))
    archive['items'][1]['sidebarItem']['data']['tab']['savedURL'] = 'https://archive-icon.example.test/'
    archive_path.write_text(json.dumps(archive), encoding='utf-8')
    for folder, item, icon in [('SidebarItemsFaviconCache', 'paper', home_icon), ('SidebarItemsFaviconCache', 'work-fav', work_icon),
                               ('ArchiveItemsFaviconCache', 'archived-one', home_icon)]:
        cache = arc_root / folder
        cache.mkdir(exist_ok=True)
        (cache / hashlib.md5(item.encode(), usedforsecurity=False).hexdigest()).write_bytes(icon)
    with sqlite3.connect(arc_profile / 'Favicons') as db:
        db.execute("INSERT INTO icon_mapping(page_url,icon_id) VALUES('https://sidebar-icon.example.test/',1)")
    shutil.copy(FIXTURES / 'chromium' / 'Bookmarks', arc_profile)
    shutil.copytree(FIXTURES / 'chromium' / 'Sessions', arc_profile / 'Sessions')
    arc = probe(exe, out / 'profileD', out / 'd1.jsonl', f'arc|{arc_profile}|tabs,bookmarks')
    counts = dict(spaces=3, tabs=8, bookmarks=3, folders=2, favorites=2, splits=1, archived=2)
    checks['arc_preview_and_commit'] = all(arc.get(stage, {}).get(key) == value for stage in ('preview', 'report')
                                           for key, value in counts.items()) and not arc.get('report', {}).get('errors')
    checks['arc_preview_reports_skipped'] = any(note.startswith('4 Arc sidebar entries') for note in arc.get('preview', {}).get('notes', []))
    checks['arc_favicons_preview_and_commit'] = arc.get('preview', {}).get('favicons') == arc.get('report', {}).get('favicons') == 6
    checks['arc_favicons_available_to_cold_tabs'] = arc.get('coldIcons', 0) >= 6
    arc_file = probe(exe, out / 'profileE', out / 'e1.jsonl', f'arc-sidebar|{sidebar_path}|tabs').get('report', {})
    checks['arc_file_import'] = arc_file.get('spaces') == 3 and arc_file.get('tabs') == 8 and arc_file.get('bookmarks') == 0 and arc_file.get('archived') == 0 and not arc_file.get('errors')
    checks['arc_file_reads_no_adjacent_caches'] = arc_file.get('favicons') == 0
    with sqlite3.connect(out / 'profileD' / 'BrowserData' / 'workspace.sqlite3') as db:
        spaces = db.execute("SELECT name,profile_id,id FROM spaces WHERE name LIKE 'Arc %' ORDER BY position").fetchall()
        checks['arc_persisted_spaces'] = ([row[0] for row in spaces] == ['Arc Work', 'Arc Home', 'Arc Empty'] and
                                         spaces[0][1] == spaces[2][1] != spaces[1][1] and all(row[1] != 'default' for row in spaces))
        checks['arc_persisted_tree'] = db.execute('''SELECT leaf.title, leaf.custom_title, leaf.page_title, parent.title, root.title, root.expanded
            FROM nodes leaf JOIN nodes parent ON leaf.parent_id=parent.id JOIN nodes root ON parent.parent_id=root.id
            WHERE leaf.title='My paper' ''').fetchall() == [('My paper', 1, 'Article', 'Research', 'Projects', 1)]
        checks['arc_persisted_favorites'] = len(spaces) == 3 and db.execute('SELECT title,profile_id FROM nodes WHERE favorite ORDER BY position').fetchall() == [
            ('Work favorite', spaces[0][1]), ('Home favorite', spaces[1][1])]
        checks['arc_persisted_split'] = db.execute('SELECT title,layout FROM split_groups').fetchall() == [('Two pages', 'v(6000*0,4000*1)')]
        checks['arc_persisted_archive'] = len(spaces) == 3 and db.execute('SELECT title,space_id,archived_at FROM nodes WHERE archived ORDER BY position').fetchall() == [
            ('Closed page', spaces[1][2], 1724733947510), ('Closed again', spaces[0][2], 1724733950000)]
        active = db.execute('''SELECT spaces.name FROM state JOIN spaces ON state.active_space=spaces.id
                               WHERE singleton=1''').fetchone()
        checks['arc_keeps_existing_space_active'] = bool(active) and not active[0].startswith('Arc ')
    if checks['arc_persisted_spaces']:
        history_root = out / 'profileD' / 'BrowserData'
        work_db = history_root / f'Profile-{spaces[0][1]}' / 'history.sqlite3'
        home_db = history_root / f'Profile-{spaces[1][1]}' / 'history.sqlite3'
        work_icons, home_icons = stored_favicons(work_db), stored_favicons(home_db)
        checks['arc_favicons_isolated_by_profile'] = work_icons.get('arc.example.test') == work_icon and home_icons.get('arc.example.test') == home_icon and \
            'arc.example.test' not in favicon_hosts(out / 'profileD')
        checks['arc_favicons_read_sidebar_and_archive_caches'] = work_icons == {'arc.example.test': work_icon, 'docs.example.test': work_icon,
            'sidebar-icon.example.test': home_icon, 'favorite-icon.example.test': work_icon} and home_icons == {
            'arc.example.test': home_icon, 'archive-icon.example.test': home_icon}
        if work_db.is_file() and home_db.is_file():
            with sqlite3.connect(work_db) as db:
                db.execute("DELETE FROM favicons WHERE host='sidebar-icon.example.test'")
            with sqlite3.connect(home_db) as db:
                db.execute("UPDATE favicons SET png=? WHERE host='arc.example.test'", (work_icon,))
            arc_repeat = probe(exe, out / 'profileD', out / 'd2.jsonl', f'arc|{arc_profile}|tabs,bookmarks')
            report = arc_repeat.get('report', {})
            checks['arc_repeat_fills_missing_icons_without_adding_spaces'] = report.get('favicons') == 1 and 'spaces' not in report and \
                any('already imported' in error for error in report.get('errors', [])) and stored_favicons(work_db).get('sidebar-icon.example.test') == home_icon
            checks['arc_repeat_keeps_existing_icon'] = stored_favicons(home_db).get('arc.example.test') == work_icon
    for key in ('arc_favicons_isolated_by_profile', 'arc_favicons_read_sidebar_and_archive_caches',
                'arc_repeat_fills_missing_icons_without_adding_spaces', 'arc_repeat_keeps_existing_icon'):
        checks.setdefault(key, False)
    empty_path = out / 'empty-arc-sidebar.json'
    empty_path.write_text(json.dumps({'sidebar': {'containers': [{'global': {}}, {'spaces': ['empty', {'id': 'empty', 'title': 'Empty'}], 'items': []}]}}))
    arc_empty = probe(exe, out / 'profileF', out / 'f1.jsonl', f'arc-sidebar|{empty_path}|tabs').get('report', {})
    checks['arc_empty_space_commits'] = arc_empty.get('spaces') == 1 and arc_empty.get('tabs') == 0 and not arc_empty.get('errors')

    stored = json.loads(subprocess.run([str(counter), '--count-logins', str(out / 'profileB')], capture_output=True, text=True, encoding='utf-8').stdout)
    checks['store_has_4_synthetic'] = stored.get('count') == 4 and all(l['synthetic_password'] for l in stored['logins'])

    leaks = []
    for path in out.rglob('*'):
        if path.is_file() and (path.suffix in ('.jsonl', '.log') or path.name == 'cef.log'):
            text = path.read_text(encoding='utf-8', errors='ignore')
            leaks += [f'{path.name}:{s}' for s in SECRETS if s in text]
    checks['no_password_in_logs'] = not leaks

    result = {'directory': str(out), 'checks': checks, 'stored': stored, 'first': first, 'repeat': again, 'zen': zen, 'arc': arc, 'leaks': leaks}
    (out / 'result.json').write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps({'directory': str(out), 'checks': checks}, indent=2))
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
