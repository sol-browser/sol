"""Acquire the checksum-locked SQLite amalgamation; extract an explicit allowlist."""
import hashlib
import json
import os
from pathlib import Path
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def fetch():
    lock = json.loads((ROOT / "cmake/sqlite.lock.json").read_text())
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
        raise RuntimeError("SQLite archive size mismatch")
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != lock["sha256"]:
        raise RuntimeError("SQLite archive SHA-256 mismatch")
    destination = ROOT / "third_party/sqlite"
    destination.mkdir(parents=True, exist_ok=True)
    prefix = lock["archive"].removesuffix(".zip")
    with zipfile.ZipFile(archive) as bundle:
        for name in ("sqlite3.c", "sqlite3.h"):
            target = destination / name
            data = bundle.read(f"{prefix}/{name}")
            if not target.exists() or target.read_bytes() != data:
                target.write_bytes(data)
    (destination / ".archive-sha256").write_text(digest + "\n")
    print(destination)


if __name__ == "__main__":
    fetch()
