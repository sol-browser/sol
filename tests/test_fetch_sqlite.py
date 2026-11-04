import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location("fetch_sqlite", Path(__file__).resolve().parents[1] / "scripts/fetch_sqlite.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SqliteDownloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / ".cache").mkdir()
        (self.root / "cmake").mkdir()
        self.archive = self.root / ".cache/sqlite-test.zip"
        with zipfile.ZipFile(self.archive, "w") as bundle:
            bundle.writestr("sqlite-test/sqlite3.c", "source")
            bundle.writestr("sqlite-test/sqlite3.h", "header")
            bundle.writestr("sqlite-test/../../escape", "must never be extracted")
        lock = {"archive": self.archive.name, "size": self.archive.stat().st_size,
                "sha256": hashlib.sha256(self.archive.read_bytes()).hexdigest()}
        (self.root / "cmake/sqlite.lock.json").write_text(json.dumps(lock))

    def fetch(self):
        with patch.object(module, "ROOT", self.root), patch("urllib.request.urlopen", side_effect=AssertionError("Unexpected network")):
            module.fetch()

    def test_only_named_sources_are_extracted(self):
        self.fetch()
        self.assertEqual((self.root / "third_party/sqlite/sqlite3.c").read_text(), "source")
        self.assertFalse((self.root / "escape").exists())

    def test_modified_archive_is_rejected(self):
        data = bytearray(self.archive.read_bytes())
        data[20] ^= 1
        self.archive.write_bytes(data)
        with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
            self.fetch()
        self.assertFalse((self.root / "third_party").exists())

    def test_modified_extraction_is_repaired_from_verified_archive(self):
        self.fetch()
        (self.root / "third_party/sqlite/sqlite3.h").write_text("modified")
        self.fetch()
        self.assertEqual((self.root / "third_party/sqlite/sqlite3.h").read_text(), "header")


if __name__ == "__main__":
    unittest.main()
