"""Floating sidebar acceptance (--probe-sidebar-peek) through actual CEF."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import sqlite3
import threading
from uuid import uuid4
from smoke import ROOT, run


def main():
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            name = self.path.strip('/') or 'a'
            body = f"<!doctype html><html lang='en'><title>{name}</title><h1>{name}</h1>".encode()
            self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_port}/a'
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
                               startup='restore', homepage='https://www.google.com/', shortcuts={},
                               integrations=dict(notion=True, linear=True, confluence=True, figma=True))
            db.execute('INSERT INTO preferences VALUES(1,?)', (json.dumps(preferences),))
        run(url, ['--workspace'], profile=profile)
        with sqlite3.connect(database) as db:
            db.execute("UPDATE nodes SET x_badge_hidden=1 WHERE title='X disabled fixture'")
        directory, rows = run(url, ['--probe-sidebar-peek'], allow_failure=True, profile=profile)
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
    checks = {
        'docked_sidebar_hidden_after_animation': any(r['event'] == 'sidebar_native_layout' and not r['details']['visible'] and r['details']['native_geometry'] for r in rows),
        'peek_window_shown': first('peek_state_open').get('open') is True and first('peek_state_open').get('visible') is True,
        'peek_lists_tabs': {'a', 'b'} <= set(document.get('rows', [])),
        'sidebar_right_aligned_in_window': abs(document.get('right', -1) - document.get('width', -2)) <= 1,
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
        'x_badge_appears_on_startup_without_switching_tabs': x_docked.get('startedInBackground') is True,
        'x_only_warms_one_enabled_home_pin_and_reuses_it': x_docked.get('otherXCold') is True and x_docked.get('activated') is True and
            [r['details']['url'] for r in rows if r['event']=='address_change' and r['details']['url'].startswith(('https://x.com/', 'https://twitter.com/'))]==['https://x.com/home'],
        'floating_x_badge_can_be_restored_and_tracks_background_title': all(x_floating.get(key) is True for key in ('startsHidden', 'visible', 'cleared')),
        'whatsapp_badge_after_startup_in_both_hosts': whatsapp_docked.get('whatsAppDocked') is True and whatsapp_floating.get('whatsAppFloating') is True,
        'whatsapp_only_warms_one_home_pin': whatsapp_floating.get('otherPinsCold') is True and
            sum(r['event']=='address_change' and r['details']['url'].startswith('https://web.whatsapp.com/') for r in rows)==1,
        'whatsapp_reuses_pin_and_respects_explicit_unload': whatsapp_docked.get('activated') is True and whatsapp_docked.get('unloaded') is True,
        'whatsapp_background_pin_survives_idle_unload': first('whatsapp_startup_pin').get('protected') is True,
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
    # Frame pacing of the slides: when each position was applied (ms since start).
    pacing = []
    frames = [r['details'] for r in rows if r['event'] == 'peek_frame']
    for direction in (True, False):
        times = [f['ms'] for f in frames if f['in'] == direction]
        gaps = sorted(b - a for a, b in zip(times, times[1:]))
        if gaps:
            pacing.append({'in': direction, 'frames': len(times), 'median_ms': round(gaps[len(gaps) // 2], 1),
                           'max_ms': round(gaps[-1], 1), 'over_20ms': sum(g > 20 for g in gaps)})
    result = {'passed': all(checks.values()), 'checks': checks, 'document': document, 'pacing': pacing,
              'context_menu': first('peek_context_menu')}
    (directory / 'sidebar_peek.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    if not result['passed']: raise RuntimeError('Floating sidebar smoke failed')


if __name__ == '__main__': main()
