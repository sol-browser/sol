"""Source integrity checks use disposable Git repositories and never fetch."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1]/'scripts'/'cef_source.py'
spec = importlib.util.spec_from_file_location('cef_source', MODULE)
cef_source = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cef_source)


def sample_lock():
    return {'repository':'https://github.com/example/cef.git', 'revision':'1'*40,
            'branch':'sol-156', 'upstream_repository':'https://github.com/chromiumembedded/cef.git',
            'upstream_revision':'2'*40, 'cef_branch':'8078',
            'chromium_tag':'refs/tags/156.0.8078.48', 'chromium_revision':'3'*40}


class ConfigTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root/'cef-source.lock.json'

    def load(self, **changes):
        lock = sample_lock()
        lock.update(changes)
        self.path.write_text(json.dumps(lock), encoding='utf-8')
        return cef_source.load_lock(self.path)

    def test_valid_source_lock_leaves_binary_lock_unchanged(self):
        binary = self.root/'cef.lock.json'
        binary.write_bytes(b'{"archive":"old.tar.bz2","sha256":"old"}\n')
        before = (binary.read_bytes(), binary.stat().st_mtime_ns)
        self.assertEqual(self.load(), sample_lock())
        cef_source.source_paths(self.root/'not-created')
        self.assertEqual((binary.read_bytes(), binary.stat().st_mtime_ns), before)
        self.assertFalse((self.root/'not-created').exists())

    def test_unsafe_repository_urls_are_rejected(self):
        for url in ('http://github.com/example/cef.git', 'git@github.com:example/cef.git',
                    'https://name:secret@github.com/example/cef.git',
                    'https://github.com/example/cef.git?ref=main',
                    'https://github.com/example/cef.git#main',
                    'https://github.com/example/../cef.git',
                    'https://github.com/example/%2e%2e/cef.git',
                    'https://github.com/example/cef.git;echo',
                    'https://github.com/example/cef.git\n', 'https://github.com/'):
            with self.subTest(url=url), self.assertRaises(ValueError):
                self.load(repository=url)

    def test_full_commit_pins_are_required(self):
        for key in ('revision', 'upstream_revision', 'chromium_revision'):
            for value in ('main', '1234567', 'a'*39, 'A'*40, '-bad', None):
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    self.load(**{key:value})

    def test_refs_and_chromium_branch_must_be_canonical(self):
        for value in ('-main', 'main..other', 'branch.lock', '.hidden', 'a//b', 'a/', 'main@{1}'):
            with self.subTest(branch=value), self.assertRaises(ValueError):
                self.load(branch=value)
        for value in ('156.0.8078.48', 'refs/heads/8078', 'refs/tags/156.0.8079.48',
                      'refs/tags/156.0.8078.48;echo'):
            with self.subTest(tag=value), self.assertRaises(ValueError):
                self.load(chromium_tag=value)

    def test_binary_lock_fields_are_not_accepted_as_source_pins(self):
        for lock in ({'archive':'old.tar.bz2'}, {**sample_lock(), 'sha256':'a'*64}):
            self.path.write_text(json.dumps(lock), encoding='utf-8')
            with self.assertRaises(ValueError): cef_source.load_lock(self.path)

    def test_shell_characters_in_work_path_are_rejected(self):
        with self.assertRaises(ValueError): cef_source.source_paths(self.root/'bad&path')


class CheckoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.lock = sample_lock()
        seed = self.root/'seed'
        seed.mkdir()
        self.run_git(seed, 'init', '-q')
        self.run_git(seed, 'config', 'core.autocrlf', 'false')
        (seed/'CHROMIUM_BUILD_COMPATIBILITY.txt').write_text(
            "# Exact Chromium tag\n{'chromium_checkout':'refs/tags/156.0.8078.48'}\n", encoding='utf-8')
        self.commit(seed)
        self.lock['upstream_revision'] = self.run_git(seed, 'rev-parse', 'HEAD')
        (seed/'fork.txt').write_text('versioned changes\n', encoding='utf-8')
        self.commit(seed)
        self.lock['revision'] = self.run_git(seed, 'rev-parse', 'HEAD')
        self.paths = cef_source.source_paths(self.root/'work')
        for key in ('bootstrap', 'checkout', 'build_checkout'):
            path = self.paths[key]
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(seed, path)
            self.run_git(path, 'remote', 'add', 'origin', self.lock['repository'])
        chromium = self.paths['chromium']
        self.run_git(chromium, 'init', '-q')
        self.run_git(chromium, 'config', 'core.autocrlf', 'false')
        (chromium/'.gitignore').write_text('cef/\n', encoding='utf-8')
        (chromium/'source.txt').write_text('upstream\n', encoding='utf-8')
        self.commit(chromium)
        self.run_git(chromium, 'remote', 'add', 'origin', cef_source.CHROMIUM_REPOSITORY)
        self.lock['chromium_revision'] = self.run_git(chromium, 'rev-parse', 'HEAD')

    def run_git(self, path, *args):
        result = subprocess.run(['git', '-C', str(path), *args], capture_output=True, text=True)
        if result.returncode:
            self.fail(f'Git fixture failed: {args}: {result.stderr}')
        return result.stdout.strip()

    def commit(self, path):
        self.run_git(path, 'add', '.')
        self.run_git(path, '-c', 'user.name=Source test', '-c', 'user.email=source@example.invalid',
                     'commit', '-qm', 'fixture')

    def check(self, mode='skip'):
        return cef_source.check_worktree(self.lock, self.root/'work', mode)

    def test_exact_checkouts_are_accepted_without_mutation(self):
        before = self.run_git(self.paths['build_checkout'], 'status', '--porcelain')
        self.assertEqual(self.check(), self.paths)
        self.assertEqual(self.run_git(self.paths['build_checkout'], 'status', '--porcelain'), before)

    def test_dirty_cef_cannot_be_built_or_replaced(self):
        (self.paths['build_checkout']/'unsaved.txt').write_text('keep me', encoding='utf-8')
        for mode in ('skip', 'update'):
            with self.subTest(mode=mode), self.assertRaisesRegex(ValueError, 'Dirty checkout'):
                self.check(mode)
        self.assertEqual((self.paths['build_checkout']/'unsaved.txt').read_text(), 'keep me')

    def test_old_actual_cef_copy_cannot_pass_skip_update(self):
        self.run_git(self.paths['build_checkout'], 'checkout', '-q', self.lock['upstream_revision'])
        with self.assertRaisesRegex(ValueError, 'CEF HEAD'):
            self.check()

    def test_wrong_origin_is_not_silently_repointed(self):
        self.run_git(self.paths['checkout'], 'remote', 'set-url', 'origin', self.lock['upstream_repository'])
        with self.assertRaisesRegex(ValueError, 'origin'):
            self.check('update')
        self.assertEqual(self.run_git(self.paths['checkout'], 'remote', 'get-url', 'origin'),
                         self.lock['upstream_repository'])

    def test_chromium_patch_tree_can_build_but_cannot_be_synced(self):
        source = self.paths['chromium']/'source.txt'
        source.write_text('CEF patch.cfg changes\n', encoding='utf-8')
        self.check()
        with self.assertRaisesRegex(ValueError, 'Dirty checkout'):
            self.check('update')
        self.assertEqual(source.read_text(), 'CEF patch.cfg changes\n')

    def test_wrong_chromium_head_is_rejected(self):
        self.lock['chromium_revision'] = 'a'*40
        with self.assertRaisesRegex(ValueError, 'Chromium HEAD'):
            self.check()

    def test_clean_old_chromium_can_sync_but_cannot_skip_update(self):
        chromium = self.paths['chromium']
        previous = self.lock['chromium_revision']
        (chromium/'source.txt').write_text('next upstream release\n', encoding='utf-8')
        self.commit(chromium)
        self.lock['chromium_revision'] = self.run_git(chromium, 'rev-parse', 'HEAD')
        self.run_git(chromium, 'checkout', '-q', previous)
        self.check('update')
        with self.assertRaisesRegex(ValueError, 'Chromium HEAD'):
            self.check()

    def test_incompatible_cef_metadata_is_rejected(self):
        self.lock['chromium_tag'] = 'refs/tags/156.0.8078.49'
        with self.assertRaisesRegex(ValueError, 'compatibility'):
            self.check()


if __name__ == '__main__':
    unittest.main()
