"""Floating sidebar acceptance (--probe-sidebar-peek) through actual CEF."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
from smoke import run


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
    try:
        directory, rows = run(f'http://127.0.0.1:{server.server_port}/a', ['--probe-sidebar-peek'], allow_failure=True)
    finally:
        server.shutdown(); server.server_close()
    def first(event): return next((r.get('details', {}) for r in rows if r['event'] == event), {})
    documents = [json.loads(r['details']['result']) for r in rows if r['event'] == 'peek_document']
    document = next((d for d in documents if 'rows' in d), {})
    menu = next((d['menu'] for d in documents if 'menu' in d), None)
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
