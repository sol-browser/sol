"""Acquire the checksum-locked Inno Setup compiler into .cache (portable, no registry)."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def fetch():
    lock = json.loads((ROOT / "cmake/innosetup.lock.json").read_text())
    cache = ROOT / ".cache"
    cache.mkdir(exist_ok=True)
    destination = cache / f"innosetup-{lock['version']}"
    compiler = destination / "ISCC.exe"
    stamp = destination / ".archive-sha256"
    if compiler.is_file() and stamp.is_file() and stamp.read_text().strip() == lock["sha256"]:
        print(compiler)
        return
    archive = cache / lock["archive"]
    if not archive.exists():
        partial = archive.with_suffix(".partial")
        with urllib.request.urlopen(lock["url"], timeout=60) as response, partial.open("wb") as out:
            while data := response.read(1024 * 1024):
                out.write(data)
        os.replace(partial, archive)
    if archive.stat().st_size != lock["size"]:
        raise RuntimeError("Inno Setup installer size mismatch")
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != lock["sha256"]:
        raise RuntimeError("Inno Setup installer SHA-256 mismatch")
    # Portable mode writes no uninstaller, registry keys or shortcuts; the
    # compiler lives only under .cache and the per-user install needs no admin.
    result = subprocess.run([str(archive), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-",
                             "/CURRENTUSER", "/PORTABLE=1", "/NOICONS", f"/DIR={destination}"])
    if result.returncode or not compiler.is_file():
        raise RuntimeError(f"Inno Setup extraction failed ({result.returncode})")
    stamp.write_text(digest + "\n")
    print(compiler)


if __name__ == "__main__":
    fetch()
