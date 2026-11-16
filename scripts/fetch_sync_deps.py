"""Acquire the checksum-locked sync libraries (Monocypher, zstd); extract each lock's explicit file list."""
import hashlib
import json
import os
from pathlib import Path
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
LIBRARIES = ("monocypher", "zstd")


def fetch(name):
    lock = json.loads((ROOT / f"cmake/{name}.lock.json").read_text())
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
        raise RuntimeError(f"{name} archive size mismatch")
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != lock["sha256"]:
        raise RuntimeError(f"{name} archive SHA-256 mismatch")
    destination = ROOT / "third_party" / name
    with tarfile.open(archive, "r:gz") as bundle:
        for relative in lock["files"]:
            if relative.startswith(("/", "\\")) or ".." in Path(relative).parts:
                raise RuntimeError(f"{name}: unsafe path in lock: {relative}")
            member = bundle.getmember(f"{lock['prefix']}/{relative}")
            if not member.isfile():
                raise RuntimeError(f"{name} archive entry is not a file: {relative}")
            data = bundle.extractfile(member).read()
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.exists() or target.read_bytes() != data:
                target.write_bytes(data)
    (destination / ".archive-sha256").write_text(digest + "\n")
    print(destination)


if __name__ == "__main__":
    for library in LIBRARIES:
        fetch(library)
