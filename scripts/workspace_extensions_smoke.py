"""Measure extension activation and real action popups, with native Chrome control.

A successful evidence run is not an assertion of full product compatibility.
Never classify an API success as UI success without native observations.
"""
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
import uuid
from smoke import ROOT, run


def classify(report, rows):
    observations = {row["details"]["phase"]: row["details"] for row in rows
                    if row["event"] == "extension_observation"}
    if report.get("fatal") or set(observations) != {"before", "after_update", "after_restore", "after_popup"}:
        raise ValueError("Incomplete extension/native observations")
    first = report["first"]["id"]
    selected = report["selected"]["id"]
    if report["update"]["id"] != first or report["update"]["active"] is not True:
        raise ValueError("tabs.update did not return an active target")
    if report["documentRetained"] is not True or report["websiteBridge"] != "undefined":
        raise ValueError("Real document retention or website trust boundary failed")
    expected_tabs = {first, selected}
    if {tab['id'] for tab in report['before']['all']} != expected_tabs:
        raise ValueError('Unfiltered query did not identify all fixture tabs')
    after = observations["after_update"]
    # This binary's measured CEF IDs differ from extension tab IDs despite the
    # header comment. Correlate the owned, unique fixture URLs independently.
    if after["source_url"] != report["first"]["url"]:
        raise ValueError("Fixture URL correlation failed")
    native_first = after["source_browser_id"]
    native_selected = observations["before"]["source_browser_id"]
    visible = [view["browser_id"] for view in after["views"] if view["visible"]]
    if report["mode"] == "workspace":
        before = observations["before"]
        if not all(o["workspace_ready"] and "snapshot" in o for o in observations.values()):
            raise ValueError("Native workspace was not ready")
        if before["content_browser_id"] != native_selected or first == selected or native_first == native_selected:
            raise ValueError("Activation did not start with two distinct real pages")
        # These are observations, not a success requirement for compatibility.
        selected_changed = after["snapshot"]["activeId"] != before["snapshot"]["activeId"]
        activation = "VERIFIED" if selected_changed and after["content_browser_id"] == native_first and visible == [native_first] else "UNSUPPORTED_FOR_THIS_HOSTING"
        if not all(o["single_visible_active"] for o in observations.values()):
            raise ValueError("An extension operation broke single-visible-active containment")
    else:
        activation = "NOT_TESTED_SHARED_WINDOW_SWITCH"  # Lifecycle control is tested by TabProbe.
    popup = report["popup"]
    document = report["popupDocument"]
    if popup["opened"]:
        if not document or document["bridge"] != "undefined" or document["viewTypeCount"] != 1:
            raise ValueError("Popup API success lacked a real isolated popup context")
        if document["tabIds"] != [selected] or document["windowIds"] != [report["selected"]["windowId"]]:
            raise ValueError("Popup saw the wrong active tab/window")
        popup_status = "VERIFIED" if document["visibility"] == "visible" and report["remainingPopupViews"] == 0 else "PARTIAL"
    else:
        if document is not None or not popup.get("error"):
            raise ValueError("Popup rejection contradicted document observations")
        popup_status = "UNSUPPORTED_FOR_THIS_HOSTING" if "Browser window has no toolbar." in popup['error'] else "PARTIAL"
    return {"activation": activation, "action_popup": popup_status,
            "visible_after_update": visible,
            "identities": {"first": {"cef": native_first, "extension": first},
                           "selected": {"cef": native_selected, "extension": selected}},
            "observations": observations}


def main():
    reports = []
    requests = Counter()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass
        def do_GET(self):
            if self.path.startswith('/fixture'):
                requests[self.path] += 1
            body = b"<!doctype html><html lang='en'><title>Workspace extension fixture</title><h1>Owned real document</h1><input value='initial'>"
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        def do_POST(self):
            length = int(self.headers.get('Content-Length', '0'))
            if self.path != '/workspace-extension-report' or not 0 < length <= 65536:
                self.send_error(400)
                return
            reports.append(json.loads(self.rfile.read(length)))
            self.send_response(204)
            self.end_headers()
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    results = []
    try:
        # Serial because popup/focus tests must not compete for foreground.
        for mode, flag in [('reference', '--probe-native-tabs'), ('workspace', '--probe-workspace')]:
            reports.clear()
            requests.clear()
            extension = ROOT / '.cache' / ('workspace-probe-' + uuid.uuid4().hex[:8])
            extension.mkdir(parents=True)
            for source in (ROOT / 'tests/extensions/workspace-probe').iterdir():
                (extension / source.name).write_text(source.read_text().replace('__PROBE_PORT__', str(server.server_port)).replace('__PROBE_MODE__', mode))
            directory, rows = run(f'http://127.0.0.1:{server.server_port}/fixture',
                                  [flag, f'--load-extension={extension}'], allow_failure=True)
            result = {'mode': mode, 'run': directory.name, 'passed': False,
                      'reports': list(reports), 'document_requests': dict(requests)}
            try:
                if len(reports) != 1:
                    raise ValueError('Expected exactly one completed extension report')
                result.update(classify(reports[0], rows))
                if not json.loads((directory / 'summary.json').read_text())['passed']:
                    raise ValueError('Native baseline acceptance failed')
                expected = {'/fixture': 1, '/fixture?second': 1} if mode == 'reference' else {'/fixture': 1, '/fixture?saved': 1}
                if requests != expected:
                    raise ValueError('Extension operation reloaded a document')
                if mode == 'reference' and not reports[0]['popup']['opened']:
                    raise ValueError('Native Chrome popup API/context control failed')
                result['passed'] = True
            except (ValueError, KeyError, TypeError) as error:
                result['error'] = str(error)
            (directory / 'workspace-extension.json').write_text(json.dumps(result, indent=2) + '\n')
            results.append(result)
            print(json.dumps(result, indent=2), flush=True)
        if not all(result['passed'] for result in results):
            raise RuntimeError('Extension evidence run failed; inspect workspace-extension.json')
    finally:
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    main()
