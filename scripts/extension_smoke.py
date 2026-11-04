"""Load an owned unpacked MV3 extension against a loopback-only fixture."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import threading
import uuid

from smoke import ROOT, run


def main() -> None:
    reports: list[dict] = []
    blocked_requests: list[str] = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass

        def do_GET(self):
            if self.path == "/blocked":
                blocked_requests.append(self.path)
                body = b"This request must be blocked by the extension."
            else:
                body = b"<!doctype html><html lang='en'><head><title>Sol controlled fixture</title></head><body><h1>CEF content view</h1><p>Owned loopback fixture for MV3 and sandbox tests.</p></body></html>"
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            if self.path != "/extension-report" or not 0 < length <= 16384:
                self.send_error(400)
                return
            reports.append(json.loads(self.rfile.read(length)))
            self.send_response(204)
            self.end_headers()

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    port = server.server_port
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    extension = ROOT / ".cache" / ("mv3-probe-" + uuid.uuid4().hex[:8])
    extension.mkdir(parents=True)
    for source in (ROOT / "tests/extensions/mv3-probe").iterdir():
        if source.is_file():
            (extension / source.name).write_text(source.read_text().replace("__PROBE_PORT__", str(port)))
    try:
        directory, _ = run(f"http://127.0.0.1:{port}/fixture", [f"--load-extension={extension}"])
        expected = {"service_worker", "content_script", "runtime", "storage", "tabs",
                    "scripting", "extension_no_host_bridge", "declarativeNetRequest"}
        passed = len(reports) == 1 and set(reports[0].get("checks", {})) == expected and all(
            value is True for value in reports[0]["checks"].values()) and not blocked_requests
        result = {"passed": passed, "reports": reports, "blocked_requests": blocked_requests}
        (directory / "extension.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2), flush=True)
        if not passed:
            raise RuntimeError(f"MV3 compatibility probe failed: {directory}")
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
