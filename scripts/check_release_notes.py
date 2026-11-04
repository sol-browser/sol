"""Validate the release history embedded in Settings before packaging."""
from datetime import date
from pathlib import Path
import re


def main():
    root = Path(__file__).resolve().parents[1]
    version = (root / 'VERSION').read_text(encoding='utf-8-sig').strip()
    releases = []
    for line in (root / 'CHANGELOG.md').read_text(encoding='utf-8-sig').splitlines():
        heading = re.fullmatch(r'## (\d+\.\d+\.\d+) — (\d{4}-\d{2}-\d{2})(?:\s.*)?', line)
        if line.startswith('## '):
            if not heading:
                raise SystemExit(f'Invalid release heading: {line}')
            number, released = heading.groups()
            date.fromisoformat(released)
            releases.append({'version': number, 'date': released, 'changes': 0})
        elif line.startswith('- ') and releases and line[2:].strip():
            releases[-1]['changes'] += 1
    if not releases or releases[0]['version'] != version:
        raise SystemExit('First CHANGELOG.md release must match VERSION')
    previous = None
    for release in releases:
        number = tuple(map(int, release['version'].split('.')))
        if previous is not None and number >= previous:
            raise SystemExit('Release versions must be unique and newest first')
        if not release['changes']:
            raise SystemExit(f"Release {release['version']} has no changes")
        previous = number
    print(f'Release notes match {version}; {len(releases)} releases')


if __name__ == '__main__':
    main()
