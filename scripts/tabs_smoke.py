"""Two real Chrome-native tabs, page-state retention and extension tab events."""
import argparse
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
import uuid
from smoke import ROOT, run


def main(with_extension=True, native_reference=False, embedded=False):
    events = []
    requests = Counter()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass

        def do_GET(self):
            if self.path in ("/fixture", "/fixture?second"):
                requests[self.path] += 1
            body = b"<!doctype html><html lang='en'><head><title>Sol tabs fixture</title></head><body><h1>Native tab lifecycle fixture</h1><label>Unsaved form state <input value='initial'></label></body></html>"
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            if self.path != "/tabs-report" or not 0 < length <= 16384:
                self.send_error(400)
                return
            events.append(json.loads(self.rfile.read(length)))
            self.send_response(204)
            self.end_headers()

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    extra = ["--probe-embedded-tabs" if embedded else "--probe-native-tabs" if native_reference else "--probe-tabs"]
    if with_extension:
        extension = ROOT / ".cache" / ("tabs-probe-" + uuid.uuid4().hex[:8])
        extension.mkdir(parents=True)
        for source in (ROOT / "tests/extensions/tabs-probe").iterdir():
            (extension / source.name).write_text(source.read_text().replace("__PROBE_PORT__", str(server.server_port)))
        extra.append(f"--load-extension={extension}")
    try:
        directory, _ = run(f"http://127.0.0.1:{server.server_port}/fixture", extra, allow_failure=True)
        native_ok = json.loads((directory / "summary.json").read_text())["passed"]
        contents = [e for e in events if e["event"] == "content"]
        first = next((e for e in contents if e["role"] == "first"), None)
        second = next((e for e in contents if e["role"] == "second"), None)
        extension_ok = not with_extension
        shared_extension_window = None
        switching_order_ok = False
        if first and second:
            activated = [e["tabId"] for e in events if e["event"] == "activated"]
            removed = [e["tabId"] for e in events if e["event"] == "removed"]
            wanted = iter([second["tabId"], first["tabId"], second["tabId"]])
            target = next(wanted)
            switching_order_ok = False
            for tab_id in activated:
                if tab_id == target:
                    target = next(wanted, None)
                    if target is None:
                        switching_order_ok = True
                        break
            shared_extension_window = first["windowId"] == second["windowId"]
            extension_ok = first["tabId"] != second["tabId"] and second["tabId"] in removed and (
                True if embedded else shared_extension_window and switching_order_ok)
        passed = native_ok and extension_ok and requests == {"/fixture": 1, "/fixture?second": 1}
        result = {"passed": passed, "native_reference": native_reference,
                  "embedded": embedded, "shared_extension_window": shared_extension_window,
                  "extension_switch_activation_sequence": switching_order_ok if with_extension else None,
                  "extension_window_semantics": "NOT_TESTED" if shared_extension_window is None else
                      "VERIFIED" if shared_extension_window and switching_order_ok else "UNSUPPORTED_FOR_THIS_HOSTING",
                  "acceptance": "visual child-window tabs; extension window semantics measured separately" if embedded else "native Chrome tabs in one extension window",
                  "extension_tested": with_extension, "events": events,
                  "document_requests": dict(requests)}
        (directory / "tabs-extension.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2), flush=True)
        if not passed:
            raise RuntimeError(f"Tab event / no-reload verification failed: {directory}")
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--without-extension", action="store_true")
    parser.add_argument("--native-reference", action="store_true", help="Full native Chrome window + separate React shell reference")
    parser.add_argument("--embedded", action="store_true", help="Native Chrome child windows inside one React shell; extension window grouping is a separate result")
    args = parser.parse_args()
    if args.embedded and args.native_reference:
        parser.error("Choose only one hosting mode")
    main(not args.without_extension, args.native_reference, args.embedded)
