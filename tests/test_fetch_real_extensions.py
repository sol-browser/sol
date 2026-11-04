import hashlib
import importlib.util
from pathlib import Path
import random
import struct
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("fetch_real_extensions", Path(__file__).resolve().parents[1] / "scripts/fetch_real_extensions.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def prime(bits: int, rng: random.Random) -> int:
    while True:
        candidate = rng.getrandbits(bits) | (1 << (bits - 1)) | 1
        if all(candidate % p for p in (3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37)) and all(
                pow(rng.randrange(2, candidate - 1), candidate - 1, candidate) == 1 for _ in range(8)):
            return candidate


def der(tag: int, body: bytes) -> bytes:
    size = len(body)
    length = bytes([size]) if size < 128 else bytes([0x80 | ((size.bit_length() + 7) // 8)]) + size.to_bytes((size.bit_length() + 7) // 8, 'big')
    return bytes([tag]) + length + body


def integer(value: int) -> bytes:
    return der(0x02, value.to_bytes(value.bit_length() // 8 + 1, 'big'))


def field(number: int, value: bytes) -> bytes:
    def varint(n):
        out = b''
        while True:
            out += bytes([(n & 0x7F) | (0x80 if n > 0x7F else 0)])
            n >>= 7
            if not n:
                return out
    return varint(number << 3 | 2) + varint(len(value)) + value


class RealExtensionFetchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Deterministic 2048-bit test key; never a real signing key.
        rng = random.Random(1380271170)
        e = 65537
        while True:
            p, q = prime(1024, rng), prime(1024, rng)
            n, phi = p * q, (p - 1) * (q - 1)
            if n.bit_length() == 2048 and phi % e:
                break
        cls.n, cls.d = n, pow(e, -1, phi)
        algorithm = der(0x30, der(0x06, bytes.fromhex('2a864886f70d010101')) + b'\x05\x00')
        cls.spki = der(0x30, algorithm + der(0x03, b'\x00' + der(0x30, integer(n) + integer(e))))
        cls.ext_id = module.extension_id(cls.spki)

    def sign(self, message: bytes) -> bytes:
        digest = module.SHA256_INFO + hashlib.sha256(message).digest()
        encoded = b'\x00\x01' + b'\xff' * (256 - len(digest) - 3) + b'\x00' + digest
        return pow(int.from_bytes(encoded, 'big'), self.d, self.n).to_bytes(256, 'big')

    def crx(self, archive: bytes, crx_id: bytes | None = None) -> bytes:
        signed = field(1, crx_id or hashlib.sha256(self.spki).digest()[:16])
        message = b'CRX3 SignedData\x00' + struct.pack('<I', len(signed)) + signed + archive
        header = field(2, field(1, self.spki) + field(2, self.sign(message))) + field(10000, signed)
        return b'Cr24' + struct.pack('<II', 3, len(header)) + header + archive

    def test_valid_crx_is_accepted(self):
        archive, key, proofs = module.verify_crx(self.crx(b'zip-bytes'), self.ext_id)
        self.assertEqual((archive, key, proofs['rsa_proofs_verified']), (b'zip-bytes', self.spki, 1))

    def test_tampered_archive_is_rejected(self):
        data = bytearray(self.crx(b'zip-bytes'))
        data[-1] ^= 1
        with self.assertRaisesRegex(ValueError, 'signature'):
            module.verify_crx(bytes(data), self.ext_id)

    def test_other_extension_id_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'CRX ID'):
            module.verify_crx(self.crx(b'zip-bytes'), 'a' * 32)
        with self.assertRaisesRegex(ValueError, 'CRX ID'):
            module.verify_crx(self.crx(b'zip-bytes', b'\x00' * 16), self.ext_id)

    def test_non_crx3_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'CRX3'):
            module.verify_crx(b'PK\x03\x04' + b'\x00' * 20, self.ext_id)

    def test_traversal_member_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / 'a.zip'
            with zipfile.ZipFile(archive, 'w') as bundle:
                bundle.writestr('manifest.json', '{}')
                bundle.writestr('../escape', 'no')
            with self.assertRaisesRegex(ValueError, 'Unsafe'):
                module.safe_extract(archive, Path(temp) / 'out')
            self.assertFalse((Path(temp) / 'escape').exists())


if __name__ == '__main__':
    unittest.main()
