"""Download the reviewed, checksum-locked CEF distribution (Python 3.12+).

A lock without "url" pins our own CEF build (scripts/build_cef.ps1 places the
archive in .cache); it is verified exactly like a downloaded one.
"""
import hashlib
import json
import os
from pathlib import Path
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def fetch() -> Path:
    lock = json.loads((ROOT / "cmake/cef.lock.json").read_text())
    cache = ROOT / ".cache"
    cache.mkdir(exist_ok=True)
    archive = cache / lock["archive"]
    if not archive.exists() and not lock.get("url"):
        raise RuntimeError(f"Missing locally built CEF archive: {archive}; run scripts/build_cef.ps1")
    if not archive.exists():
        partial = archive.with_suffix(archive.suffix + ".partial")
        print(f"Downloading {lock['cef_version']}", flush=True)
        with urllib.request.urlopen(lock["url"], timeout=60) as source, partial.open("wb") as out:
            while chunk := source.read(1024 * 1024):
                out.write(chunk)
        os.replace(partial, archive)
    if archive.stat().st_size != lock["size"]:
        raise RuntimeError(f"Wrong archive size: {archive}; remove and retry")
    with archive.open("rb") as stream:
        actual = hashlib.file_digest(stream, "sha256").hexdigest()
    if actual != lock["sha256"]:
        raise RuntimeError(f"SHA-256 mismatch: {archive}; refusing extraction")

    destination = ROOT / "third_party" / "cef"
    stamp = destination / ".archive-sha256"
    if stamp.exists() and stamp.read_text().strip() == actual:
        print(destination)
        return destination
    parked = ROOT / "third_party" / f"cef-{actual[:12]}"
    if destination.exists():
        previous = stamp.read_text().strip() if stamp.exists() else ""
        if len(previous) != 64:
            raise RuntimeError(f"Unverified extraction already exists: {destination}; move it aside")
        # Switching between locked distributions: keep the verified one parked.
        aside = ROOT / "third_party" / f"cef-{previous[:12]}"
        if aside.exists():
            raise RuntimeError(f"Cannot park {destination}: {aside} exists")
        os.replace(destination, aside)
    if (parked / ".archive-sha256").exists() and (parked / ".archive-sha256").read_text().strip() == actual:
        os.replace(parked, destination)
        print(destination)
        return destination
    staging = ROOT / "third_party" / "cef-unpack"
    staging.mkdir(parents=True, exist_ok=True)
    prefix = lock.get("root", lock["archive"].removesuffix(".tar.bz2"))
    with tarfile.open(archive, "r:bz2") as bundle:
        # Require the expected root and reject links/devices before extraction.
        for member in bundle:
            parts = Path(member.name).parts
            if not parts or parts[0] != prefix or ".." in parts or not (member.isdir() or member.isfile()):
                raise RuntimeError(f"Unsafe archive member: {member.name}")
        bundle.extractall(staging, filter="data")
    os.replace(staging / prefix, destination)
    staging.rmdir()
    stamp.write_text(actual + "\n")
    print(destination)
    return destination


if __name__ == "__main__":
    fetch()
