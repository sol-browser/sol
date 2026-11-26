"""Native sources stay within MSVC's limits for string literals.

MSVC rejects a single string literal longer than 16380 bytes (C2026). A Windows
checkout with CRLF line endings makes a multi-line raw literal one byte longer
per line, so a probe script that built here can fail in CI (Sol 0.12.1's first
tag build: menu_probe.cpp). Long scripts are split into adjacent raw literals.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
LIMIT = 16380
RAW = re.compile(r'R"([^ ()\\\t\v\f\n]{0,16})\((.*?)\)\1"', re.S)


class SourceLimitsTest(unittest.TestCase):
    def test_raw_string_literals_fit_msvc(self):
        found = []
        for path in (ROOT / 'apps').rglob('*'):
            if path.suffix not in ('.cpp', '.h') or 'node_modules' in path.parts:
                continue
            text = path.read_bytes().decode('utf-8', 'replace').replace('\r\n', '\n')
            for match in RAW.finditer(text):
                body = match.group(2)
                size = len(body.encode('utf-8')) + body.count('\n')  # As a CRLF checkout sees it.
                if size > LIMIT:
                    found.append(f'{path.relative_to(ROOT)}:{text.count(chr(10), 0, match.start()) + 1} ({size} bytes)')
        self.assertEqual(found, [], 'split these raw string literals into adjacent literals under 16 KB')


if __name__ == '__main__':
    unittest.main()
