"""Bounded, real Windows/CEF smoke run; no public debugging port or sandbox bypass."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]


def run(url: str, extra: list[str] | None = None, *, allow_failure: bool = False, profile: Path | None = None,
        timeout: int = 45) -> tuple[Path, list[dict]]:
    directory = ROOT / "artifacts" / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + uuid.uuid4().hex[:8])
    directory.mkdir(parents=True)
    report = directory / "native.jsonl"
    executable = Path(os.environ.get('RED_BROWSER_TEST_EXE', ROOT / "build/Release/apps/desktop/Release/sol.exe"))
    command = [str(executable), f"--probe-report={report}",
               f"--profile-dir={profile or directory / 'profile'}", f"--probe-url={url}", *(extra or [])]
    print(f"Running CEF smoke: {url}", flush=True)
    with (directory / "process.log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen(command, cwd=executable.parent, stdout=log, stderr=log)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True)
            raise RuntimeError(f"CEF did not shut down within {timeout}s: {directory}")
    rows = [json.loads(line) for line in report.read_text(encoding="utf-8").splitlines()] if report.exists() else []
    dev_exit = False
    # Dev engines (scripts/dev_red.ps1) end with one known DCHECK at CefShutdown (Sol still
    # holds CEF objects). RED_ACCEPT_DEV_SHUTDOWN_DCHECK=1 accepts that exit only when the
    # process log has no other fatal line.
    if code == 0x80000003 and os.environ.get('RED_ACCEPT_DEV_SHUTDOWN_DCHECK') == '1':
        fatal = [line for line in (directory / "process.log").read_text(encoding="utf-8", errors="replace").splitlines()
                 if ":FATAL:" in line]
        # Chromium 158 Dev reports the same leak as a still-registered browser context.
        known = ("Object reference incorrectly held at CefShutdown", "browser_context.cc:44] DCHECK failed: all_.empty()")
        if fatal and all(any(k in line for k in known) for line in fatal): code = 0; dev_exit = True
    results = [row["details"] for row in rows if row["event"] == "probe_result"]
    # The accepted Dev DCHECK stops CefShutdown before "shutdown" is written; Sol's own
    # part of the exit ends with "loop_ended".
    ended = "loop_ended" if dev_exit else "shutdown"
    passed = code == 0 and len(results) == 1 and results[0]["pass"] and any(r["event"] == ended for r in rows)
    if any(flag in ('--probe-workspace', '--probe-organization', '--probe-browsing') or flag.startswith('--probe-lifecycle') for flag in (extra or [])):
        layouts = [r['details'] for r in rows if r['event'] == 'sidebar_native_layout']
        passed = passed and all(any(r['visible'] == visible and r['native_geometry'] and r['native_input']
                                   for r in layouts) for visible in (False, True))
    summary = {"passed": passed, "exit_code": code, "url": url,
               "checks": results, "report": str(report)}
    (directory / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2), flush=True)
    if not passed and not allow_failure:
        print(json.dumps(rows, indent=2), flush=True)
        raise RuntimeError(f"Smoke failed: {directory}")
    return directory, rows


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="https://example.com")
    parser.add_argument("--navigation", action="store_true", help="Submit the real React address form (example.com only)")
    args = parser.parse_args()
    try:
        if args.navigation and args.url != "https://example.com":
            raise RuntimeError("The form navigation probe currently requires https://example.com")
        run(args.url, ["--probe-navigation"] if args.navigation else [])
    except (RuntimeError, OSError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
