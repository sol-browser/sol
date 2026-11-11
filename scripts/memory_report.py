"""Where a running Sol's memory goes: per process, with the tabs and Sol windows each renderer hosts.

Asks the running Sol for its browsers (`sol.exe --memory-report=<file>`, see
apps/desktop/src/app/memory_report.cpp) and joins them with each sol.exe process's memory through
the renderer host ID (`--renderer-client-id`). Only site hosts are reported, never page
addresses or titles. Nothing is sent anywhere; the joined report is saved next to the Sol report.

  python scripts/memory_report.py                       # the installed Sol
  python scripts/memory_report.py --exe <sol.exe> --profile-dir <dir>   # a test build/profile

Sol is launched through WMI so the request reaches the user's real Sol even from a packaged
(MSIX-virtualized) shell, where a direct launch would see another AppData and start a new Sol.
"""
import argparse
import json
import os
import re
import subprocess
import time
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def powershell(script):
    out = subprocess.run(['powershell.exe', '-NoProfile', '-Command', script], capture_output=True, text=True, encoding='utf-8')
    if out.returncode:
        raise SystemExit(out.stderr.strip() or 'PowerShell failed')
    return out.stdout


def launch(command_line):
    escaped = command_line.replace("'", "''")
    powershell(f"$r = Invoke-CimMethod Win32_Process -MethodName Create -Arguments @{{CommandLine='{escaped}'}}; "
               "if ($r.ReturnValue) {{ throw \"WMI launch failed: $($r.ReturnValue)\" }}")


def processes():
    data = powershell(
        "Get-CimInstance Win32_Process -Filter \"name='sol.exe'\" | ForEach-Object { $g = Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue; "
        "if ($g) { [pscustomobject]@{ pid=[int]$_.ProcessId; parent=[int]$_.ParentProcessId; cmd=[string]$_.CommandLine; "
        "private=[int64]$g.PrivateMemorySize64; ws=[int64]$g.WorkingSet64 } } } | ConvertTo-Json -Depth 2")
    rows = json.loads(data or '[]')
    return rows if isinstance(rows, list) else [rows]


def kind(cmd):
    t = re.search(r'--type=([\w-]+)', cmd)
    if not t:
        return 'browser'
    if '--extension-process' in cmd:
        return 'extension'
    sub = re.search(r'--utility-sub-type=([\w.]+)', cmd)
    return f"utility:{sub.group(1).split('.')[-1]}" if sub else t.group(1)


def category(process, browsers):
    if process['kind'] != 'renderer':
        return {'browser': 'main browser process', 'gpu-process': 'GPU', 'extension': 'extensions'}.get(process['kind'], 'utilities')
    roles = {b['role'] for b in browsers}
    if not roles:
        return 'renderers without a known browser'
    if all(r.startswith('sol:') for r in roles):
        return 'Sol windows'
    return 'websites'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe', type=Path, default=Path(os.environ.get('LOCALAPPDATA', '')) / 'SolBrowser/current/sol.exe')
    parser.add_argument('--profile-dir', type=Path, help='the profile the running Sol uses, when it is not the default one')
    parser.add_argument('--out', type=Path, default=ROOT / 'artifacts' / time.strftime('memory-%Y%m%dT%H%M%S'))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    report_path = (args.out / 'sol-browsers.json').resolve()
    command = f'"{args.exe}" --memory-report="{report_path}"' + (f' "--profile-dir={args.profile_dir}"' if args.profile_dir else '')
    launch(command)
    for _ in range(100):
        if report_path.exists() and report_path.stat().st_size:
            break
        time.sleep(0.1)
    else:
        raise SystemExit('Sol did not write a report. Is it running with this --exe/--profile-dir, and does it include memory reports?')
    time.sleep(0.2)
    report = json.loads(report_path.read_text(encoding='utf-8'))
    main_pid = report['browserProcessId']
    all_processes = processes()
    mine = [p for p in all_processes if p['pid'] == main_pid or p['parent'] == main_pid]
    by_host = defaultdict(list)
    for b in report['browsers']:
        by_host[b['rendererHostId']].append(b)
    rows = []
    for p in mine:
        p['kind'] = kind(p['cmd'])
        client = re.search(r'--renderer-client-id=(\d+)', p['cmd'])
        hosted = by_host.get(int(client.group(1)), []) if client else []
        rows.append({'pid': p['pid'], 'kind': p['kind'], 'privateMB': round(p['private'] / 2**20), 'workingSetMB': round(p['ws'] / 2**20),
                     'category': category(p, hosted),
                     'hosts': [{k: b[k] for k in ('role', 'site', 'shown', 'active', 'audible')} for b in hosted]})
    rows.sort(key=lambda r: -r['privateMB'])
    totals = defaultdict(lambda: [0, 0])
    for r in rows:
        totals[r['category']][0] += 1
        totals[r['category']][1] += r['privateMB']
    unmatched = [b for b in report['browsers'] if b['rendererHostId'] not in
                 {int(m.group(1)) for p in mine if (m := re.search(r'--renderer-client-id=(\d+)', p['cmd']))}]
    result = {'totalPrivateMB': sum(r['privateMB'] for r in rows), 'processes': rows,
              'byCategory': {k: {'processes': v[0], 'privateMB': v[1]} for k, v in sorted(totals.items(), key=lambda kv: -kv[1][1])},
              'browsersWithoutProcess': unmatched}
    (args.out / 'memory.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')

    print(f"Sol: {len(rows)} processes, {result['totalPrivateMB']} MB private\n")
    for name, value in result['byCategory'].items():
        print(f"  {value['privateMB']:6} MB  {name} ({value['processes']})")
    print()
    for r in rows:
        hosted = ', '.join(f"{h['role']}{' ' + h['site'] if not h['role'].startswith('sol:') else ''}"
                           f"{' [shown]' if h['shown'] else ''}{' [audible]' if h['audible'] else ''}" for h in r['hosts'])
        print(f"{r['privateMB']:6} MB  {r['kind']:<24} {hosted}")
    if unmatched:
        print(f"\n{len(unmatched)} browsers without a matching process (still loading or closing).")
    print(f"\nSaved {args.out / 'memory.json'}")


if __name__ == '__main__':
    main()
