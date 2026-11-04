"""Exercise CMake's semantic API gate without configuring the Windows app."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def find_cmake():
    if found := shutil.which('cmake'):
        return found
    # CMake bundled with Visual Studio need not be on PATH.
    for cache in (ROOT / 'build').glob('*/CMakeCache.txt'):
        for line in cache.read_text(encoding='utf-8').splitlines():
            if line.startswith('CMAKE_COMMAND:INTERNAL='):
                path = line.split('=', 1)[1]
                if Path(path).is_file():
                    return path
    return None


class ContentTransformTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cmake = find_cmake()
        if not cls.cmake:
            raise unittest.SkipTest('CMake is required to exercise the API gate')

    def check_header(self, header, enabled=False, rejected=False, container=False):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / 'check.cmake'
            script.write_text(
                f'include("{(ROOT / "cmake/cef_content_transform.cmake").as_posix()}")\n'
                f'red_check_cef_{"page_container" if container else "content_transform"}([==[{header}]==] actual)\n'
                f'if(NOT "${{actual}}" STREQUAL "{str(enabled).upper()}")\n'
                'message(FATAL_ERROR "Unexpected capability result: ${actual}")\nendif()\n',
                encoding='utf-8')
            run = subprocess.run([self.cmake, '-P', str(script)], capture_output=True, text=True)
        if rejected:
            self.assertNotEqual(run.returncode, 0)
            self.assertIn('page container API' if container else 'requires the dim revision', run.stderr)
        else:
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_stock_cef_keeps_snapshot_fallback(self):
        self.check_header('class CefBrowserHost {}; // SetContentTransform unavailable')

    def test_dim_revision_lf_and_crlf(self):
        for ending in ('\n', '\r\n'):
            with self.subTest(ending=repr(ending)):
                self.check_header(ending.join([
                    'virtual void SetContentTransform(double scale,',
                    '    double dim,', '    int duration_ms) = 0;']), enabled=True)

    def test_old_opacity_revision_is_rejected(self):
        self.check_header('virtual void SetContentTransform(double scale, double opacity, int duration_ms) = 0;', rejected=True)

    def test_unknown_signature_fails_closed(self):
        self.check_header('virtual void SetContentTransform(double scale, double value, int duration_ms) = 0;', rejected=True)

    def test_container_is_separate_from_legacy_live_transform(self):
        self.check_header('virtual void SetContentTransform(double scale, double dim, int duration_ms) = 0;', container=True)

    def test_container_signature(self):
        self.check_header('virtual void SetContentContainerTransform(double scale, double dim, int duration_ms, cef_color_t veil_color) = 0;', enabled=True, container=True)

    def test_unknown_container_api_fails_closed(self):
        self.check_header('virtual void SetContentContainerTransform(double scale, double opacity, int duration_ms, cef_color_t veil_color) = 0;', rejected=True, container=True)


if __name__ == '__main__':
    unittest.main()
