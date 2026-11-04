"""Validate source pins/checkouts without changing Git or the binary archive lock."""
import argparse
import ast
import json
from pathlib import Path
import re
import subprocess
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
FIELDS = frozenset(('repository', 'revision', 'branch', 'upstream_repository',
                    'upstream_revision', 'cef_branch', 'chromium_tag', 'chromium_revision'))
CHROMIUM_REPOSITORY = 'https://chromium.googlesource.com/chromium/src.git'


def repository_url(value):
    """Require shell-safe HTTPS URLs: automate-git constructs shell commands."""
    if not isinstance(value, str) or not re.fullmatch(r'https://[A-Za-z0-9._/-]+', value):
        raise ValueError('Repository must be an HTTPS URL without credentials, options or escapes')
    parsed = urlsplit(value)
    if not parsed.hostname or not re.fullmatch(r'[A-Za-z0-9]+(?:[.-][A-Za-z0-9]+)*', parsed.hostname):
        raise ValueError('Invalid repository hostname')
    parts = parsed.path.split('/')[1:]
    if not parts or any(not part or part in ('.', '..') or part.startswith('-') for part in parts):
        raise ValueError('Invalid repository path')
    return value


def branch_name(value):
    if (not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._/-]*', value)
            or '..' in value or '//' in value
            or any(not part or part.startswith('.') or part.endswith(('.', '.lock')) for part in value.split('/'))):
        raise ValueError('Invalid fork branch')
    return value


def load_lock(path):
    lock = json.loads(Path(path).read_text(encoding='utf-8'))
    if not isinstance(lock, dict) or set(lock) != FIELDS:
        raise ValueError('Source lock must contain exactly: '+', '.join(sorted(FIELDS)))
    for key in ('repository', 'upstream_repository'):
        repository_url(lock[key])
    for key in ('revision', 'upstream_revision', 'chromium_revision'):
        if not isinstance(lock[key], str) or not re.fullmatch(r'[0-9a-f]{40}', lock[key]):
            raise ValueError(f'{key} must be a full lowercase Git SHA')
    branch_name(lock['branch'])
    if not isinstance(lock['cef_branch'], str) or not re.fullmatch(r'[1-9][0-9]{3,5}', lock['cef_branch']):
        raise ValueError('cef_branch must be the numeric upstream CEF release branch')
    tag = lock['chromium_tag']
    if not isinstance(tag, str) or not re.fullmatch(r'refs/tags/(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)', tag):
        raise ValueError('chromium_tag must be an exact refs/tags/A.B.C.D release tag')
    if tag.rsplit('/', 1)[1].split('.')[2] != lock['cef_branch']:
        raise ValueError('Chromium tag does not match cef_branch')
    return lock


def source_paths(work):
    work = Path(work).resolve()
    if work.parent == work or any(c.isspace() for c in str(work)):
        raise ValueError('Work directory must be a dedicated directory without spaces')
    if not re.fullmatch(r'[A-Za-z0-9_./:\\-]+', str(work)):
        raise ValueError('Work directory contains unsupported shell characters')
    paths = {'bootstrap':work/'cef-src', 'checkout':work/'code'/'cef',
             'chromium':work/'code'/'chromium'/'src',
             'build_checkout':work/'code'/'chromium'/'src'/'cef'}
    for path in paths.values():
        if not path.resolve().is_relative_to(work):
            raise ValueError(f'Source path escapes work directory: {path}')
    return paths


def git(path, *arguments):
    result = subprocess.run(['git', '--no-optional-locks', '-C', str(path), *arguments],
                            capture_output=True, text=True, encoding='utf-8')
    if result.returncode:
        raise ValueError(f'Git check failed in {path}: '+result.stderr.strip())
    return result.stdout.strip()


def require_checkout(path, repository):
    if not path.is_dir():
        raise ValueError(f'Missing source checkout: {path}')
    if Path(git(path, 'rev-parse', '--show-toplevel')).resolve() != path.resolve():
        raise ValueError(f'Expected an independent Git checkout at {path}')
    if git(path, 'remote', 'get-url', 'origin') != repository:
        raise ValueError(f'Checkout origin does not match source lock: {path}; adopt the fork explicitly or use a new work directory')


def require_clean(path):
    changes = git(path, 'status', '--porcelain', '--untracked-files=all')
    if changes:
        raise ValueError(f'Dirty checkout: {path}; preserve changes before syncing:\n'+'\n'.join(changes.splitlines()[:8]))


def check_cef(path, lock, exact):
    require_checkout(path, lock['repository'])
    require_clean(path)
    if exact:
        if git(path, 'rev-parse', 'HEAD') != lock['revision']:
            raise ValueError(f'CEF HEAD does not match source lock: {path}')
        git(path, 'merge-base', '--is-ancestor', lock['upstream_revision'], 'HEAD')
        compatibility = ast.literal_eval((path/'CHROMIUM_BUILD_COMPATIBILITY.txt').read_text(encoding='utf-8'))
        if not isinstance(compatibility, dict) or compatibility.get('chromium_checkout') != lock['chromium_tag']:
            raise ValueError(f'CEF Chromium compatibility does not match source lock: {path}')


def check_worktree(lock, work, mode):
    paths = source_paths(work)
    exact = mode == 'skip'
    for key in ('bootstrap', 'checkout', 'build_checkout'):
        path = paths[key]
        if exact or path.exists():
            check_cef(path, lock, exact)
    chromium = paths['chromium']
    if exact or chromium.exists():
        require_checkout(chromium, CHROMIUM_REPOSITORY)
        if exact and git(chromium, 'rev-parse', 'HEAD') != lock['chromium_revision']:
            raise ValueError('Existing Chromium HEAD differs from source pin; sync a clean checkout before using -SkipUpdate')
        # Chromium is normally dirty after CEF applies its versioned patch.cfg.
        # Sync requires a clean workspace; incremental --no-update builds keep
        # those patched Chromium files in place.
        if not exact:
            require_clean(chromium)
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lock', type=Path, default=ROOT/'cmake'/'cef-source.lock.json')
    parser.add_argument('--work', type=Path, default=Path('E:/cef-build'))
    parser.add_argument('--mode', choices=('config', 'update', 'skip'), default='config')
    args = parser.parse_args()
    try:
        lock = load_lock(args.lock)
        paths = source_paths(args.work) if args.mode == 'config' else check_worktree(lock, args.work, args.mode)
    except (ValueError, OSError, SyntaxError) as error:
        parser.exit(1, str(error)+'\n')
    print(json.dumps({'source':lock, 'paths':{key:str(value) for key,value in paths.items()}, 'mode':args.mode}, indent=2))


if __name__ == '__main__':
    main()
