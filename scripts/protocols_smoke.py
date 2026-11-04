"""PDF viewing, local files and external protocols through actual CEF.

A PDF served over HTTP and the same PDF as a local file (opened like a typed
path) must both load Chrome's PDF viewer. A file dropped on a page must open in a
new tab with the viewer. A website's own navigation to
file:// must be refused, and a user-gesture link to an app scheme must reach
Chrome's external protocol handler without leaving the page.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
from smoke import run


def pdf() -> bytes:
    objects = [b'<< /Type /Catalog /Pages 2 0 R >>', b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
               b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>',
               None, b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>']
    stream = b'BT /F1 24 Tf 40 100 Td (Sol PDF fixture) Tj ET'
    objects[3] = b'<< /Length %d >>\nstream\n' % len(stream) + stream + b'\nendstream'
    out, offsets = bytearray(b'%PDF-1.4\n'), []
    for number, body in enumerate(objects, 1):
        offsets.append(len(out)); out += b'%d 0 obj\n' % number + body + b'\nendobj\n'
    xref = len(out)
    out += b'xref\n0 %d\n0000000000 65535 f \n' % (len(objects) + 1)
    out += b''.join(b'%010d 00000 n \n' % offset for offset in offsets)
    out += b'trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (len(objects) + 1, xref)
    return bytes(out)


def main():
    document = pdf()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            pdf_request = self.path == '/doc.pdf'
            body = document if pdf_request else b"<!doctype html><html lang='en'><title>Protocols fixture</title><h1>Start</h1>"
            self.send_response(200)
            self.send_header('Content-Type', 'application/pdf' if pdf_request else 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    start = f'http://127.0.0.1:{server.server_port}/start'
    from smoke import ROOT
    import uuid
    folder = ROOT / 'artifacts' / ('pdf-' + uuid.uuid4().hex[:8]); folder.mkdir(parents=True)
    local = folder / 'local fixture.pdf'; local.write_bytes(document)
    file_url = local.resolve().as_uri()
    try:
        directory, rows = run(start, [f'--probe-protocols={file_url}'], allow_failure=True)
    finally:
        server.shutdown(); server.server_close()
    def details(event): return [r.get('details', {}) for r in rows if r['event'] == event]
    viewer = [d['phase'] for d in details('protocols_pdf_viewer')]
    after_file = details('protocols_after_file_attempt')
    after_external = details('protocols_after_external')
    external = details('external_protocol')
    spa = next((i for i, r in enumerate(rows) if r['event'] == 'address_change' and r['details']['url'].endswith('#spa')), None)
    checks = {
        'same_document_back_state_published': spa is not None and any(
            r['event'] == 'snapshot_published' and r['details']['can_go_back'] for r in rows[spa:]),
        'http_pdf_opens_in_viewer': 1 in viewer,
        'typed_local_pdf_accepted': any(d['code'] == 0 for d in details('protocols_file_navigate')),
        'local_pdf_opens_in_viewer': 2 in viewer,
        'website_cannot_open_local_file': bool(after_file) and after_file[0]['url'] == start,
        'app_link_reaches_chrome_handler': any(d['scheme'] == 'red-probe-app' and d['allowed'] for d in external),
        'app_link_keeps_page': bool(after_external) and after_external[0]['url'] == start,
        'dropped_file_opens_new_tab': any(r['event'] == 'file_drop_opened' for r in rows),
        'dropped_pdf_opens_in_viewer': any(r['event'] == 'protocols_dropped_pdf_viewer' for r in rows),
        'probe_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows),
    }
    # Sidebar drop through DevTools may bypass CEF's OS drag hook: observed, not gated.
    observed = {'sidebar_drop_opened_files': any(r['event'] == 'shell_files_dropped' for r in rows)}
    result = {'passed': all(checks.values()), 'checks': checks, 'observed': observed, 'file_url': file_url,
              'windows_after_app_link': after_external[0]['windows'] if after_external else None}
    (directory / 'protocols.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps(result, indent=2, ensure_ascii=False), flush=True)
    if not result['passed']: raise RuntimeError('Protocols smoke failed')


if __name__ == '__main__': main()
