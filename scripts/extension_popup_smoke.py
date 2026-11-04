"""Extension popup acceptance through actual CEF in workspace mode.

Loads the owned popup fixture extension, opens /second and /third, switches
back to the middle tab, then clicks the extension in the real site popover. The popup must
open in its own Alloy window bound to the visible (middle) tab, also as seen
by the extension's service worker, allow tab capture (activeTab invocation),, follow the
document's size and close when the popup calls window.close().
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import shutil
import struct
import threading
import uuid
import zlib
from smoke import ROOT, run


def png(size: int) -> bytes:
    row = b'\x00' + b'\xd9\x43\x5f\xff' * size
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(row * size)) + chunk(b'IEND', b''))


def main():
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            body = f"<!doctype html><html lang='en'><title>{self.path}</title><h1>{self.path}</h1>".encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    extension = ROOT / '.cache' / ('popup-probe-' + uuid.uuid4().hex[:8])
    shutil.copytree(ROOT / 'tests/extensions/popup-probe', extension)
    for size in (16, 48): (extension / f'icon{size}.png').write_bytes(png(size))
    action = ROOT / '.cache' / ('action-probe-' + uuid.uuid4().hex[:8])
    shutil.copytree(ROOT / 'tests/extensions/action-probe', action)
    first = f'http://127.0.0.1:{server.server_port}/first'
    second = first[:-len('first')] + 'second'
    try:
        directory, rows = run(first, ['--probe-extension-popup', f'--load-extension={extension},{action}'], allow_failure=True)
    finally:
        server.shutdown(); server.server_close()
        shutil.rmtree(extension, ignore_errors=True); shutil.rmtree(action, ignore_errors=True)
    def details(event): return [r.get('details', {}) for r in rows if r['event'] == event]
    queries = [json.loads(d['result']) for d in details('ext_popup_query')]
    resized = details('ext_popup_resized')
    loaded = details('ext_popup_loaded')
    clicked = details('extension_open_clicked')
    checks = {
        'switched_back_to_middle_tab': any(d['code'] == 0 for d in details('ext_popup_probe_activated')),
        'catalog_localized_name_and_png_icon': any(d['name'] == 'Popup Probe' and d['icon'] and d['popup'] for d in clicked),
        'popup_loaded_in_alloy_window': any(d['status'] == 200 and d['extension_origin'] and d['style'] == 2 for d in loaded),
        'popup_bound_to_visible_tab': len(queries) == 1 and queries[0].get('current') == [second] and queries[0].get('focused') == [second],
        'worker_bound_to_visible_tab': len(queries) == 1 and queries[0].get('worker', {}).get('focused') == [second],
        'window_follows_document_size': bool(resized) and resized[-1] == {'width': 280, 'height': 150},
        'window_close_closes_popup': len(details('ext_popup_closed')) == 1,
        # Engine patches 01/02 (cef-patches/): a click grants tab capture, pages
        # are normal windows, and a popup-less action gets action.onClicked.
        'tab_capture_allowed': bool(queries) and queries[0].get('capture', {}).get('ok') is True,
        'normal_window_type': bool(queries) and queries[0].get('worker', {}).get('normal') == [second],
        'popup_opened_via_action_run': any(d.get('result') == 1 and d.get('popup_url') for d in details('extension_action_run')),
        'popupless_action_ran': any(d.get('result') == 2 for d in details('extension_action_run')),
        'action_on_clicked_with_active_tab': any(d.get('path') == '/second' for d in details('ext_action_clicked')),
        'probe_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows),
    }
    result = {'passed': all(checks.values()), 'checks': checks, 'queries': queries, 'resized': resized}
    (directory / 'extension_popup.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    if not result['passed']: raise RuntimeError('Extension popup failed')


if __name__ == '__main__': main()
