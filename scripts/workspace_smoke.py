"""Drive React/native folder commands, restart the real app, verify lazy restore."""
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import sqlite3
import threading
import uuid
from smoke import ROOT, run


def main():
    requests = Counter()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass
        def do_GET(self):
            if self.path in ("/fixture", "/fixture?saved"):
                requests[self.path] += 1
            body = b"<!doctype html><html lang='en'><title>Initial workspace title</title><h1>Native workspace content</h1><input value='preserve me'><script>setTimeout(()=>document.title='Workspace real page',100)</script>"
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    profile = ROOT / "artifacts" / ("workspace-profile-" + uuid.uuid4().hex[:8])
    profile.mkdir()
    url = f"http://127.0.0.1:{server.server_port}/fixture"
    try:
        first, _ = run(url, ["--probe-workspace"], profile=profile)
        db = profile / "BrowserData/workspace.sqlite3"
        def saved():
            with sqlite3.connect(db) as connection:
                return {"nodes": connection.execute("SELECT * FROM nodes ORDER BY id").fetchall(),
                        "state": connection.execute("SELECT * FROM state").fetchall()}
        before = saved()
        first_requests = dict(requests)
        second, _ = run(url, ["--probe-workspace"], profile=profile)
        after = saved()
        with sqlite3.connect(db) as connection:
            metadata = connection.execute("SELECT title,page_title,custom_title FROM nodes WHERE kind=0 ORDER BY title").fetchall()
            width = connection.execute("SELECT sidebar_width FROM state").fetchone()[0]
        passed = before == after and requests == {"/fixture": 1, "/fixture?saved": 2} and width == 336 and all(
            title == 'Workspace real page' and custom == 0 for _, title, custom in metadata)
        result = {"passed": passed, "first_run": first.name, "second_run": second.name,
                  "same_nodes_and_revision_after_restart": before == after,
                  "document_requests_first": first_requests, "document_requests_total": dict(requests),
                  "saved": after, "profile": str(profile), "page_titles": metadata, "sidebar_width": width}
        (second / "workspace.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2), flush=True)
        if not passed:
            raise RuntimeError("Workspace restart or lazy renderer restoration failed")
    finally:
        server.shutdown(); server.server_close()


if __name__ == "__main__":
    main()
