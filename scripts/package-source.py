#!/usr/bin/env python3
"""Create and verify the deterministic Project Daedalus source ZIP."""
from __future__ import annotations

import argparse
import fnmatch
import hashlib
import os
from pathlib import Path, PurePosixPath
import sys
import zipfile

CANONICAL_ROOT = "Project-Daedalus"
FIXED_TIMESTAMP = (2000, 1, 1, 0, 0, 0)
FORBIDDEN_DIRECTORIES = {
    ".git", ".vs", ".vscode", ".idea", "build", "out", "bin", "obj", "Debug", "Release",
    "RelWithDebInfo", "MinSizeRel", "x64", "CMakeFiles", "Testing", "__pycache__", "runtime",
}
FORBIDDEN_FILES = (
    "CMakeCache.txt", "cmake_install.cmake", "CTestTestfile.cmake", "compile_commands.json",
    "*.sln", "*.vcxproj", "*.vcxproj.filters", "*.vcxproj.user", "*.pdb", "*.ilk", "*.obj",
    "*.lib", "*.exp", "*.exe", "*.dll", "*.cso", "*.dxil", "*.cache", "*.log", "*.zip",
    "*.tar", "*.tar.gz", "*.7z", "*.pyc", "*.pyo",
)


def forbidden(relative: PurePosixPath) -> bool:
    if any(part in FORBIDDEN_DIRECTORIES or part.startswith("build-") for part in relative.parts):
        return True
    return any(fnmatch.fnmatchcase(relative.name, pattern) for pattern in FORBIDDEN_FILES)


def collect(root: Path, output: Path) -> list[tuple[PurePosixPath, Path]]:
    root_resolved = root.resolve()
    output_resolved = output.resolve(strict=False)
    try:
        output_resolved.relative_to(root_resolved)
    except ValueError:
        pass
    else:
        raise RuntimeError("output archive must be outside the repository root")

    files: list[tuple[PurePosixPath, Path]] = []
    violations: list[str] = []
    for path in root.rglob("*"):
        relative = PurePosixPath(path.relative_to(root).as_posix())
        if path.is_symlink():
            violations.append(f"symlink:{relative}")
            continue
        if forbidden(relative):
            violations.append(str(relative))
            continue
        if path.is_file():
            if path.resolve() == output_resolved:
                violations.append(str(relative))
            else:
                files.append((relative, path))
    if violations:
        raise RuntimeError("source archive rejected because prohibited artefacts exist:\n" + "\n".join(sorted(violations)))
    files.sort(key=lambda item: str(item[0]).encode("utf-8"))
    if not files:
        raise RuntimeError("source archive rejected because no source files were found")
    return files


def write_archive(root: Path, output: Path) -> tuple[str, int]:
    files = collect(root, output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9, strict_timestamps=True) as archive:
        for relative, path in files:
            name = f"{CANONICAL_ROOT}/{relative}"
            info = zipfile.ZipInfo(name, date_time=FIXED_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (0o100644 & 0xFFFF) << 16
            info.flag_bits = 0x800
            info.extra = b""
            info.comment = b""
            archive.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    verify_archive(output)
    return hashlib.sha256(output.read_bytes()).hexdigest(), len(files)


def verify_archive(path: Path) -> None:
    with zipfile.ZipFile(path, "r") as archive:
        bad = archive.testzip()
        if bad is not None:
            raise RuntimeError(f"ZIP CRC validation failed for {bad}")
        infos = archive.infolist()
        names = [info.filename for info in infos]
        expected_names = sorted(names, key=lambda value: value.encode("utf-8"))
        if names != expected_names:
            raise RuntimeError("archive entries are not in ordinal UTF-8 lexical order")
        if len(names) != len(set(names)):
            raise RuntimeError("archive contains duplicate entry names")
        for info in infos:
            if not info.filename.startswith(CANONICAL_ROOT + "/"):
                raise RuntimeError(f"entry outside canonical root: {info.filename}")
            relative = PurePosixPath(info.filename[len(CANONICAL_ROOT) + 1 :])
            if forbidden(relative):
                raise RuntimeError(f"prohibited archive entry: {info.filename}")
            if info.date_time != FIXED_TIMESTAMP:
                raise RuntimeError(f"noncanonical timestamp: {info.filename}")
            if info.extra or info.comment:
                raise RuntimeError(f"nondeterministic extra/comment metadata: {info.filename}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = args.output or root.parent / "Project-Daedalus-Campaign-B-audit-closure-source.zip"
    try:
        digest, count = write_archive(root, output.resolve())
    except (OSError, RuntimeError, zipfile.BadZipFile) as error:
        print(f"package error: {error}", file=sys.stderr)
        return 1
    print(f"Archive: {output.resolve()}")
    print(f"Entries: {count}")
    print("Fixed timestamp: 2000-01-01T00:00:00Z")
    print(f"Archive SHA-256: {digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
