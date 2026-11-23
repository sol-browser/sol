"""Run the isolated Spotify feasibility probe with the pinned Windows ECS runtime."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / '.cache/spotify-ecs-development/SolSpotifyProbe.exe'


def events(directory):
    report = directory / 'electron.jsonl'
    if not report.exists():
        return []
    # The host may poll while Electron is still appending the last event.
    complete = report.read_bytes().split(b'\n')[:-1]
    return [json.loads(line) for line in complete if line]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('preflight', 'interactive', 'host', 'tab'), default='preflight', nargs='?')
    parser.add_argument('--electron', type=Path)
    parser.add_argument('--session', help='Reuse an isolated, named ECS profile for a paired playback test')
    parser.add_argument('--exercise-playback', action='store_true',
                        help='Measure a full track after a person clicks Spotify Play in the signed tab')
    args = parser.parse_args()
    if args.session and (args.mode == 'host' or not re.fullmatch(r'[a-z0-9-]{1,32}', args.session)):
        parser.error('--session needs preflight, interactive or tab mode and 1–32 lowercase letters, digits or hyphens')
    if args.mode == 'tab' and not args.electron:
        parser.error('tab mode requires an explicit production-signed --electron executable')
    if args.exercise_playback and (args.mode != 'tab' or not args.session):
        parser.error('--exercise-playback requires tab mode and an existing named ECS session')
    executable = (args.electron or RUNTIME).resolve()
    if not executable.is_file():
        parser.error('Missing ECS executable. See the Spotify experiment in docs/codecs.md.')
    if args.mode == 'tab':
        signature = subprocess.run([sys.executable, '-m', 'castlabs_evs.vmp', '--no-ask', 'verify-pkg',
                                    str(executable.parent)], text=True, capture_output=True, timeout=30)
        if signature.returncode or 'Signature is valid: streaming' not in signature.stdout:
            parser.error('tab mode needs castLabs EVS and a currently valid streaming signature')
    directory = ROOT / 'artifacts' / ('spotify-ecs-' + uuid.uuid4().hex[:8])
    directory.mkdir(parents=True)
    print(directory, flush=True)
    profile = directory / 'ecs-profile'
    if args.session:
        profile = ROOT / '.cache' / ('spotify-ecs-profile-' + args.session)
    env = {**os.environ, 'SOL_ECS_MODE': args.mode, 'SOL_ECS_REPORT_DIR': str(directory),
           'SOL_ECS_PROFILE': str(profile)}
    command = [str(executable)]
    with subprocess.Popen(command, cwd=executable.parent, env=env,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) as process:
        try:
            if args.mode in ('host', 'tab'):
                from spotify_ecs_host import exercise_host
                exercise_host(directory, process, live=args.mode == 'tab',
                              exercise_playback=args.exercise_playback)
            code = process.wait(timeout=None if args.mode == 'interactive' else 110)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    rows = events(directory)
    print(json.dumps(rows, indent=2))
    if not rows or (args.mode == 'preflight' and not any(
        row['event'] == 'spotify_preflight' and row['mediaKeys'] for row in rows
    )):
        return code or 1
    return code


if __name__ == '__main__':
    raise SystemExit(main())
