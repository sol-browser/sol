"""Acquire the checksum-locked Velopack C/C++ library (velopack_libc).

Extracts only the C++ headers and the Windows x64 DLL with its import library.
The DLL links the CRT statically and imports nothing beyond Windows itself.
(The static library crashed when linked into sol.dll, 2026-11-04.)
"""
import hashlib
import json
import os
from pathlib import Path
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MEMBERS = {"include/Velopack.h": "Velopack.h", "include/Velopack.hpp": "Velopack.hpp",
           "lib/velopack_libc_win_x64_msvc.dll": "velopack_libc_win_x64_msvc.dll",
           "lib/velopack_libc_win_x64_msvc.dll.lib": "velopack_libc_win_x64_msvc.dll.lib"}


def fetch():
    lock = json.loads((ROOT / "cmake/velopack.lock.json").read_text())
    cache = ROOT / ".cache"
    cache.mkdir(exist_ok=True)
    archive = cache / lock["archive"]
    if not archive.exists():
        partial = archive.with_suffix(".partial")
        with urllib.request.urlopen(lock["url"], timeout=60) as response, partial.open("wb") as out:
            while data := response.read(1024 * 1024):
                out.write(data)
        os.replace(partial, archive)
    if archive.stat().st_size != lock["size"]:
        raise RuntimeError("Velopack archive size mismatch")
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != lock["sha256"]:
        raise RuntimeError("Velopack archive SHA-256 mismatch")
    destination = ROOT / "third_party/velopack"
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as bundle:
        for member, name in MEMBERS.items():
            target = destination / name
            data = bundle.read(member)
            if not target.exists() or target.read_bytes() != data:
                target.write_bytes(data)
    (destination / ".archive-sha256").write_text(digest + "\n")
    print(destination)


if __name__ == "__main__":
    fetch()
