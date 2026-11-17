"""Switch the language from the Settings UI and relaunch the same CEF profile."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import sqlite3
import threading
from uuid import uuid4

from smoke import ROOT, run


def main():
    requests = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass

        def do_GET(self):
            requests.append(self.headers.get("Accept-Language"))
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(b"<!doctype html><title>Language probe</title>")

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        profile = ROOT / "artifacts" / f"interface-language-{uuid4().hex[:8]}" / "profile"
        database = profile / "BrowserData" / "settings.sqlite3"
        database.parent.mkdir(parents=True)
        with sqlite3.connect(database) as db:
            db.executescript("CREATE TABLE preferences(id INTEGER PRIMARY KEY, value TEXT NOT NULL);"
                             "PRAGMA application_id=1380271184; PRAGMA user_version=1;")
            preferences = dict(schema=1, searchEngine="google", downloadDirectory="", askDownload=False,
                               startup="restore", homepage="https://www.google.com/", shortcuts={}, interfaceLanguage="en")
            db.execute("INSERT INTO preferences VALUES(1,?)", (json.dumps(preferences),))
        url = f"http://127.0.0.1:{server.server_port}/language"
        _, first = run(url, ["--probe-menu=language-save"], profile=profile, allow_failure=True)
        first_header = requests[0] if requests else None
        requests.clear()
        directory, second = run(url, ["--probe-menu=language-read"], profile=profile, allow_failure=True)
        second_header = requests[0] if requests else None
    finally:
        server.shutdown()
        server.server_close()

    def details(rows, event):
        return next((row["details"] for row in rows if row["event"] == event), {})

    def document(rows, prefix="RED_LANGUAGE:"):
        for row in rows:
            value = row.get("details", {}).get("message", "")
            if row["event"] == "settings_console" and value.startswith(prefix):
                return json.loads(value[len(prefix):])
        return {}

    before, after = document(first), document(second)
    groups = [document(rows, "RED_SETTINGS_GROUPS:") for rows in (first, second)]
    language = before.get("lang")
    checks = {
        "saved_from_settings": before.get("before") == "en" and before.get("value") == "uk",
        "invalid_language_rejected": before.get("invalid") == 400,
        "applied_after_restart": language == "en" and after.get("lang") == "uk"
        and after.get("before") == after.get("value") == "uk" and after.get("heading") == "Мова інтерфейсу",
        "native_settings_title": details(second, "language_window").get("title") == "Налаштування — Sol",
        "localized_menu_action": all(any(row["event"] == "menu_action_ran" for row in rows) for rows in (first, second)),
        "settings_groups_and_links": all(group.get("sidebar") == [
            "general", "tabs", "appearance", "search", "blocker", "languages", "profiles", "integrations", "keyboard", "releases"]
            and all(group.get(key) for key in ("browserGroup", "littleRows", "shortcutLink", "passwords", "imports",
                                                "integrations", "appearance", "about", "languages")) for group in groups),
        "minimum_width": all(0 < doc.get("width", 0) <= 720 for doc in (before, after)),
        "translated_search_and_shortcuts": after.get("search") == "Пошук з адресного рядка"
        and after.get("searchOverflow") is False and after.get("shortcuts") is True,
        "no_horizontal_overflow": all(doc.get("overflow") is False and doc.get("expandedOverflow") is False
                                      for doc in (before, after)),
        "website_language_preserved": bool(first_header) and first_header == second_header,
        "clean_shutdown_with_sandbox": all(details(rows, "probe_result").get("pass") is True
                                           and any(row["event"] == "shutdown" for row in rows)
                                           for rows in (first, second)),
    }
    result = {"checks": checks, "before": before, "after": after, "groups": groups}
    (directory / "interface_language.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    if not all(checks.values()):
        raise SystemExit(f"Interface language smoke failed: {directory}")


if __name__ == "__main__":
    main()
