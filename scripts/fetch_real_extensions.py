"""Fetch the user's real third-party extensions for local compatibility tests.

Uses Chrome's public Web Store update endpoint, verifies every CRX3 RSA proof
and the extension ID, then unpacks into the ignored .cache directory. Third-party
code is never written into the repository. This does not exercise the Web Store
installation UI; that is a separate experiment.
"""
import base64
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import urllib.parse
import urllib.request
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CHROMIUM = '154.0.8037.98'
EXTENSIONS = {
    'ublock-origin-lite': 'ddkjiahejlhfcafbddmgiahcphecmpfh',
    'sponsorblock': 'mnjggcdmjocbbbhaepdhchncahnbgone',
    'dark-reader': 'eimadpbcbfnmbkopoojfekhnkhdbieeh',
    '7tv': 'ammjkodgmmoknidbanneddgankgfejfh',
}
MAX_CRX = 64 * 1024 * 1024
MAX_UNPACKED = 256 * 1024 * 1024
# DER DigestInfo prefix for SHA-256 (RFC 8017, section 9.2).
SHA256_INFO = bytes.fromhex('3031300d060960864801650304020105000420')


def varint(data: bytes, offset: int) -> tuple[int, int]:
    value = shift = 0
    while True:
        if offset >= len(data) or shift > 63:
            raise ValueError('Truncated protobuf varint')
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, offset
        shift += 7


def fields(data: bytes) -> list[tuple[int, bytes]]:
    """Length-delimited protobuf fields only; other wire types are rejected."""
    result, offset = [], 0
    while offset < len(data):
        key, offset = varint(data, offset)
        if key & 7 != 2:
            raise ValueError('Unexpected protobuf wire type')
        length, offset = varint(data, offset)
        if offset + length > len(data):
            raise ValueError('Truncated protobuf field')
        result.append((key >> 3, data[offset:offset + length]))
        offset += length
    return result


def der(data: bytes, offset: int) -> tuple[int, bytes, int]:
    if offset + 2 > len(data):
        raise ValueError('Truncated DER')
    tag, length = data[offset], data[offset + 1]
    offset += 2
    if length & 0x80:
        count = length & 0x7F
        if not 0 < count <= 4 or offset + count > len(data):
            raise ValueError('Invalid DER length')
        length = int.from_bytes(data[offset:offset + count], 'big')
        offset += count
    if offset + length > len(data):
        raise ValueError('Truncated DER value')
    return tag, data[offset:offset + length], offset + length


def rsa_key(spki: bytes) -> tuple[int, int]:
    tag, body, _ = der(spki, 0)
    _, algorithm, offset = der(body, 0)
    if tag != 0x30 or b'\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01' not in algorithm:
        raise ValueError('Not an RSA SubjectPublicKeyInfo')
    tag, bits, _ = der(body, offset)
    if tag != 0x03 or not bits or bits[0] != 0:
        raise ValueError('Invalid RSA key bit string')
    _, sequence, _ = der(bits[1:], 0)
    _, modulus, offset = der(sequence, 0)
    _, exponent, _ = der(sequence, offset)
    return int.from_bytes(modulus, 'big'), int.from_bytes(exponent, 'big')


def rsa_verify(spki: bytes, signature: bytes, message: bytes) -> bool:
    modulus, exponent = rsa_key(spki)
    size = (modulus.bit_length() + 7) // 8
    if modulus.bit_length() < 2048 or len(signature) != size:
        return False
    encoded = pow(int.from_bytes(signature, 'big'), exponent, modulus).to_bytes(size, 'big')
    digest = SHA256_INFO + hashlib.sha256(message).digest()
    return encoded == b'\x00\x01' + b'\xff' * (size - len(digest) - 3) + b'\x00' + digest


def extension_id(spki: bytes) -> str:
    return ''.join(chr(ord('a') + int(c, 16)) for c in hashlib.sha256(spki).hexdigest()[:32])


def verify_crx(data: bytes, expected_id: str) -> tuple[bytes, dict]:
    if len(data) < 12 or data[:4] != b'Cr24' or struct.unpack('<I', data[4:8])[0] != 3:
        raise ValueError('Not a CRX3 file')
    header_size = struct.unpack('<I', data[8:12])[0]
    if 12 + header_size > len(data):
        raise ValueError('Truncated CRX header')
    header, archive = data[12:12 + header_size], data[12 + header_size:]
    proofs, ecdsa, signed = [], 0, None
    for number, value in fields(header):
        if number == 2:
            proof = dict(fields(value))
            proofs.append((proof.get(1, b''), proof.get(2, b'')))
        elif number == 3:
            ecdsa += 1  # Store publisher proof; recorded, not verified here.
        elif number == 10000:
            signed = value
    if signed is None or not proofs:
        raise ValueError('Missing CRX signed data or RSA proofs')
    crx_id = dict(fields(signed)).get(1, b'')
    message = b'CRX3 SignedData\x00' + struct.pack('<I', len(signed)) + signed + archive
    if not all(rsa_verify(key, signature, message) for key, signature in proofs):
        raise ValueError('CRX RSA signature verification failed')
    developer = [key for key, _ in proofs if extension_id(key) == expected_id]
    if not developer or crx_id.hex() != hashlib.sha256(developer[0]).hexdigest()[:32]:
        raise ValueError('CRX ID does not match the expected extension')
    return archive, developer[0], {'rsa_proofs_verified': len(proofs), 'ecdsa_proofs_unverified': ecdsa}


def safe_extract(archive: Path, destination: Path) -> None:
    with zipfile.ZipFile(archive) as bundle:
        total = 0
        for member in bundle.infolist():
            name = PurePosixPath(member.filename)
            # Reject absolute/drive paths, traversal and Unix symlinks.
            if name.is_absolute() or '..' in name.parts or ':' in member.filename or '\\' in member.filename \
                    or (member.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError(f'Unsafe archive member: {member.filename}')
            total += member.file_size
            if total > MAX_UNPACKED:
                raise ValueError('Unpacked extension too large')
        bundle.extractall(destination)


def fetch(name: str, ext_id: str) -> dict:
    query = urllib.parse.quote(f'id={ext_id}&installsource=ondemand&uc')
    url = (f'https://clients2.google.com/service/update2/crx?response=redirect&prodversion={CHROMIUM}'
           f'&acceptformat=crx3&x={query}')
    # Each download lands in its own new, empty directory.
    directory = ROOT / '.cache' / 'real-extensions' / f'{name}-{uuid.uuid4().hex[:8]}'
    directory.mkdir(parents=True)
    with urllib.request.urlopen(url, timeout=60) as source:
        data = source.read(MAX_CRX + 1)
    if len(data) > MAX_CRX:
        raise ValueError(f'{name}: CRX too large')
    archive, developer_key, proofs = verify_crx(data, ext_id)
    (directory / 'extension.crx').write_bytes(data)
    (directory / 'archive.zip').write_bytes(archive)
    unpacked = directory / 'unpacked'
    safe_extract(directory / 'archive.zip', unpacked)
    # Unpacked loading derives the ID from the path unless the manifest has the
    # developer's public key; set it so the tested ID equals the Web Store ID.
    manifest_path = unpacked / 'manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    manifest['key'] = base64.b64encode(developer_key).decode()
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    return {'name': name, 'id': ext_id, 'manifest_name': manifest.get('name'),
            'version': manifest.get('version'), 'manifest_version': manifest.get('manifest_version'),
            'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
            **proofs, 'id_verified': True,
            'unpacked': str(unpacked), 'permissions': manifest.get('permissions', []),
            'host_permissions': manifest.get('host_permissions', []),
            'has_action_popup': bool(manifest.get('action', {}).get('default_popup')),
            'options_page': manifest.get('options_ui', {}).get('page') or manifest.get('options_page'),
            'commands': sorted(manifest.get('commands', {}))}


def main() -> None:
    results = [fetch(name, ext_id) for name, ext_id in EXTENSIONS.items()]
    output = ROOT / '.cache' / 'real-extensions' / 'fetched.json'
    output.write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
