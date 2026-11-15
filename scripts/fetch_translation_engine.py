"""Acquire the checksum-locked page translation engine (docs/page-translation.md).

Bergamot (Firefox's translation engine as WASM, MPL-2.0) and ELD (language detection, Apache-2.0)
come from npm tarballs pinned by size and SHA-256 in cmake/translation-engine.lock.json. Only an
explicit allowlist of files is extracted into third_party/translation-engine; the build copies that
folder next to sol.exe as translator/. Models are not here: Sol downloads them on demand.
"""
import hashlib
import json
import os
from pathlib import Path
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def fetch():
    lock = json.loads((ROOT / "cmake/translation-engine.lock.json").read_text())
    cache = ROOT / ".cache"
    cache.mkdir(exist_ok=True)
    destination = ROOT / "third_party/translation-engine"
    for package in lock["packages"]:
        archive = cache / package["archive"]
        if not archive.exists():
            partial = archive.with_suffix(".partial")
            with urllib.request.urlopen(package["url"], timeout=60) as response, partial.open("wb") as out:
                while data := response.read(1024 * 1024):
                    out.write(data)
            os.replace(partial, archive)
        if archive.stat().st_size != package["size"]:
            raise RuntimeError(f"{package['name']} archive size mismatch")
        with archive.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != package["sha256"]:
                raise RuntimeError(f"{package['name']} archive SHA-256 mismatch")
        with tarfile.open(archive, "r:gz") as bundle:
            for source, target_name in package["files"].items():
                member = bundle.getmember(source)
                if not member.isfile():
                    raise RuntimeError(f"{source} is not a regular file")
                target = destination / target_name
                if destination.resolve() not in target.resolve().parents:
                    raise RuntimeError(f"Unexpected target {target_name}")
                target.parent.mkdir(parents=True, exist_ok=True)
                data = bundle.extractfile(member).read()
                if not target.exists() or target.read_bytes() != data:
                    target.write_bytes(data)
    print(destination)


if __name__ == "__main__":
    fetch()
