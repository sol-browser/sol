"""sol-sync end to end: two Sol profiles sync their workspace through one folder.

Uses the built tool (SOL_SYNC_TOOL, else build/Release) on disposable
profiles; no Sol window is started. Workspaces are created by the tool's own
store and edited with sqlite3 the way Sol's store writes them.
"""
from contextlib import closing
import os
from pathlib import Path
import sqlite3
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOL = Path(os.environ.get("SOL_SYNC_TOOL", ROOT / "build/Release/apps/desktop/Release/sol-sync.exe"))
PASSPHRASE = "correct horse battery"


def run(*args, passphrase=PASSPHRASE):
    env = dict(os.environ, SOL_SYNC_PASSPHRASE=passphrase)
    return subprocess.run([str(TOOL), *map(str, args)], capture_output=True, text=True, encoding="utf-8", env=env, timeout=120)


def database(profile):
    # sqlite3's own context manager commits but never closes; Windows then keeps the file locked.
    return closing(sqlite3.connect(profile / "BrowserData" / "workspace.sqlite3"))


def spaces(profile):
    with database(profile) as db:
        return sorted(name for (name,) in db.execute("SELECT name FROM spaces"))


@unittest.skipUnless(TOOL.exists(), f"sol-sync not built at {TOOL}")
class SolSyncTool(unittest.TestCase):
    def test_two_profiles_sync_spaces(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            a, b, cloud = temp / "A", temp / "B", temp / "Cloud"
            cloud.mkdir()
            for profile in (a, b):
                (profile / "BrowserData").mkdir(parents=True)  # As Sol's first start leaves it.
            # A pass on a profile that is not set up says so.
            self.assertIn("Not set up", run("pass", "--profile", a).stderr)

            self.assertEqual(run("setup", "--profile", a, "--folder", cloud).returncode, 0)
            first = run("pass", "--profile", a)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertIn("device file written", first.stdout)
            with database(a) as db, db:
                db.execute("UPDATE spaces SET name='Synced from A'")
            self.assertIn("device file written", run("pass", "--profile", a).stdout)
            self.assertIn("already current", run("pass", "--profile", a).stdout)

            wrong = run("setup", "--profile", b, "--folder", cloud, passphrase="not the passphrase")
            self.assertNotEqual(wrong.returncode, 0)
            self.assertIn("Wrong passphrase", wrong.stderr)
            self.assertEqual(run("setup", "--profile", b, "--folder", cloud, "--join", "synced").returncode, 0)
            joined = run("pass", "--profile", b)
            self.assertEqual(joined.returncode, 0, joined.stderr)
            self.assertIn("workspace updated", joined.stdout)
            self.assertEqual(spaces(b), ["Synced from A"])

            status = run("status", "--profile", b)
            self.assertEqual(status.returncode, 0, status.stderr)
            self.assertIn("(this PC)", status.stdout)
            self.assertEqual(status.stdout.count("records"), 2)

            for path in (cloud / "Sol Sync").rglob("*"):
                if path.is_file():
                    self.assertNotIn(b"Synced from A", path.read_bytes())
            self.assertEqual(run("off", "--profile", b).returncode, 0)
            self.assertEqual(len(list((cloud / "Sol Sync" / "devices").glob("*.sol"))), 1)


if __name__ == "__main__":
    unittest.main()
