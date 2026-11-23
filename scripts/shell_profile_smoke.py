"""Exercise Shell startup with a real extension and an isolated CEF profile."""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import json
from pathlib import Path
import sqlite3
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/extensions/mv3-probe"
user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]


def windows(pid):
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def collect(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            name = ctypes.create_unicode_buffer(128)
            user32.GetClassNameW(hwnd, name, len(name))
            found.append((hwnd, name.value))
        return True

    user32.EnumWindows(collect, 0)
    return found


def settings(profile):
    entries = {}
    for filename in ("Preferences", "Secure Preferences"):
        path = profile / filename
        if path.is_file():
            prefs = json.loads(path.read_text(encoding="utf-8"))
            entries.update(prefs.get("extensions", {}).get("settings", {}))
    return entries


def disable_tray(profile):
    directory = profile / "BrowserData"
    directory.mkdir(parents=True)
    settings = {"schema": 1, "searchEngine": "google", "downloadDirectory": "", "askDownload": False,
                "startup": "restore", "homepage": "https://www.google.com/", "shortcuts": {}, "littleSolHotkey": False}
    with sqlite3.connect(directory / "settings.sqlite3") as db:
        db.executescript("PRAGMA application_id=1380271184; PRAGMA user_version=1;"
                         "CREATE TABLE preferences(id INTEGER PRIMARY KEY, value TEXT NOT NULL);")
        db.execute("INSERT INTO preferences VALUES(1, ?)", (json.dumps(settings),))


def run(exe, profile, report=None, expect_dialog=False):
    command = [str(exe), "--workspace", f"--profile-dir={profile}"]
    if report:
        command += [f"--probe-report={report}", f"--load-extension={FIXTURE}", "--probe-input"]
    process = subprocess.Popen(command, cwd=exe.parent)
    visible = []
    had_dialog = False
    try:
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f"Sol exited before opening its window ({process.returncode})")
            visible = windows(process.pid)
            had_dialog = any(name == "#32770" for _, name in visible)
            if had_dialog:
                break
            if not expect_dialog and any(name == "Chrome_WidgetWin_1" for _, name in visible):
                break
            time.sleep(0.1)
        else:
            raise RuntimeError(f"No Sol window appeared, windows={visible}")

        if not expect_dialog and not had_dialog:
            # A legacy guard waited 3 seconds before showing the violation.
            deadline = time.monotonic() + 4
            while time.monotonic() < deadline:
                visible = windows(process.pid)
                had_dialog = any(name == "#32770" for _, name in visible)
                if had_dialog or process.poll() is not None:
                    break
                time.sleep(0.1)
        if had_dialog != expect_dialog:
            raise RuntimeError(f"Unexpected Shell guard result, dialog={had_dialog}")
        if expect_dialog:
            log = (profile / "cef.log").read_text(encoding="utf-8", errors="replace")
            if not any(line.startswith(f"[{process.pid}:") and "Shell profile violation confirmed" in line
                       for line in log.splitlines()):
                raise RuntimeError("The legacy dialog did not come from the Shell guard")
            return
        for hwnd, name in visible:
            if name == "Chrome_WidgetWin_1":
                user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
                break
        code = process.wait(timeout=15)
        if code != 0:
            raise RuntimeError(f"Unexpected shutdown status {code}")
    finally:
        if process.poll() is None:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True)
            process.wait(timeout=15)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--baseline-only", action="store_true")
    args = parser.parse_args()
    baseline = args.baseline.resolve(strict=True)
    candidate = args.candidate.resolve(strict=True)
    directory = ROOT / "artifacts" / (
        "shell-profile-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + uuid.uuid4().hex[:6])
    directory.mkdir(parents=True)
    print(directory, flush=True)
    profile = directory / "profile"
    disable_tray(profile)
    run(baseline, profile, directory / "legacy.jsonl")
    shell = profile / "Shell"
    extension_ids = {key for key, entry in settings(shell).items() if entry.get("location") == 8}
    if len(extension_ids) != 1 or not extension_ids <= settings(profile / "Default").keys():
        raise RuntimeError("The same Chromium fixture must be installed in the legacy web and Shell profiles")

    (profile / "shell-reset-done").touch()
    for _ in range(4):
        run(baseline, profile, expect_dialog=True)
    print("Baseline showed the blocking dialog on four consecutive starts.", flush=True)
    if args.baseline_only:
        return

    (shell / "preserve-test").write_text("legacy interface data", encoding="utf-8")
    for _ in range(4):
        run(candidate, profile)
    backups = list(profile.glob("Shell (reset *)"))
    if len(backups) != 1 or (backups[0] / "preserve-test").read_text() != "legacy interface data":
        raise RuntimeError("The original Shell must be preserved by a single migration")
    if not extension_ids <= settings(profile / "Default").keys():
        raise RuntimeError("The web extension was lost during migration")
    run(candidate, profile, directory / "protected.jsonl")
    if extension_ids & settings(shell).keys():
        raise RuntimeError("The extension reached the protected Shell")
    rows = [json.loads(line) for line in (directory / "protected.jsonl").read_text(encoding="utf-8").splitlines()]
    checks = [row["details"] for row in rows if row["event"] == "shell_profile_extensions"]
    if not checks or any(not check["available"] or check["denied"] for check in checks):
        raise RuntimeError(f"Protected Shell did not pass its guard: {checks}")
    print("Protected Shell opened repeatedly, preserved the backup, and kept the extension in Default.", flush=True)


if __name__ == "__main__":
    main()
