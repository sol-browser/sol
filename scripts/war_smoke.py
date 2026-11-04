"""Extension web_accessible_resources (static and use_dynamic_url) loaded by a page.

Owned loopback fixture and owned extension only. Runs the workspace host and
the single Chrome BrowserView host so a host-specific failure is visible.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
import uuid
from smoke import ROOT, run


def main() -> None:
    reports = []
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass
        def do_GET(self):
            body = b'<!doctype html><html lang="en"><meta charset="utf-8"><title>WAR fixture</title><h1>WAR fixture</h1>'
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        def do_POST(self):
            length = int(self.headers.get('Content-Length', '0'))
            if self.path != '/war-report' or not 0 < length <= 65536:
                self.send_error(400)
                return
            reports.append(json.loads(self.rfile.read(length)))
            self.send_response(204)
            self.end_headers()
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    results = []
    try:
        for mode, flags in [('workspace', ['--probe-workspace']), ('single', [])]:
            reports.clear()
            extension = ROOT / '.cache' / ('war-probe-' + uuid.uuid4().hex[:8])
            extension.mkdir(parents=True)
            for source in (ROOT / 'tests/extensions/war-probe').iterdir():
                (extension / source.name).write_text(source.read_text().replace('__PROBE_PORT__', str(server.server_port)))
            directory, rows = run(f'http://127.0.0.1:{server.server_port}/fixture',
                                  [*flags, f'--load-extension={extension}'], allow_failure=True)
            result = {'mode': mode, 'run': directory.name, 'reports': list(reports),
                      'console': [r['details'] for r in rows if r['event'] == 'page_extension_console']}
            (directory / 'war.json').write_text(json.dumps(result, indent=2) + '\n')
            results.append(result)
    finally:
        server.shutdown()
        server.server_close()
    print(json.dumps(results, indent=2))
    # Static and dynamic-URL resources must load in every host. Dynamic URLs
    # need Red engine patch 03 (cef-patches/, docs/web-accessible-resources.md).
    verdicts = {}
    for result in results:
        if not result['reports']:
            raise SystemExit(f"{result['mode']}: no extension report")
        report = result['reports'][0]['results']
        if report['static_getURL']['outcome'] != 'loaded':
            raise SystemExit(f"{result['mode']}: static web-accessible resource failed")
        if report['dynamic_static_id']['outcome'] == 'loaded':
            raise SystemExit(f"{result['mode']}: dynamic-only resource loaded through the static ID")
        if report['dynamic_getURL']['outcome'] != 'loaded':
            raise SystemExit(f"{result['mode']}: dynamic-URL web-accessible resource failed (engine patch 03)")
        verdicts[result['mode']] = 'VERIFIED'
    print(json.dumps({'static': 'VERIFIED', 'dynamic_url': verdicts}))


if __name__ == '__main__':
    main()
