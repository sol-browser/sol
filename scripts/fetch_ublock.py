"""Acquire the checksum-locked uBlock Origin package built into Sol (docs/ublock-origin.md).

Extracts the official Chromium release archive into third_party/ublock. The
engine registers it as a component extension from <exe dir>/extensions/ublock;
its ID comes from the manifest's official key and is checked by the engine.
"""
import base64
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import zipfile

import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def extension_id(key):
    """Chromium's extension ID: the first 128 bits of SHA-256(public key), as a-p."""
    digest = hashlib.sha256(base64.b64decode(key)).hexdigest()[:32]
    return "".join(chr(ord("a") + int(c, 16)) for c in digest)


def main():
    lock = json.loads((ROOT / "cmake/ublock.lock.json").read_text(encoding="utf-8"))
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
        raise RuntimeError("uBlock Origin archive size mismatch")
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != lock["sha256"]:
        raise RuntimeError("uBlock Origin archive SHA-256 mismatch")
    destination = ROOT / "third_party" / "ublock"
    marker = destination / ".archive-sha256"
    if marker.exists() and marker.read_text().strip() == digest:
        print(destination)
        return
    if destination.exists():
        shutil.rmtree(destination)
    prefix = lock["prefix"] + "/"
    with zipfile.ZipFile(archive) as bundle:
        for info in bundle.infolist():
            name = PurePosixPath(info.filename)
            if info.is_dir() or not info.filename.startswith(prefix):
                continue
            relative = PurePosixPath(info.filename[len(prefix):])
            if relative.is_absolute() or ".." in relative.parts or not relative.parts:
                raise RuntimeError(f"uBlock Origin: unsafe path in archive: {name}")
            target = destination.joinpath(*relative.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(bundle.read(info))
    manifest_path = destination / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("version") != lock["version"]:
        raise RuntimeError("uBlock Origin manifest does not match the lock")
    # The release archive has no key; the official one keeps the extension's ID.
    manifest["key"] = lock["manifest_key"]
    if extension_id(lock["manifest_key"]) != lock["extension_id"]:
        raise RuntimeError("uBlock Origin key does not give the locked extension ID")
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    marker.write_text(digest + "\n")
    print(destination)


if __name__ == "__main__":
    main()
