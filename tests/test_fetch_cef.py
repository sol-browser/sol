"""Exercise dependency integrity and archive path confinement without networking."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

MODULE = Path(__file__).resolve().parents[1] / "scripts/fetch_cef.py"
spec = importlib.util.spec_from_file_location("fetch_cef", MODULE)
fetch_cef = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fetch_cef)


class FetchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / ".cache").mkdir()
        (self.root / "cmake").mkdir()
        self.archive = self.root / ".cache/probe.tar.bz2"

    def archive_with(self, member="probe/include/cef_version.h", symlink=False):
        with tarfile.open(self.archive, "w:bz2") as bundle:
            info = tarfile.TarInfo(member)
            if symlink:
                info.type = tarfile.SYMTYPE
                info.linkname = "../../escape"
                bundle.addfile(info)
            else:
                info.size = 3
                bundle.addfile(info, io.BytesIO(b"CEF"))
        lock = {"archive": self.archive.name, "size": self.archive.stat().st_size,
                "sha256": hashlib.sha256(self.archive.read_bytes()).hexdigest()}
        (self.root / "cmake/cef.lock.json").write_text(json.dumps(lock))
        return lock

    def fetch(self):
        with patch.object(fetch_cef, "ROOT", self.root), patch("urllib.request.urlopen", side_effect=AssertionError("Unexpected network")):
            return fetch_cef.fetch()

    def test_valid_archive_and_reuse(self):
        self.archive_with()
        destination = self.fetch()
        self.assertEqual((destination / "include/cef_version.h").read_bytes(), b"CEF")
        self.assertEqual(self.fetch(), destination)

    def test_modified_content_is_rejected(self):
        self.archive_with()
        data = bytearray(self.archive.read_bytes())
        data[len(data) // 2] ^= 1
        self.archive.write_bytes(data)
        with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
            self.fetch()
        self.assertFalse((self.root / "third_party").exists())

    def test_truncated_archive_is_rejected(self):
        self.archive_with()
        self.archive.write_bytes(b"truncated")
        with self.assertRaisesRegex(RuntimeError, "Wrong archive size"):
            self.fetch()

    def test_path_traversal_is_rejected(self):
        self.archive_with("probe/../../escape")
        with self.assertRaisesRegex(RuntimeError, "Unsafe archive member"):
            self.fetch()
        self.assertFalse((self.root / "escape").exists())

    def test_symlink_is_rejected(self):
        self.archive_with("probe/link", symlink=True)
        with self.assertRaisesRegex(RuntimeError, "Unsafe archive member"):
            self.fetch()

    def test_unverified_destination_is_not_overwritten(self):
        self.archive_with()
        destination = self.root / "third_party/cef"
        destination.mkdir(parents=True)
        with self.assertRaisesRegex(RuntimeError, "Unverified extraction"):
            self.fetch()

    def test_missing_local_build_is_reported(self):
        lock = self.archive_with()
        self.archive.unlink()
        (self.root / "cmake/cef.lock.json").write_text(json.dumps(lock))
        with self.assertRaisesRegex(RuntimeError, "build_cef.ps1"):
            self.fetch()

    def test_switching_locked_distributions_parks_the_verified_one(self):
        first = self.archive_with()
        self.fetch()
        # A second build with the same root directory, e.g. our codecs build.
        other = self.root / ".cache/probe-codecs.tar.bz2"
        with tarfile.open(other, "w:bz2") as bundle:
            info = tarfile.TarInfo("probe/include/cef_version.h"); info.size = 6
            bundle.addfile(info, io.BytesIO(b"CODECS"))
        second = {"archive": other.name, "root": "probe", "size": other.stat().st_size,
                  "sha256": hashlib.sha256(other.read_bytes()).hexdigest()}
        (self.root / "cmake/cef.lock.json").write_text(json.dumps(second))
        destination = self.fetch()
        self.assertEqual((destination / "include/cef_version.h").read_bytes(), b"CODECS")
        self.assertTrue((self.root / f"third_party/cef-{first['sha256'][:12]}").is_dir())
        # Switching back still requires and verifies the archive first.
        self.archive.unlink()
        first["archive"] = "probe.tar.bz2"
        (self.root / "cmake/cef.lock.json").write_text(json.dumps(first))
        with self.assertRaises(RuntimeError):
            self.fetch()  # The archive itself is still required and verified.


if __name__ == "__main__":
    unittest.main()
