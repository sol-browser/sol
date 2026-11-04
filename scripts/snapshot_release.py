"""Save the working source bytes and Git state for a local release.

The checkpoint includes tracked and nonignored untracked source files, including
uncommitted changes. It deliberately excludes build products, dependencies,
profiles, local tooling state and runtime evidence. It never changes Git state.
Existing checkpoints are immutable: choose another output directory to retry.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import tempfile
import zipfile


VERSION_RE = re.compile(r"(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\Z")
ROOT_EXCLUDES = frozenset({
    ".git", ".cache", "build", "artifacts", "third_party", "profiles",
    ".vs", ".vscode", ".idea", ".claude", ".codex", ".agents", ".aws",
})
TREE_EXCLUDES = frozenset({"node_modules", "dist", "__pycache__"})


def safe_relative(name: str) -> PurePosixPath:
    """Accept a portable Git-relative filename, without Windows escape forms."""
    path = PurePosixPath(name)
    if (not name or path.is_absolute() or "\\" in name or "\0" in name
            or any(part in {"", ".", ".."} or ":" in part for part in name.split("/"))):
        raise ValueError(f"Unsafe source path: {name!r}")
    return path


def excluded_reason(relative: str | PurePosixPath) -> str | None:
    """Return the explicit exclusion policy that applies to a source path."""
    path = safe_relative(str(relative))
    parts = tuple(part.casefold() for part in path.parts)
    if parts[0] in ROOT_EXCLUDES:
        return "local build/dependency/profile/tooling directory"
    if any(part in TREE_EXCLUDES for part in parts):
        return "generated dependency/build directory"
    if parts[:2] == ("docs", "evidence"):
        return "local runtime evidence"
    if path.suffix.casefold() == ".pyc":
        return "Python bytecode"
    return None


def _git(root: Path, *arguments: str) -> str:
    return subprocess.run(
        ["git", "-c", "core.quotepath=false", *arguments], cwd=root,
        check=True, capture_output=True, encoding="utf-8", errors="strict",
    ).stdout


def _reject_link(path: Path) -> None:
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & getattr(
            stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400):
        raise ValueError(f"Source checkpoint cannot follow a symlink/reparse point: {path}")


def _checked_source(root: Path, relative: PurePosixPath) -> Path:
    current = root
    _reject_link(current)
    for part in relative.parts:
        current = current / part
        _reject_link(current)
    if not current.is_file():
        raise ValueError(f"Source is not a regular file: {current}")
    return current


def _fingerprint(path: Path) -> tuple[int, int, int]:
    info = path.stat()
    return info.st_size, info.st_mtime_ns, info.st_ino


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _temporary(directory: Path, suffix: str) -> Path:
    descriptor, name = tempfile.mkstemp(prefix=".source-snapshot-", suffix=suffix, dir=directory)
    os.close(descriptor)
    return Path(name)


def _publish_exclusive(temporary: Path, destination: Path) -> None:
    # Windows rename is atomic and refuses an existing destination. POSIX
    # rename can replace a file, so use an exclusive hard link there instead.
    if os.name == "nt":
        os.rename(temporary, destination)
    else:
        os.link(temporary, destination)
        temporary.unlink()


def snapshot(root: Path, version: str, output: Path) -> dict:
    root = Path(os.path.abspath(root))
    if not VERSION_RE.fullmatch(version):
        raise ValueError("Version must be a three-part release number")
    actual_version = (root / "VERSION").read_text(encoding="utf-8-sig").strip()
    if version != actual_version:
        raise ValueError(f"Requested version {version} differs from VERSION {actual_version}")
    output = Path(os.path.abspath(output))
    try:
        relative_output = output.relative_to(root / "artifacts")
    except ValueError as error:
        raise ValueError("Checkpoint output must remain inside this repository's artifacts directory") from error
    current = root
    _reject_link(current)
    for part in ("artifacts", *relative_output.parts):
        current = current / part
        if current.exists() or current.is_symlink():
            _reject_link(current)
    archive = output / "source-snapshot.zip"
    receipt_path = output / "snapshot.json"
    if any(path.exists() or path.is_symlink() for path in (archive, receipt_path)):
        raise FileExistsError("Release checkpoint already exists; it will not be overwritten")

    head = _git(root, "rev-parse", "HEAD").strip()
    status = _git(root, "status", "--porcelain=v1", "--untracked-files=all")
    # HEAD contributes deleted staged paths, so omissions remain auditable too.
    names = set(_git(root, "ls-tree", "-r", "--name-only", "-z", "HEAD").split("\0"))
    names.update(_git(root, "ls-files", "--cached", "--others", "--exclude-standard", "-z").split("\0"))
    names.discard("")
    included: list[tuple[PurePosixPath, Path]] = []
    omitted: list[dict] = []
    for name in sorted(names):
        relative = safe_relative(name)
        reason = excluded_reason(relative)
        if reason:
            omitted.append({"path": name, "reason": reason})
            continue
        try:
            source = _checked_source(root, relative)
        except FileNotFoundError:
            omitted.append({"path": name, "reason": "absent from working tree (deleted)"})
            continue
        included.append((relative, source))
    if not included:
        raise ValueError("No source files found for checkpoint")

    output.mkdir(parents=True, exist_ok=True)
    temporary_archive = _temporary(output, ".zip")
    temporary_receipt = _temporary(output, ".json")
    published_archive = False
    try:
        files = []
        fingerprints: list[tuple[Path, tuple[int, int, int]]] = []
        with zipfile.ZipFile(temporary_archive, "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=6, allowZip64=True) as bundle:
            for relative, source in included:
                before = _fingerprint(source)
                data = source.read_bytes()
                if _fingerprint(source) != before:
                    raise RuntimeError(f"Source changed while checkpointing: {relative}")
                bundle.writestr(str(relative), data)
                files.append({"path": str(relative), "size": len(data),
                              "sha256": hashlib.sha256(data).hexdigest()})
                fingerprints.append((source, before))
        for source, fingerprint in fingerprints:
            if _fingerprint(source) != fingerprint:
                raise RuntimeError(f"Source changed during checkpoint: {source}")
        if head != _git(root, "rev-parse", "HEAD").strip() or status != _git(
                root, "status", "--porcelain=v1", "--untracked-files=all"):
            raise RuntimeError("Git state changed during checkpoint; retry after edits finish")
        receipt = {
            "schema_version": 1, "version": version,
            "created_at": datetime.now(timezone.utc).isoformat(),
            "source": "Git tracked and nonignored untracked working-tree bytes",
            "git_head": head, "git_status_porcelain": status,
            "archive": archive.name, "size": temporary_archive.stat().st_size,
            "sha256": _sha256(temporary_archive), "file_count": len(files),
            "files": files, "omitted": omitted,
        }
        temporary_receipt.write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n",
                                     encoding="utf-8")
        _publish_exclusive(temporary_archive, archive)
        published_archive = True
        _publish_exclusive(temporary_receipt, receipt_path)
        return receipt
    except BaseException:
        if published_archive:
            archive.unlink(missing_ok=True)
        raise
    finally:
        temporary_archive.unlink(missing_ok=True)
        temporary_receipt.unlink(missing_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", help="Defaults to VERSION; must match it")
    parser.add_argument("--output", type=Path, help="Directory inside artifacts/ (default: artifacts/release-VERSION)")
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    version = arguments.version or (root / "VERSION").read_text(encoding="utf-8-sig").strip()
    output = arguments.output or root / "artifacts" / f"release-{version}"
    if not output.is_absolute():
        output = root / output
    try:
        receipt = snapshot(root, version, output)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Source checkpoint failed: {error}\n")
    print(json.dumps({"version": version, "archive": str(output / receipt["archive"]),
                      "receipt": str(output / "snapshot.json"), "sha256": receipt["sha256"],
                      "size": receipt["size"], "file_count": receipt["file_count"]}, indent=2))


if __name__ == "__main__":
    main()
