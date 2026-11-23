"""Floating sidebar acceptance (--probe-sidebar-peek) through actual CEF."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
import json
import os
import shutil
import sqlite3
import threading
from uuid import uuid4
import wave
from smoke import ROOT, run


PHASES = (('detect', 'entered', 'peek_show_requested'), ('snapshot', 'peek_show_requested', 'peek_snapshot_sent'),
          ('react', 'peek_snapshot_sent', 'peek_ui_ready'), ('frame', 'peek_ui_ready', 'peek_ready_to_slide'),
          ('place', 'peek_ready_to_slide', 'peek_placed_over_sample'), ('compose', 'peek_placed_over_sample', 'painted'),
          ('reveal', 'peek_ready_to_slide', 'painted'))


def phases(rows, entered, painted):
    """Split one pointer entry into native pipeline phases (all QPC ms)."""
    marks = {'entered': entered, 'painted': painted}
    for row in rows:
        ms = row.get('details', {}).get('ms')
        if row['event'] in ('peek_show_requested', 'peek_snapshot_sent', 'peek_ui_ready', 'peek_ready_to_slide',
                            'peek_placed_over_sample') and \
                ms is not None and entered - 5 <= ms <= painted and row['event'] not in marks:
            marks[row['event']] = ms
            if row['event'] == 'peek_snapshot_sent':
                marks['bytes'] = row['details'].get('bytes')
    out = {name: round(marks[end] - marks[start], 1) if start in marks and end in marks else None
           for name, start, end in PHASES}
    out['total'] = round(painted - entered, 1)
    out['snapshot_bytes'] = marks.get('bytes')
    return out


def seed(profile, url, tabs):
    database = profile / 'BrowserData' / 'workspace.sqlite3'
    database.parent.mkdir(parents=True)
    with sqlite3.connect(database) as db:
        db.executescript('CREATE TABLE nodes(id TEXT PRIMARY KEY,parent_id TEXT,kind INTEGER,title TEXT,url TEXT,position INTEGER,pinned INTEGER,expanded INTEGER,favorite INTEGER,archived INTEGER);'
                         'CREATE TABLE state(singleton INTEGER PRIMARY KEY,revision INTEGER,active_id TEXT);'
                         'PRAGMA application_id=1380271170; PRAGMA user_version=2;')
        base = url.removesuffix('/a')
        for position in range(tabs + 1):
            node = f'22222222-2222-4222-8222-{position:012d}'
            title, address = ('Start', url) if position == 0 else (f'Saved tab {position}', f'{base}/t{position}')
            db.execute('INSERT INTO nodes VALUES(?,?,?,?,?,?,?,?,?,?)',
                       (node, None, 0, title, address, position, int(0 < position <= 8), 1, 0, 0))
            if position == 0:
                db.execute('INSERT INTO state VALUES(1,1,?)', (node,))


def cold(host, url, wait, repeat, tabs):
    """First opening of each fresh process after |wait| ms idle."""
    template = None
    if tabs:
        # An existing, already migrated profile, copied for every process (the
        # probe changes its tabs). Only its workspace database is kept.
        template = ROOT / 'artifacts' / ('sidebar-cold-' + uuid4().hex[:8])
        seed(template / 'seed', url, tabs)
        run(url, ['--workspace'], profile=template / 'seed')
    samples = []
    for index in range(repeat):
        profile = None
        if template:
            profile = template / f'run{index}'
            (profile / 'BrowserData').mkdir(parents=True)
            shutil.copy2(template / 'seed' / 'BrowserData' / 'workspace.sqlite3', profile / 'BrowserData' / 'workspace.sqlite3')
        directory, rows = run(url, [f'--probe-sidebar-peek={host}-cold-{wait}'], allow_failure=True, profile=profile)
        found = [r['details'] for r in rows if r['event'] == 'peek_edge_measurement']
        if len(found) != 1 or found[0]['error'] or len(found[0]['edge_to_pixel_ms']) != 1:
            samples.append({'error': found[0]['error'] if found else 'missing measurement', 'run': str(directory)})
            continue
        sample = phases(rows, found[0]['entered_ms'][0], found[0]['painted_ms'][0])
        sample['child'] = found[0].get('child'); sample['run'] = directory.name
        startup = {r['event']: r['details']['ms'] for r in rows if r['event'] in ('peek_create_requested', 'peek_created', 'peek_subscribed')}
        sample['subscribed_before_entry_ms'] = round(found[0]['entered_ms'][0] - startup['peek_subscribed'], 1) if 'peek_subscribed' in startup else None
        samples.append(sample)
        print(json.dumps(sample), flush=True)
    result = {'host': host, 'wait_ms': wait, 'tabs': tabs, 'samples': samples}
    totals = sorted(s['total'] for s in samples if 'total' in s)
    result['median_total_ms'] = totals[len(totals) // 2] if totals else None
    result['passed'] = len(totals) == repeat and all(s['child'] == (host == 'child') for s in samples)
    out = ROOT / 'artifacts' / f'sidebar-cold-{host}-{uuid4().hex[:8]}.json'
    out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'samples'}), out, flush=True)
    if not result['passed']:
        raise RuntimeError(f'Cold experiment incomplete: {out}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', choices=('owned', 'child'), help='Compare native hosts on an interactive display')
    parser.add_argument('--edge', action='store_true', help='Measure 15 pointer entries against a solid page')
    parser.add_argument('--cold', type=int, metavar='MS', help='Measure the first opening of each process after MS idle')
    parser.add_argument('--repeat', type=int, default=5, help='Processes for --cold')
    parser.add_argument('--tabs', type=int, default=0, help='Extra saved tabs in the --cold profile')
    parser.add_argument('--space-creator', action='store_true', help='Check Create Space with six Favorites and playing media in both sidebars')
    parser.add_argument('--tooltips', action='store_true', help='Move the real cursor from the sidebar tooltip into the native page in both hosts')
    args = parser.parse_args()
    host = args.host
    if (args.edge or args.cold) and not host:
        parser.error('--edge and --cold require --host')
    if args.cold:
        args.edge = True  # Same solid fixture page.
    tone = BytesIO()
    if args.space_creator:
        with wave.open(tone, 'wb') as audio:
            audio.setparams((1, 1, 8000, 0, 'NONE', 'not compressed'))
            audio.writeframes(b'\x80' * 8000)
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            if args.space_creator and self.path == '/silence.wav':
                body = tone.getvalue()
                self.send_response(200); self.send_header('Content-Type', 'audio/wav')
                self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
                return
            name = self.path.strip('/') or 'a'
            body = f"<!doctype html><html lang='en'><title>{name}</title><h1>{name}</h1>".encode()
            if args.space_creator and self.path == '/a':
                body += b'<audio src="/silence.wav" preload="auto" loop></audio>'
            if args.edge:
                body += b'<style>html {background:#216491}</style>'
            self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_port}/a'
    if args.tooltips:
        try:
            directory, rows = run(url, ['--probe-sidebar-peek=tooltips'], allow_failure=True)
            documents = [json.loads(row['details']['result']) for row in rows if row['event'] == 'peek_document']
            tips = {d['tooltipHost']: d for d in documents if 'tooltipHost' in d}
            moves = {row['details']['host']: row['details'] for row in rows if row['event'] == 'sidebar_tooltip_pointer'}
            passed = (set(tips) == set(moves) == {'docked', 'floating'} and
                      all(d['sidebar_hit'] and d['page_hit'] for d in moves.values()) and
                      all(t['shown'] and t['dismissed'] and not t['nativeTitle'] for t in tips.values()) and
                      json.loads((directory / 'summary.json').read_text())['passed'])
            print(json.dumps({'passed': passed, 'tooltips': tips, 'pointer': moves}, indent=2), flush=True)
            if not passed:
                raise RuntimeError(f'Sidebar tooltip transition incomplete: {directory}')
        finally:
            server.shutdown(); server.server_close()
        return
    if args.space_creator:
        profile = ROOT / 'artifacts' / ('space-creator-profile-' + uuid4().hex[:8]) / 'profile'
        try:
            seed(profile, url, 6)
            with sqlite3.connect(profile / 'BrowserData/workspace.sqlite3') as db:
                db.execute('UPDATE nodes SET favorite=1 WHERE position>0')
            directory, rows = run(url, ['--probe-sidebar-peek=space-creator'], allow_failure=True, profile=profile, timeout=65)
            documents = [json.loads(row['details']['result']) for row in rows if row['event'] == 'peek_document']
            results = {d['spaceCreator']: d for d in documents if 'spaceCreator' in d}
            routes = {r['details']['surface'] for r in rows if r['event'] == 'shared_sidebar_action' and r['details']['method'] == 'space.cancelDraft'}
            fields = ('before', 'clickable', 'hiddenDuringCreate', 'restored', 'gridRestored', 'shrank', 'grew')
            passed = set(results) == routes == {'docked', 'floating'} and all(d.get(key) is True for d in results.values() for key in fields)
            passed = passed and json.loads((directory / 'summary.json').read_text())['passed']
            result = {'passed': passed, 'run': directory.name, 'hosts': results, 'cancel_routes': sorted(routes)}
            (directory / 'space-creator.json').write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps(result, indent=2), flush=True)
            if not passed:
                raise RuntimeError(f'Create Space regression: {directory}')
        finally:
            server.shutdown(); server.server_close()
        return
    if args.cold:
        try:
            cold(host, url, args.cold, args.repeat, args.tabs)
        finally:
            server.shutdown()
            server.server_close()
        return
    if args.edge:
        try:
            directory, rows = run(url, [f'--probe-sidebar-peek={host}-edge'], allow_failure=True)
            samples = [r['details'] for r in rows if r['event'] == 'peek_edge_measurement']
            result = samples[0] if len(samples) == 1 else {'error': 'missing measurement', 'edge_to_pixel_ms': []}
            result['passed'] = (not result['error'] and len(result['edge_to_pixel_ms']) == 15 and
                                result.get('child') == (host == 'child') and json.loads((directory / 'summary.json').read_text())['passed'])
            (directory / 'sidebar_edge.json').write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps(result), flush=True)
            if not result['passed']:
                raise RuntimeError(f'Edge experiment failed: {directory}')
        finally:
            server.shutdown()
            server.server_close()
        return
    profile = ROOT / 'artifacts' / ('sidebar-profile-' + uuid4().hex[:8]) / 'profile'
    database = profile / 'BrowserData' / 'workspace.sqlite3'
    database.parent.mkdir(parents=True)
    try:
        # Start with the historical Favorites format and let Sol migrate it.
        with sqlite3.connect(database) as db:
            db.executescript('CREATE TABLE nodes(id TEXT PRIMARY KEY,parent_id TEXT,kind INTEGER,title TEXT,url TEXT,position INTEGER,pinned INTEGER,expanded INTEGER,favorite INTEGER,archived INTEGER);'
                             'CREATE TABLE state(singleton INTEGER PRIMARY KEY,revision INTEGER,active_id TEXT);'
                             'PRAGMA application_id=1380271170; PRAGMA user_version=2;')
            for position, (title, address) in enumerate([
                ('Start', url), ('WhatsApp fixture', 'https://web.whatsapp.com/'),
                ('WhatsApp duplicate fixture', 'https://web.whatsapp.com/'),
                ('WhatsApp deep-link fixture', 'https://web.whatsapp.com/send'),
                ('X disabled fixture', 'https://twitter.com/home'),
                ('X fixture', 'https://x.com/home'),
                ('X duplicate fixture', 'https://x.com/'),
                ('X deep-link fixture', 'https://x.com/notifications'),
                ('Pinned editor fixture', url.removesuffix('/a') + '/pinned-editor'),
            ]):
                node = f'11111111-1111-4111-8111-{position:012d}'
                favorite = title in ('X disabled fixture', 'X fixture')
                db.execute('INSERT INTO nodes VALUES(?,?,?,?,?,?,?,?,?,?)', (node, None, 0, title, address, position, position > 0, 1, favorite, 0))
                if position == 0:
                    db.execute('INSERT INTO state VALUES(1,1,?)', (node,))
        with sqlite3.connect(database.with_name('settings.sqlite3')) as db:
            db.executescript('CREATE TABLE preferences(id INTEGER PRIMARY KEY, value TEXT NOT NULL);'
                             'PRAGMA application_id=1380271184; PRAGMA user_version=1;')
            preferences = dict(schema=1, searchEngine='bing', downloadDirectory='', askDownload=False,
                               interfaceLanguage='uk',
                               startup='restore', homepage='https://www.google.com/', shortcuts={},
                               integrations=dict(notion=True, linear=True, confluence=True, figma=True))
            db.execute('INSERT INTO preferences VALUES(1,?)', (json.dumps(preferences),))
        run(url, ['--workspace'], profile=profile)
        with sqlite3.connect(database) as db:
            db.execute("UPDATE nodes SET x_badge_hidden=1 WHERE title='X disabled fixture'")
            db.execute("UPDATE spaces SET theme_palette=?, theme_noise=0 WHERE id=(SELECT active_space FROM state)",
                       ('2,3,380,66,0,0,100,30,850,27,120,850,62,240,850,37,90',))
        flag = f'--probe-sidebar-peek={host}' if host else '--probe-sidebar-peek'
        directory, rows = run(url, [flag], allow_failure=True, profile=profile)
        with sqlite3.connect(database) as db:
            saved_pin = db.execute("SELECT title,url FROM nodes WHERE id='11111111-1111-4111-8111-000000000008'").fetchone()
    finally:
        server.shutdown(); server.server_close()
    def first(event): return next((r.get('details', {}) for r in rows if r['event'] == event), {})
    documents = [json.loads(r['details']['result']) for r in rows if r['event'] == 'peek_document']
    document = next((d for d in documents if 'rows' in d), {})
    menu = next((d['menu'] for d in documents if 'menu' in d), None)
    x_docked = next((d for d in documents if d.get('xBadge') == 'docked'), {})
    x_floating = next((d for d in documents if d.get('xBadge') == 'floating' and 'startsHidden' in d), {})
    whatsapp_floating = next((d for d in documents if 'whatsAppFloating' in d), {})
    whatsapp_docked = next((d for d in documents if 'whatsAppDocked' in d), {})
    cards = [r['details'] for r in rows if r['event'] == 'popup_presented' and r['details'].get('kind') == 'integration']
    favorites = [c for c in cards if c.get('favorite') is True]
    pinned = [c for c in cards if c.get('favorite') is False]
    editors = {r['details']['floating']: r['details'] for r in rows if r['event'] == 'pin_editor_window'}
    editor_documents = {r['details']['save']: r['details'] for r in rows if r['event'] == 'pin_editor_document'}
    editor_closes = {r['details']['floating']: r['details'] for r in rows if r['event'] == 'pin_editor_closed'}
    editor_presentations = [r['details'] for r in rows if r['event'] == 'popup_presented' and r['details'].get('kind') == 'editPinned']
    pinned_hints = {d['pinnedHint']: d for d in documents if 'pinnedHint' in d}
    library = {r['details']['surface']: r['details'] for r in rows if r['event'] == 'library_sidebar_host'}
    checks = {
        'both_hosts_open_library_and_restore_geometry': set(library) == {'docked', 'floating'} and
            all(all(value.get(key) is True for key in ('opened', 'restored', 'same_browser')) for value in library.values()) and
            {r['details']['surface'] for r in rows if r['event'] == 'shared_sidebar_action' and r['details']['method'] == 'library.setOpen'} == {'docked', 'floating'},
        'ukrainian_in_both_hosts': all(any(d.get('localeHost') == host and d.get('locale') == 'uk' and
                                          d.get('newTab') == 'Нова вкладка' and d.get('sidebarLabel') == 'Вкладки й папки' and
                                          d.get('overflow') is False and d.get('narrowOverflow') is False for d in documents)
                                      for host in ('docked', 'floating')),
        'both_hosts_reveal_pinned_hint_and_reset': set(pinned_hints) == {'docked', 'floating'} and
            all(all(d.get(key) is True for key in ('hidden', 'shown', 'lifted', 'sameHeight', 'arrowCursor', 'highlight', 'atHome')) for d in pinned_hints.values()),
        'both_hosts_open_pinned_editor': {r['details']['surface'] for r in rows if r['event']=='shared_sidebar_action' and r['details']['method']=='tab.editPinned'} == {'docked','floating'},
        'both_editors_centered_owned_and_modal': len(editors) == 2 and all(
            all(d[key] for key in ('presented', 'centered', 'owned', 'owner_disabled'))
            and (os.environ.get('RED_PROBE_DISPLAY') != 'secondary' or d['secondary']) for d in editors.values()),
        'both_composited_backdrops_dim_the_page_and_leave_the_editor_clickable': len(editors) == 2 and
            all(d['backdrop'] and d['editor_hit_test'] for d in editors.values()),
        'both_editors_remove_the_backdrop': len(editor_closes) == 2 and all(d['backdrop_removed'] for d in editor_closes.values()),
        'editor_prefills_and_focuses_after_open_and_cancel': len(editor_documents) == 2 and len(editor_presentations) == 2 and
            all(d['prefilled'] and d['focused'] for d in editor_documents.values()) and
            all(d['foreground'] and d['keyboard_focus'] for d in editor_presentations),
        'save_and_cancel_give_pressed_feedback': len(editor_documents) == 2 and all(d['pressed'] for d in editor_documents.values()),
        'editor_rejects_invalid_scheme_and_saves': editor_documents.get(True, {}).get('rejected') is True and
            saved_pin == ('Edited in modal', url.removesuffix('/a') + '/pinned-saved'),
        'both_editors_restore_owner_on_close': len(editor_closes) == 2 and all(d['closed'] and d['owner_enabled'] for d in editor_closes.values()),
        'docked_sidebar_hidden_after_animation': any(r['event'] == 'sidebar_native_layout' and not r['details']['visible'] and r['details']['native_geometry'] for r in rows),
        'peek_window_shown': first('peek_state_open').get('open') is True and first('peek_state_open').get('visible') is True,
        'peek_lists_tabs': {'a', 'b'} <= set(document.get('rows', [])),
        'sidebar_right_aligned_in_window': abs(document.get('right', -1) - document.get('width', -2)) <= 1,
        'floating_background_matches_workspace_geometry': document.get('backgroundPalette') is True and
            bool(first('peek_state_open').get('background_size')) and
            first('peek_state_open').get('background_size') == document.get('backgroundSize') and
            first('peek_state_open').get('background_origin') == document.get('backgroundOrigin'),
        'non_sidebar_command_refused': document.get('refused') == 404,
        'ready_update_visible_in_floating_sidebar': any(d.get('updateVisible') is True and d.get('updateVersion') is True for d in documents),
        'media_state_reaches_floating_sidebar': any(d.get('mediaVisible') is True for d in documents),
        'update_notice_not_covered_by_media': any(d.get('updateClickable') is True for d in documents),
        'floating_restart_routes_to_native_update_handler': any(d.get('restartRouted') is True and d.get('invalidUpdate')==400 for d in documents),
        'both_hosts_use_shared_restart_dispatch': {r['details']['surface'] for r in rows if r['event']=='shared_sidebar_action' and r['details']['method']=='browser.restartToUpdate'} == {'docked','floating'},
        'both_hosts_route_integration_cards': {r['details']['surface'] for r in rows if r['event']=='shared_sidebar_action' and r['details']['method']=='integration.preview'} == {'docked','floating'},
        'both_hosts_route_x_badge': {r['details']['surface'] for r in rows if r['event']=='shared_sidebar_action' and r['details']['method']=='tab.setXBadgeHidden'} == {'docked','floating'},
        'both_hosts_apply_global_integration_setting': {r['details']['surface'] for r in rows if r['event']=='shared_sidebar_action' and r['details']['method']=='tab.setIntegration'} == {'docked','floating'} and
            x_docked.get('globalDisabled') is True and x_floating.get('globallyOff') is True and x_floating.get('restoredGlobal') is True,
        'paused_integration_preferences_are_ignored': x_docked.get('legacyPreferencesLoaded') is True and x_docked.get('pausedCommandsRejected') is True,
        'docked_x_badge_renders_and_can_be_hidden': all(x_docked.get(key) is True for key in ('visible', 'corner', 'hidden')),
        # X's counter supplies the dot; the X pin itself stays unloaded (integrations/x_unread.h).
        'x_badge_appears_on_startup_without_loading_x': x_docked.get('startedInBackground') is True,
        'x_pins_stay_cold_and_open_on_click': x_docked.get('otherXCold') is True and x_docked.get('activated') is True and
            [r['details']['url'] for r in rows if r['event']=='address_change' and r['details']['url'].startswith(('https://x.com/', 'https://twitter.com/'))]==['https://x.com/home'],
        'floating_x_badge_can_be_restored_and_follows_the_counter': all(x_floating.get(key) is True for key in ('startsHidden', 'visible', 'cleared')),
        # WhatsApp is not kept loaded: hidden periodic checks supply the dot (integrations/whatsapp_unread.h).
        'whatsapp_badge_from_hidden_check_in_both_hosts': whatsapp_docked.get('whatsAppDocked') is True and whatsapp_floating.get('whatsAppFloating') is True,
        'whatsapp_pins_stay_cold_until_opened': whatsapp_floating.get('otherPinsCold') is True and
            sum(r['event']=='address_change' and r['details']['url'].startswith('https://web.whatsapp.com/') for r in rows)==1,
        'whatsapp_reuses_pin_and_keeps_dot_after_unload': whatsapp_docked.get('activated') is True and whatsapp_docked.get('unloaded') is True,
        'whatsapp_hidden_check_reads_count_and_closes': any(r['event']=='whatsapp_check' and r['details'].get('unread')==3 for r in rows) and
            sum(r['event']=='whatsapp_worker_closed' for r in rows) >= 1,
        'whatsapp_pin_is_not_held_in_memory': first('whatsapp_startup_pin').get('protected') is False,
        'integration_card_shows_without_focus_theft': first('integration_docked').get('open') is True and first('integration_docked').get('kept_focus') is True,
        'integration_card_in_floating_host': first('integration_floating').get('open') is True,
        'integration_cef_widget_and_document_visible': first('integration_docked').get('views_visible') is True and first('integration_document').get('visible') is True,
        'integration_aligns_favorite_corner': len(favorites) >= 2 and all(
            abs(c['x'] - c['anchor_right']) <= 1 and abs(c['y'] - c['anchor_bottom']) <= 1
            and c['origin_x'] == c['origin_y'] == 0 for c in favorites),
        'integration_centers_pinned_card_with_gap': bool(pinned) and all(
            abs(c['x'] - c['anchor_right'] - 8) <= 1 and c['origin_x'] == 0
            and abs(c['y'] + c['origin_y'] * c['height'] - (c['anchor_bottom'] + c['anchor_top']) / 2) <= 1
            and abs(c['origin_y'] - .5) <= .01 for c in pinned),
        'integration_zooms_in_both_hosts_when_motion_enabled': {c['surface'] for c in cards} == {'docked', 'floating'} and all(
            c['composed'] == c['motion_enabled'] for c in cards),
        'integration_hands_off_to_the_visible_window': first('integration_floating').get('settled') is True and
            first('integration_floating').get('hit_testable') is True,
        # One row per Figma file, named from its titled visit or its address; newest visit sets the time.
        'integration_uses_real_profile_history_and_filters_spoofs': first('integration_document').get('rows') == ['Abyss Cup', 'Slug Name', 'Drafts', 'Owned recent design'] and
            first('integration_document').get('times') == ['1970-01-01T00:00:02.600Z', '1970-01-01T00:00:02.400Z', '1970-01-01T00:00:01.500Z', '1970-01-01T00:00:01.000Z'],
        'integration_rejects_stale_opening': first('integration_document').get('stale', {}).get('code') == 409 and first('integration_document').get('stale', {}).get('message') == 'Integration opening changed',
        'figma_create_rejects_stale_opening': first('integration_document').get('staleCreate', {}).get('code') == 409,
        'figma_create_opens_in_original_pin': first('figma_create_in_pin').get('passed') is True,
        'integration_opens_while_owner_inactive': first('integration_inactive_owner').get('open') is True and first('integration_inactive_owner').get('cover_kept_focus') is True,
        'integration_stays_closed_over_covering_window': first('integration_covered_owner').get('covered') is True and
            first('integration_covered_owner').get('closed') is True and first('integration_covered_request').get('blocked') is True,
        'integration_closes_when_pointer_leaves': first('integration_pointer_left').get('closed') is True,
        # Foreground policy can dismiss the native panel in a background runner;
        # count dispatches, so a hidden subscriber cannot open/toggle it twice.
        'browser_menu_flow_has_one_active_sidebar_owner': first('sidebar_request_owner').get('docked') is True and sum(r['event']=='panel_open' and r.get('details',{}).get('kind')=='space' for r in rows)==1,
        'row_menu_opens_from_floating_sidebar': any(r['event'] == 'sidebar_menu_open' for r in rows) and menu == 'shown',
        'row_click_activates_tab': first('peek_after_click').get('url', '').endswith('/a'),
        'slide_clipped_by_host_inside_window': bool(first('peek_mid_slide')) and first('peek_mid_slide')['sidebar_left'] < first('peek_mid_slide')['host_left']
            and first('peek_mid_slide')['child_of_host'] is True and first('peek_mid_slide')['host_inside_root'] is True
            and first('peek_mid_slide')['host_transparent'] is True,
        # Trusted UI: a right-click in a text field offers text editing only.
        'context_menu_editing_only': bool(first('peek_context_menu')) and first('peek_context_menu')['editable'] is True
            and first('peek_context_menu')['inspect_kept'] is False and 0 < first('peek_context_menu')['kept'] <= 8
            and first('peek_context_menu')['kept'] < first('peek_context_menu')['offered'],
        'peek_hidden_after_slide_out': first('peek_state_closed').get('open') is False and first('peek_state_closed').get('visible') is False,
        'probe_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows),
    }
    host_state = first('peek_state_open')
    if host:
        checks['requested_native_host'] = host_state.get('root_child') == (host == 'child') and host_state.get('root_owned') == (host == 'owned')
        checks['native_hit_inside_and_beside_panel'] = host_state.get('panel_hit') is True and host_state.get('page_hit') is True
        checks['native_click_reached_panel_input'] = any(d.get('nativeInputClick') is True for d in documents)
    # Compare successive placements only within the same slide.
    pacing = []
    frames = [r['details'] for r in rows if r['event'] == 'peek_frame']
    for direction in (True, False):
        times = [f['ms'] for f in frames if f['in'] == direction]
        gaps = sorted(b['ms'] - a['ms'] for a, b in zip(frames, frames[1:])
                      if a['in'] == b['in'] == direction and b['ms'] > a['ms'])
        if gaps:
            pacing.append({'in': direction, 'frames': len(times), 'median_ms': round(gaps[len(gaps) // 2], 1),
                           'max_ms': round(gaps[-1], 1), 'over_20ms': sum(g > 20 for g in gaps)})
    ready_ms = []
    requested = None
    for row in rows:
        if row['event'] == 'peek_show_requested' and requested is None:
            requested = row['details']['ms']
        elif row['event'] == 'peek_ready_to_slide' and requested is not None:
            ready_ms.append(round(row['details']['ms'] - requested, 1))
            requested = None
        elif row['event'] in ('sidebar_peek_hidden', 'sidebar_peek_frame_timeout'):
            requested = None
    result = {'passed': all(checks.values()) and json.loads((directory / 'summary.json').read_text())['passed'],
              'checks': checks, 'document': document, 'pacing': pacing, 'library': library,
              'host': host, 'host_state': host_state, 'request_to_ready_ms': ready_ms,
              'context_menu': first('peek_context_menu'), 'pin_editors': editors, 'pin_editor_documents': editor_documents,
              'pin_editor_closes': editor_closes, 'pinned_hints': pinned_hints}
    (directory / 'sidebar_peek.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    if not result['passed']: raise RuntimeError('Floating sidebar smoke failed')


if __name__ == '__main__': main()
