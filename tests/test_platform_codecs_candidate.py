import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('candidate', Path(__file__).resolve().parents[1] / 'scripts/platform_codecs_candidate.py')
candidate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)


class CandidateTests(unittest.TestCase):
    def test_unexpected_or_duplicate_source_rejected(self):
        for raw in (b'other', b'old old'):
            with self.assertRaises(ValueError):
                candidate.transform(raw, [('old', 'new')])

    def test_restore_preserves_concurrent_edit_in_every_file(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            receipt = root / 'receipt'
            receipt.mkdir()
            files = []
            for i in range(2):
                (root / f'source{i}').write_bytes(b'candidate')
                (receipt / f'{i}.before').write_bytes(b'original\r\n')
                (receipt / f'{i}.after').write_bytes(b'candidate')
                files.append({'path': f'source{i}', 'before': candidate.sha(b'original\r\n'), 'after': candidate.sha(b'candidate')})
            (receipt / 'manifest.json').write_text(json.dumps({'source': str(root), 'files': files}))
            (root / 'source1').write_bytes(b'concurrent edit')
            with self.assertRaises(ValueError):
                candidate.restore(receipt)
            self.assertEqual((root / 'source0').read_bytes(), b'candidate')
            self.assertEqual((root / 'source1').read_bytes(), b'concurrent edit')
            (root / 'source1').write_bytes(b'candidate')
            candidate.restore(receipt)
            self.assertEqual((root / 'source0').read_bytes(), b'original\r\n')

    def test_line_endings_preserved(self):
        for newline in ('\n', '\r\n'):
            raw = ('a' + newline + 'old' + newline).encode()
            self.assertEqual(candidate.transform(raw, [('old', 'new')]), ('a' + newline + 'new' + newline).encode())


if __name__ == '__main__':
    unittest.main()
