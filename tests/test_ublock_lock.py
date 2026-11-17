"""The uBlock Origin lock gives the official extension ID (scripts/fetch_ublock.py)."""
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import fetch_ublock  # noqa: E402


class UBlockLockTest(unittest.TestCase):
    def setUp(self):
        self.lock = json.loads((ROOT / "cmake/ublock.lock.json").read_text(encoding="utf-8"))

    def test_key_gives_locked_id(self):
        self.assertEqual(fetch_ublock.extension_id(self.lock["manifest_key"]),
                         "fkgkibajhfbepljeaefdnfnegdcjomkh")
        self.assertEqual(self.lock["extension_id"], "fkgkibajhfbepljeaefdnfnegdcjomkh")

    def test_download_is_pinned(self):
        self.assertTrue(self.lock["url"].startswith("https://github.com/gorhill/uBlock/releases/download/"))
        self.assertIn(self.lock["version"], self.lock["url"])
        self.assertRegex(self.lock["sha256"], r"^[0-9a-f]{64}$")


if __name__ == "__main__":
    unittest.main()
