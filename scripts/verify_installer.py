"""Verify an already-built installer and its stage without executing setup.

Usage: python scripts/verify_installer.py --build-directory sol080 [--test-variant]
The receipt checks source provenance and staged bytes; it does not claim that
installation/upgrade or the packaged browser has been exercised.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import tarfile


ROOT = Path(__file__).resolve().parents[1]
HEX = re.compile(r"[0-9a-fA-F]{64}\Z")
VERSION = re.compile(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)\Z")


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def relative_name(value):
    """Canonical, case-preserving Windows relative name, with aliases rejected."""
    if not isinstance(value, str) or not value or '\x00' in value:
        raise ValueError('Invalid manifest path')
    value = value.replace('\\', '/')
    parts = value.split('/')
    reserved = {'con', 'prn', 'aux', 'nul', *(f'com{i}' for i in range(1, 10)),
                *(f'lpt{i}' for i in range(1, 10))}
    if any(not part or part in ('.', '..') or part.endswith((' ', '.'))
           or re.search(r'[\x00-\x1f:<>"|?*]', part)
           or part.split('.')[0].lower() in reserved for part in parts):
        raise ValueError('Unsafe manifest path: ' + value)
    return '/'.join(parts)


def require_plain(path):
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
        raise ValueError('Reparse point is not allowed: ' + str(path))


def inventory(directory):
    directory = Path(directory)
    require_plain(directory)
    if not directory.is_dir():
        raise ValueError('Missing directory: ' + str(directory))
    found = {}
    for current, directories, files in os.walk(directory, followlinks=False):
        for name in directories + files:
            require_plain(Path(current) / name)
        for name in files:
            path = Path(current) / name
            relative = relative_name(path.relative_to(directory).as_posix())
            key = relative.casefold()
            if key in found:
                raise ValueError('Case-colliding files: ' + relative)
            found[key] = (relative, path)
    return found


def verify_manifest(manifest, stage, setup, version):
    if manifest.get('version') != version or manifest.get('installer') != setup.name:
        raise ValueError('Manifest version/installer mismatch')
    expected_hash = manifest.get('sha256', '')
    if not isinstance(expected_hash, str) or not HEX.fullmatch(expected_hash):
        raise ValueError('Invalid installer SHA-256')
    actual_hash = sha256(setup)
    if actual_hash != expected_hash.lower():
        raise ValueError('Installer SHA-256 mismatch')
    if 'size' in manifest and manifest['size'] != setup.stat().st_size:
        raise ValueError('Installer size mismatch')
    actual = inventory(stage)
    entries = manifest.get('files')
    if not isinstance(entries, list) or not entries:
        raise ValueError('Empty manifest')
    expected = {}
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError('Invalid manifest file entry')
        name = relative_name(entry.get('path'))
        key = name.casefold()
        if key in expected:
            raise ValueError('Duplicate manifest path: ' + name)
        digest, size = entry.get('sha256'), entry.get('size')
        if not isinstance(digest, str) or not HEX.fullmatch(digest):
            raise ValueError('Invalid file SHA-256: ' + name)
        if not isinstance(size, int) or isinstance(size, bool) or size < 0:
            raise ValueError('Invalid file size: ' + name)
        expected[key] = entry
    if actual.keys() != expected.keys():
        raise ValueError('Stage/manifest file set mismatch: missing=' +
                         str(sorted(expected.keys() - actual.keys())) + ' extra=' +
                         str(sorted(actual.keys() - expected.keys())))
    for key, (_, path) in actual.items():
        entry = expected[key]
        if path.stat().st_size != entry['size'] or sha256(path) != entry['sha256'].lower():
            raise ValueError('Staged file hash/size mismatch: ' + str(path))
    return actual_hash, actual


def pe_details(path):
    """Read x64 PE metadata and non-resource sections without loading the image."""
    data = Path(path).read_bytes()
    try:
        if data[:2] != b'MZ':
            raise ValueError('Missing MZ')
        offset = struct.unpack_from('<I', data, 0x3c)[0]
        if data[offset:offset + 4] != b'PE\0\0':
            raise ValueError('Missing PE')
        machine, count = struct.unpack_from('<HH', data, offset + 4)
        optional_size = struct.unpack_from('<H', data, offset + 20)[0]
        optional = offset + 24
        if machine != 0x8664 or struct.unpack_from('<H', data, optional)[0] != 0x20b:
            raise ValueError('Expected PE32+ AMD64')
        if optional_size < 112 or count == 0 or count > 96:
            raise ValueError('Invalid PE header')
        entry = struct.unpack_from('<I', data, optional + 16)[0]
        subsystem = struct.unpack_from('<H', data, optional + 68)[0]
        stack = struct.unpack_from('<Q', data, optional + 72)[0]
        sections = {}
        for index in range(count):
            section = optional + optional_size + 40 * index
            name = data[section:section + 8].rstrip(b'\0').decode('ascii')
            virtual_size, address, size, pointer = struct.unpack_from('<IIII', data, section + 8)
            flags = struct.unpack_from('<I', data, section + 36)[0]
            if not name or name in sections or pointer + size > len(data):
                raise ValueError('Invalid PE section')
            if name != '.rsrc':
                sections[name] = {'sha256': hashlib.sha256(data[pointer:pointer + size]).hexdigest(),
                                  'virtual_size': virtual_size, 'address': address,
                                  'size': size, 'flags': flags}
        return {'machine': machine, 'entry_point': entry, 'subsystem': subsystem,
                'stack_reserve': stack, 'sections': sections}
    except (struct.error, UnicodeError) as error:
        raise ValueError('Malformed PE: ' + str(path)) from error


def compare_bootstrap(original, branded, require_same_stack=True):
    left, right = pe_details(original), pe_details(branded)
    # UpdateResource grows .rsrc and can move the following .reloc section's
    # RVA. Its complete payload, size and flags must still be unchanged.
    if require_same_stack:
        for details in (left, right):
            if '.reloc' in details['sections']:
                details['sections']['.reloc'].pop('address')
    fields = ['machine', 'entry_point', 'subsystem', 'sections']
    if require_same_stack:
        fields.append('stack_reserve')
    if any(left[field] != right[field] for field in fields):
        raise ValueError('Bootstrap changed outside branding/resources: ' + str(branded))
    return right


def windows_versions(paths):
    """Read VERSIONINFO as data. Paths travel in JSON on stdin, never shell code."""
    script = ("$ErrorActionPreference='Stop'; $paths=([Console]::In.ReadToEnd() | ConvertFrom-Json); "
              "$items=@(foreach($p in $paths){$v=[Diagnostics.FileVersionInfo]::GetVersionInfo($p); "
              "@{path=$p;product_version=$v.ProductVersion;file_version=$v.FileVersion;"
              "product_name=$v.ProductName;description=$v.FileDescription}}); "
              "ConvertTo-Json -InputObject $items -Depth 3 -Compress")
    result = subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-Command', script],
                            input=json.dumps([str(path) for path in paths]), capture_output=True,
                            text=True, encoding='utf-8', errors='replace', check=True)
    values = json.loads(result.stdout.lstrip('\ufeff'))
    # Inno pads VERSIONINFO string slots with spaces; app resources do not.
    for item in values:
        for field in ('product_version', 'file_version', 'product_name', 'description'):
            item[field] = (item.get(field) or '').strip()
    return values


def version_matches(actual, expected):
    return actual in (expected, expected + '.0')


def cef_lists(sdk):
    text = (sdk / 'cmake/cef_variables.cmake').read_text(encoding='utf-8')
    text = text.split('if(OS_WINDOWS)', 1)[1]
    text = re.sub(r'#[^\n]*', '', text)
    result = {}
    for name in ('CEF_BINARY_FILES', 'CEF_RESOURCE_FILES'):
        chunks = re.findall(r'\bset\(\s*' + name + r'\s+([^)]*)\)', text)
        if len(chunks) != 1:
            raise ValueError('Cannot resolve Windows CEF file list: ' + name)
        chunks += re.findall(r'\blist\(\s*APPEND\s+' + name + r'\s+([^)]*)\)', text)
        values = [relative_name(token) for chunk in chunks for token in chunk.split()]
        if len({value.casefold() for value in values}) != len(values):
            raise ValueError('Duplicate CEF runtime filename')
        result[name] = values
    return result


def source_map(root, build, sdk):
    """Expected stage includes only the configured x64 CEF runtime and app assets."""
    sources = {'sol.dll': build / 'sol.dll',
               'licenses/cef-license.txt': sdk / 'LICENSE.txt',
               'licenses/chromium-credits.html': sdk / 'CREDITS.html'}
    lists = cef_lists(sdk)
    for group, directory in (('CEF_BINARY_FILES', sdk / 'Release'),
                             ('CEF_RESOURCE_FILES', sdk / 'Resources')):
        for name in lists[group]:
            path = directory / name
            if path.is_dir():
                for relative, file in inventory(path).values():
                    sources[(name + '/' + relative).casefold()] = file
            else:
                if not path.is_file():
                    raise ValueError('Missing CEF runtime source: ' + str(path))
                sources[name.casefold()] = path
    for relative, path in inventory(root / 'apps/browser-ui/dist').values():
        sources[('ui/' + relative).casefold()] = path
    return sources


def verify_sdk_archive(archive, archive_root, sdk, sources):
    """Prove used SDK bytes came from the checksum-pinned archive, without extracting."""
    wanted = {path.relative_to(sdk).as_posix(): path for path in sources
              if path.is_relative_to(sdk)}
    wanted.update({name: sdk / name for name in (
        'Release/bootstrap.exe', 'include/cef_version.h', 'cmake/cef_variables.cmake')})
    remaining = dict(wanted)
    with tarfile.open(archive, mode='r|bz2') as stream:
        for member in stream:
            name = member.name.removeprefix('./')
            prefix = archive_root + '/'
            if not name.startswith(prefix) or name[len(prefix):] not in wanted:
                continue
            relative = name[len(prefix):]
            if relative not in remaining or not member.isfile():
                raise ValueError('Duplicate/non-file SDK archive member: ' + member.name)
            path = remaining.pop(relative)
            digest = hashlib.sha256()
            source = stream.extractfile(member)
            if source is None:
                raise ValueError('Unreadable SDK archive member: ' + member.name)
            with source:
                for block in iter(lambda: source.read(1024 * 1024), b''):
                    digest.update(block)
            if member.size != path.stat().st_size or digest.hexdigest() != sha256(path):
                raise ValueError('SDK differs from pinned archive: ' + relative)
    if remaining:
        raise ValueError('SDK archive missing required files: ' + str(sorted(remaining)))
    return len(wanted)


def verify(root, build_directory, version, test_variant=False):
    root = Path(root).resolve()
    if not re.fullmatch(r'[A-Za-z0-9_-]+', build_directory):
        raise ValueError('Invalid build directory')
    if not VERSION.fullmatch(version) or (root / 'VERSION').read_text().strip() != version:
        raise ValueError('Version must match VERSION')
    name = 'SolInstallerTest' if test_variant else 'Sol'
    work = root / 'artifacts/installer'
    setup = work / f'{name}-Setup-{version}.exe'
    stage = work / ('stage-test' if test_variant else 'stage-release')
    build = root / 'build' / build_directory / 'apps/desktop/Release'
    sdk = root / 'third_party/cef'
    for path in (work, setup, build, sdk):
        require_plain(path)
    manifest_path = setup.with_suffix('.manifest.json')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    digest, actual = verify_manifest(manifest, stage, setup, version)
    binary_pin = json.loads((root / 'cmake/cef.lock.json').read_text())
    source_pin = json.loads((root / 'cmake/cef-source.lock.json').read_text())
    header = (sdk / 'include/cef_version.h').read_text()
    revisions = re.findall(r'^#define CEF_COMMIT_HASH "([0-9a-f]{40})"', header, re.M)
    cef_versions = re.findall(r'^#define CEF_VERSION "([^"]+)"', header, re.M)
    if revisions != [source_pin['revision']] or binary_pin['cef_revision'] != source_pin['revision']:
        raise ValueError('CEF SDK/binary/source revisions disagree')
    if cef_versions != [binary_pin['cef_version']]:
        raise ValueError('CEF SDK version disagrees with binary lock')
    if binary_pin['chromium_revision'] != source_pin['chromium_revision']:
        raise ValueError('Chromium binary/source revisions disagree')
    archive = root / '.cache' / relative_name(binary_pin['archive'])
    if archive.stat().st_size != binary_pin['size'] or sha256(archive) != binary_pin['sha256'].lower():
        raise ValueError('Pinned CEF archive hash/size mismatch')
    expected = source_map(root, build, sdk)
    archive_files = verify_sdk_archive(archive, binary_pin['root'], sdk, expected.values())
    if actual.keys() != expected.keys() | {'sol.exe'}:
        raise ValueError('Stage/source file set mismatch: missing=' +
                         str(sorted((expected.keys() | {'sol.exe'}) - actual.keys())) +
                         ' extra=' + str(sorted(actual.keys() - (expected.keys() | {'sol.exe'}))))
    for key, source in expected.items():
        path = actual[key][1]
        require_plain(source)
        if path.stat().st_size != source.stat().st_size or sha256(path) != sha256(source):
            raise ValueError('Stage differs from source: ' + str(path) + ' / ' + str(source))
        # Independently require every CEF runtime file in the build to match SDK.
        if not key.startswith(('ui/', 'licenses/')) and key != 'sol.dll':
            built = build / actual[key][0]
            if not built.is_file() or sha256(built) != sha256(source):
                raise ValueError('Build contains stale/missing CEF runtime: ' + str(built))
    if sha256(build / 'ui/index.html') != sha256(root / 'apps/browser-ui/dist/index.html'):
        raise ValueError('Build UI entry disagrees with current dist')
    bootstrap = compare_bootstrap(sdk / 'Release/bootstrap.exe', build / 'sol.exe', False)
    staged = compare_bootstrap(build / 'sol.exe', stage / 'sol.exe')
    if bootstrap['stack_reserve'] != 0x800000 or staged['stack_reserve'] != 0x800000:
        raise ValueError('Sol bootstrap requires an 8 MiB stack')
    versions = windows_versions([setup, stage / 'sol.exe', stage / 'sol.dll'])
    if len(versions) != 3 or any(not version_matches(item[field], version)
                                 for item in versions for field in ('product_version', 'file_version')):
        raise ValueError('Setup/EXE/DLL versions disagree with VERSION: ' + repr(versions))
    if versions[0]['product_name'] != ('Sol Installer Test' if test_variant else 'Sol'):
        raise ValueError('Incorrect installer product identity')
    if any(item['product_name'] != 'Sol' for item in versions[1:]):
        raise ValueError('Incorrect app product identity')
    return {'passed': True, 'checked_at_utc': datetime.now(timezone.utc).isoformat(),
            'version': version, 'build_directory': build_directory, 'test_variant': test_variant,
            'installer': str(setup), 'size': setup.stat().st_size, 'sha256': digest,
            'manifest_sha256': sha256(manifest_path), 'files': len(actual),
            'source_matched_files': len(expected), 'ui_files': sum(k.startswith('ui/') for k in actual),
            'cef_revision': revisions[0], 'cef_version': cef_versions[0],
            'cef_archive_sha256': binary_pin['sha256'], 'versions': versions,
            'sdk_files_matched_to_pinned_archive': archive_files,
            'bootstrap_stack_reserve': staged['stack_reserve'],
            'bootstrap_non_resource_sections_match': True,
            'limitations': ['Setup not executed; installation/upgrade not verified here.',
                            'Browser runtime acceptance is recorded separately.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory', required=True)
    parser.add_argument('--version', default=(ROOT / 'VERSION').read_text().strip())
    parser.add_argument('--test-variant', action='store_true')
    args = parser.parse_args()
    try:
        receipt = verify(ROOT, args.build_directory, args.version, args.test_variant)
    except (ValueError, OSError, KeyError, IndexError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Installer verification failed: {error}\n')
    setup = Path(receipt['installer'])
    setup.with_suffix('.verification.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    setup.with_suffix('.sha256').write_text(receipt['sha256'] + '  ' + setup.name + '\n', encoding='ascii')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
