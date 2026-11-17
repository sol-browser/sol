"""Regenerate a dev SDK's CMake files from the engine checkout (scripts/dev_red.ps1).

The dev SDK (devdist) starts as a copy of the pinned distribution. After an
engine move to a new CEF branch its wrapper source list and CMake helpers no
longer match the checkout's headers, so build them from the checkout's own
templates and transfer list as make_distrib.py does:
  python dev_sdk_cmake.py <checkout>/cef <devdist>
"""
import os
import shutil
import sys


def main(cef_dir, dist_dir):
    sys.path.insert(0, os.path.join(cef_dir, 'tools'))
    from file_util import eval_file  # noqa: E402 (CEF's own tools)
    from make_cmake import process_cmake_template  # noqa: E402
    variables = eval_file(os.path.join(cef_dir, 'cef_paths.gypi'))['variables']
    variables.update(eval_file(os.path.join(cef_dir, 'cef_paths2.gypi'))['variables'])
    # Files make_distrib.py copies in from Chromium over the checkout's forwarding stubs.
    for entry in eval_file(os.path.join(cef_dir, 'tools', 'distrib', 'transfer.cfg')):
        if 'source' in entry and 'post-process' not in entry:
            target = os.path.join(dist_dir, *entry['target'].split('/'))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(os.path.normpath(os.path.join(cef_dir, entry['source'])), target)
    for source, target in ((('cmake', 'cef_macros.cmake.in'), ('cmake', 'cef_macros.cmake')),
                           (('cmake', 'cef_variables.cmake.in'), ('cmake', 'cef_variables.cmake')),
                           (('cmake', 'FindCEF.cmake.in'), ('cmake', 'FindCEF.cmake')),
                           (('libcef_dll', 'CMakeLists.txt.in'), ('libcef_dll', 'CMakeLists.txt'))):
        process_cmake_template(os.path.join(cef_dir, *source), os.path.join(dist_dir, *target), variables, True)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]))
