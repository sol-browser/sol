"""Full regression: every scripts/*smoke*.py in turn, then a summary.

Smokes open Sol windows and take focus: run it while nobody uses the PC.
Usage: python regression.py [--exe <sol.exe>] [--dev] [--only name ...]
  --dev  accept the Dev engine's known shutdown DCHECK (scripts/smoke.py)
Writes artifacts/regression-<time>/summary.json and one log per smoke.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
TIMEOUT_S = 900


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--dev", action="store_true")
    parser.add_argument("--only", nargs="*")
    args = parser.parse_args()
    smokes = sorted(p for p in (ROOT / "scripts").glob("*smoke*.py"))
    if args.only:
        smokes = [p for p in smokes if p.stem in args.only]
    out = ROOT / "artifacts" / ("regression-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"))
    out.mkdir(parents=True)
    env = dict(os.environ)
    if args.exe:
        env["RED_BROWSER_TEST_EXE"] = str(args.exe.resolve())
    if args.dev:
        env["RED_ACCEPT_DEV_SHUTDOWN_DCHECK"] = "1"
    results = []
    for smoke in smokes:
        started = time.monotonic()
        log = out / f"{smoke.stem}.log"
        print(f"[{len(results) + 1}/{len(smokes)}] {smoke.stem} ...", end=" ", flush=True)
        with log.open("w", encoding="utf-8") as handle:
            try:
                code = subprocess.run([sys.executable, str(smoke)], cwd=ROOT / "scripts", env=env, stdout=handle,
                                      stderr=subprocess.STDOUT, timeout=TIMEOUT_S).returncode
            except subprocess.TimeoutExpired:
                code = "timeout"
        seconds = round(time.monotonic() - started)
        tail = log.read_text(encoding="utf-8", errors="replace").strip().splitlines()[-3:]
        results.append({"smoke": smoke.stem, "passed": code == 0, "code": code, "seconds": seconds, "tail": tail})
        print("PASS" if code == 0 else f"FAIL ({code})", f"{seconds}s", flush=True)
        (out / "summary.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    passed = sum(r["passed"] for r in results)
    print(f"\n{passed}/{len(results)} passed; details in {out}")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
