"""Saves one of Red's engine patches (cef-patches/NN-name.{cef,chromium}.patch) from the
current CEF checkout, when patches stack (several patches change the same file).

The patch is the diff of the listed files between "pristine + every earlier Red patch"
(for Chromium files also CEF's own patch/patches/*.patch that touch them) and the current
working tree. The working files are restored afterwards. Hunks of LATER patches that live
in the same files must be taken out of the tree first (or they land in this patch).

  python scripts/save_cef_patch.py 11-live-page-transform cef include/cef_browser.h libcef/browser/browser_host_base.cc
  python scripts/save_cef_patch.py 12-translucent-window chromium ui/views/win/hwnd_message_handler.cc --cef-base views_widget

Patch files are LF; CEF sources are CRLF (edit them with scripts/cefedit.py or keep endings).
CEF's own patches use -p0. Verify with: git -C <dir> apply --check --reverse <patch>.
"""
import argparse, os, shutil, subprocess, tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, 'cef-patches')

def git_apply(directory, files, patch, p0=False):
    subprocess.run(['git', '-C', directory, 'apply', '--ignore-whitespace'] + (['-p0'] if p0 else []) +
                   ['--include=' + f for f in files] + [patch], check=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('name', help='patch name without extension, e.g. 11-live-page-transform')
    parser.add_argument('kind', choices=['cef', 'chromium'])
    parser.add_argument('files', nargs='+', help='paths relative to chromium/src/cef (cef) or chromium/src (chromium)')
    parser.add_argument('--work', default='E:/cef-build')
    parser.add_argument('--cef-base', action='append', default=[], help="CEF patch/patches/<name>.patch also in the baseline")
    args = parser.parse_args()
    src = os.path.join(args.work, 'code', 'chromium', 'src')
    directory = os.path.join(src, 'cef') if args.kind == 'cef' else src
    if os.path.isfile(os.path.join(src, 'cef', 'RED_ENGINE.json')):
        raise SystemExit('sol-156 owns these changes: commit CEF edits in the fork and update its native Chromium patches; legacy patch export is disabled for this checkout')
    suffix = f'.{args.kind}.patch'
    earlier = sorted(os.path.join(OUT, p) for p in os.listdir(OUT) if p.endswith(suffix) and p < args.name)
    tmp = tempfile.mkdtemp()
    try:
        for f in args.files:
            os.makedirs(os.path.dirname(os.path.join(tmp, 'after', f)), exist_ok=True)
            shutil.copy2(os.path.join(directory, f), os.path.join(tmp, 'after', f))
        subprocess.run(['git', '-C', directory, 'checkout', '--'] + args.files, check=True)
        try:
            for base in args.cef_base:
                git_apply(directory, args.files, os.path.join(src, 'cef', 'patch', 'patches', base + '.patch'), p0=True)
            for patch in earlier:
                git_apply(directory, args.files, patch)
            diff = ''
            for f in args.files:
                os.makedirs(os.path.dirname(os.path.join(tmp, 'before', f)), exist_ok=True)
                shutil.copy2(os.path.join(directory, f), os.path.join(tmp, 'before', f))
                r = subprocess.run(['git', 'diff', '--no-index', '--', 'before/' + f, 'after/' + f], cwd=tmp,
                                   capture_output=True, text=True, encoding='utf-8')
                diff += r.stdout.replace('a/before/', 'a/').replace('b/after/', 'b/').replace('\r\n', '\n')
        finally:
            for f in args.files:
                shutil.copy2(os.path.join(tmp, 'after', f), os.path.join(directory, f))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    target = os.path.join(OUT, args.name + suffix)
    with open(target, 'w', encoding='utf-8', newline='\n') as file:
        file.write(diff)
    print(target, len(diff.splitlines()), 'lines')

if __name__ == '__main__':
    main()
