"""Launch the actual built bootstrap to verify unsafe switches fail closed."""
from pathlib import Path
import subprocess
import os
import unittest

EXE = Path(os.environ.get('RED_BROWSER_TEST_EXE', Path(__file__).resolve().parents[1] / "build/Release/apps/desktop/Release/sol.exe"))


@unittest.skipUnless(EXE.exists(), "Build the Release host before CLI integration tests")
class CliTests(unittest.TestCase):
    def test_unsafe_and_unrecognized_switches_are_rejected(self):
        for argument in ["--no-sandbox", "--disable-web-security", "--ignore-certificate-errors",
                         "--remote-debugging-port=9222", "--disable-features=SiteIsolationTrialOptOut",
                         "--load-extension=unapproved", "--probe-lifecycle"]:
            with self.subTest(argument=argument):
                result = subprocess.run([str(EXE), argument], cwd=EXE.parent,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
                self.assertIn(result.returncode, (10, 11))


if __name__ == "__main__":
    unittest.main()
