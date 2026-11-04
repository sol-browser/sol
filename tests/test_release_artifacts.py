"""Artifact checks reject altered bytes, unsafe paths and stale source copies."""
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import tarfile
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('verify_installer', ROOT / 'scripts/verify_installer.py')
verify_installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify_installer)
spec = importlib.util.spec_from_file_location('snapshot_release', ROOT / 'scripts/snapshot_release.py')
snapshot_release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(snapshot_release)


def fake_pe(code=b'code', resource=b'icon', stack=0x800000):
    data = bytearray(1024)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', data, 0x84, 0x8664, 3)
    struct.pack_into('<H', data, 0x94, 0xf0)
    optional = 0x98
    struct.pack_into('<H', data, optional, 0x20b)
    struct.pack_into('<I', data, optional + 16, 0x1000)
    struct.pack_into('<H', data, optional + 68, 2)
    struct.pack_into('<Q', data, optional + 72, stack)
    for index, (name, payload, pointer) in enumerate(((b'.text', code, 0x240), (b'.rsrc', resource, 0x340),
                                                     (b'.reloc', b'relocs', 0x3c0))):
        section = optional + 0xf0 + 40 * index
        data[section:section + 8] = name.ljust(8, b'\0')
        struct.pack_into('<IIII', data, section + 8, len(payload), 0x1000 * (index + 1), len(payload), pointer)
        struct.pack_into('<I', data, section + 36, 0x60000020 if index == 0 else 0x40000040)
        data[pointer:pointer + len(payload)] = payload
    return bytes(data)


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.stage = self.root / 'stage'
        (self.stage / 'ui').mkdir(parents=True)
        (self.stage / 'ui/index.html').write_bytes(b'current ui')
        self.setup = self.root / 'Sol-Setup-0.8.0.exe'
        self.setup.write_bytes(b'installer')
        self.manifest = {'version': '0.8.0', 'installer': self.setup.name,
                         'sha256': verify_installer.sha256(self.setup), 'files': [
                             {'path': 'ui\\index.html', 'size': 10,
                              'sha256': verify_installer.sha256(self.stage / 'ui/index.html')}]}

    def verify(self):
        return verify_installer.verify_manifest(self.manifest, self.stage, self.setup, '0.8.0')

    def test_matching_manifest_checks_case_insensitive_windows_names(self):
        self.manifest['files'][0]['path'] = 'UI\\index.html'
        digest, files = self.verify()
        self.assertEqual(digest, verify_installer.sha256(self.setup))
        self.assertEqual(set(files), {'ui/index.html'})

    def test_changed_setup_rejected(self):
        self.setup.write_bytes(b'installer changed')
        with self.assertRaisesRegex(ValueError, 'Installer SHA-256 mismatch'):
            self.verify()

    def test_same_size_staged_tamper_rejected(self):
        (self.stage / 'ui/index.html').write_bytes(b'evil!!!!!?')
        with self.assertRaisesRegex(ValueError, 'Staged file hash/size mismatch'):
            self.verify()

    def test_unmanifested_stale_bundle_rejected(self):
        (self.stage / 'ui/stale.js').write_bytes(b'previous release')
        with self.assertRaisesRegex(ValueError, 'file set mismatch'):
            self.verify()

    def test_missing_manifest_file_rejected(self):
        (self.stage / 'ui/index.html').unlink()
        with self.assertRaisesRegex(ValueError, 'file set mismatch'):
            self.verify()

    def test_duplicate_case_alias_rejected(self):
        entry = dict(self.manifest['files'][0], path='UI/index.html')
        self.manifest['files'].append(entry)
        with self.assertRaisesRegex(ValueError, 'Duplicate manifest'):
            self.verify()

    def test_manifest_cannot_name_files_outside_stage_or_windows_aliases(self):
        for path in ('../outside', '/outside', 'C:/outside', '\\\\server\\outside',
                     'ui//index.html', 'ui/./index.html', 'file:stream', 'aux.txt', 'ui/trailing.'): 
            with self.subTest(path=path), self.assertRaises(ValueError):
                verify_installer.relative_name(path)

    def test_boolean_size_is_not_an_integer_file_length(self):
        self.manifest['files'][0]['size'] = True
        with self.assertRaisesRegex(ValueError, 'Invalid file size'):
            self.verify()


class PeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.original = self.root / 'original.exe'
        self.branded = self.root / 'branded.exe'
        self.original.write_bytes(fake_pe())

    def test_resource_only_branding_preserves_code(self):
        self.branded.write_bytes(fake_pe(resource=b'new bigger icon and version resource'))
        self.assertEqual(verify_installer.compare_bootstrap(self.original, self.branded)['stack_reserve'], 0x800000)

    def test_code_change_cannot_hide_in_branding_exception(self):
        self.branded.write_bytes(fake_pe(code=b'evil', resource=b'new icon'))
        with self.assertRaisesRegex(ValueError, 'outside branding'):
            verify_installer.compare_bootstrap(self.original, self.branded)

    def test_resource_growth_can_rebase_unchanged_reloc_section(self):
        branded = bytearray(fake_pe(resource=b'larger icon'))
        struct.pack_into('<I', branded, 0x98 + 0xf0 + 40 * 2 + 12, 0x4000)
        self.branded.write_bytes(branded)
        verify_installer.compare_bootstrap(self.original, self.branded)
        # The SDK -> build comparison permits only the expected stack edit.
        with self.assertRaisesRegex(ValueError, 'outside branding'):
            verify_installer.compare_bootstrap(self.original, self.branded, False)

    def test_stack_changed_by_branding_is_rejected(self):
        self.branded.write_bytes(fake_pe(stack=0x100000))
        with self.assertRaisesRegex(ValueError, 'outside branding'):
            verify_installer.compare_bootstrap(self.original, self.branded)
        self.assertEqual(verify_installer.compare_bootstrap(self.original, self.branded, False)['stack_reserve'], 0x100000)

    def test_truncated_pe_rejected(self):
        self.branded.write_bytes(b'MZ')
        with self.assertRaisesRegex(ValueError, 'Malformed PE'):
            verify_installer.pe_details(self.branded)


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sdk = self.root / 'sdk'
        self.contents = {'Release/bootstrap.exe': fake_pe(), 'include/cef_version.h': b'pinned header',
                         'cmake/cef_variables.cmake': b'pinned cmake', 'Release/libcef.dll': b'pinned engine'}
        for name, payload in self.contents.items():
            path = self.sdk / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
        self.archive = self.root / 'sdk.tar.bz2'

    def write_archive(self, changes=None, omit=None, duplicate=None):
        with tarfile.open(self.archive, 'w:bz2') as stream:
            for name, payload in self.contents.items():
                if name == omit:
                    continue
                payload = (changes or {}).get(name, payload)
                member = tarfile.TarInfo('cef_binary_pinned/' + name)
                member.size = len(payload)
                stream.addfile(member, io.BytesIO(payload))
                if name == duplicate:
                    stream.addfile(member, io.BytesIO(payload))

    def verify(self):
        return verify_installer.verify_sdk_archive(self.archive, 'cef_binary_pinned', self.sdk,
                                                    [self.sdk / 'Release/libcef.dll'])

    def test_same_sdk_bytes_proven_against_archive_without_extraction(self):
        self.write_archive()
        self.assertEqual(self.verify(), 4)
        self.assertFalse((self.root / 'cef_binary_pinned').exists())

    def test_sdk_drift_detected_even_when_stage_and_build_match_it(self):
        self.write_archive()
        (self.sdk / 'Release/libcef.dll').write_bytes(b'stale engine!')
        with self.assertRaisesRegex(ValueError, 'SDK differs from pinned archive'):
            self.verify()

    def test_missing_and_duplicate_archive_members_rejected(self):
        self.write_archive(omit='Release/libcef.dll')
        with self.assertRaisesRegex(ValueError, 'missing required'):
            self.verify()
        self.write_archive(duplicate='Release/libcef.dll')
        with self.assertRaisesRegex(ValueError, 'Duplicate/non-file'):
            self.verify()


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.git('init', '-q')
        self.git('config', 'user.email', 'fixture@example.invalid')
        self.git('config', 'user.name', 'Fixture')
        (self.root / 'VERSION').write_text('0.8.0\n')
        (self.root / '.gitignore').write_text('/artifacts/\n')
        (self.root / 'source.txt').write_text('committed')
        (self.root / 'removed.txt').write_text('deleted later')
        (self.root / 'docs/evidence').mkdir(parents=True)
        (self.root / 'docs/evidence/private.json').write_text('local evidence')
        self.git('add', '.')
        self.git('commit', '-qm', 'fixture')

    def git(self, *arguments):
        return subprocess.run(['git', *arguments], cwd=self.root, check=True,
                              capture_output=True, encoding='utf-8').stdout

    def test_checkpoint_records_current_working_bytes_and_omissions(self):
        (self.root / 'source.txt').write_text('staged')
        self.git('add', 'source.txt')
        (self.root / 'source.txt').write_text('working bytes')
        (self.root / 'untracked.txt').write_text('new source')
        (self.root / 'removed.txt').unlink()
        before = self.git('status', '--porcelain=v1', '--untracked-files=all')
        output = self.root / 'artifacts/release-0.8.0'
        receipt = snapshot_release.snapshot(self.root, '0.8.0', output)
        self.assertEqual(receipt, json.loads((output / 'snapshot.json').read_text(encoding='utf-8')))
        self.assertEqual(receipt['sha256'], verify_installer.sha256(output / 'source-snapshot.zip'))
        with zipfile.ZipFile(output / 'source-snapshot.zip') as bundle:
            self.assertEqual(bundle.read('source.txt'), b'working bytes')
            self.assertEqual(bundle.read('untracked.txt'), b'new source')
            self.assertNotIn('docs/evidence/private.json', bundle.namelist())
            self.assertNotIn('removed.txt', bundle.namelist())
            for entry in receipt['files']:
                data = bundle.read(entry['path'])
                self.assertEqual(len(data), entry['size'])
        omitted = {entry['path']: entry['reason'] for entry in receipt['omitted']}
        self.assertIn('evidence', omitted['docs/evidence/private.json'])
        self.assertIn('deleted', omitted['removed.txt'])
        self.assertEqual(before, self.git('status', '--porcelain=v1', '--untracked-files=all'))
        with self.assertRaises(FileExistsError):
            snapshot_release.snapshot(self.root, '0.8.0', output)
        self.assertEqual(receipt['sha256'], verify_installer.sha256(output / 'source-snapshot.zip'))

    def test_output_cannot_escape_artifacts_and_version_cannot_drift(self):
        with self.assertRaisesRegex(ValueError, 'artifacts'):
            snapshot_release.snapshot(self.root, '0.8.0', self.root / 'outside')
        with self.assertRaisesRegex(ValueError, 'differs from VERSION'):
            snapshot_release.snapshot(self.root, '0.8.1', self.root / 'artifacts/release-0.8.1')

    def test_source_policy_excludes_local_evidence_and_dependencies(self):
        for path in ('build/sol080/red.dll', 'artifacts/profile/Cookies', 'third_party/cef/a',
                     'node_modules/a/index.js', 'apps/browser-ui/dist/a.js',
                     'docs/evidence/native.jsonl', '.codex/session.json'):
            with self.subTest(path=path):
                self.assertIsNotNone(snapshot_release.excluded_reason(path))
        self.assertIsNone(snapshot_release.excluded_reason('docs/specs/link-peek-container.md'))
        for path in ('../outside', '/absolute', 'C:/drive', 'a\\b', 'a/../b'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                snapshot_release.safe_relative(path)


if __name__ == '__main__':
    unittest.main()
