#!/usr/bin/env python3
"""Fast source-health preflight for common cross-platform/Windows integration regressions."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hlsl"}


def fail(message: str, failures: list[str]) -> None:
    failures.append(message)


def main() -> int:
    failures: list[str] = []

    # Catch accidental Markdown/code-fence paste corruption in source/build files.
    for path in [ROOT / "CMakeLists.txt", *ROOT.joinpath("src").rglob("*"), *ROOT.joinpath("shaders").rglob("*")]:
        if not path.is_file() or (path.name != "CMakeLists.txt" and path.suffix.lower() not in SOURCE_SUFFIXES):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if "```" in text:
            fail(f"{path.relative_to(ROOT)} contains a Markdown code fence", failures)

    # Any translation unit/header that directly includes Windows.h must defend itself against min/max macro leakage.
    for path in ROOT.joinpath("src").rglob("*"):
        if not path.is_file() or path.suffix.lower() not in {".cpp", ".h", ".hpp"}:
            continue
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for index, line in enumerate(lines):
            if re.match(r"\s*#\s*include\s*<Windows\.h>", line, flags=re.IGNORECASE):
                prefix = "\n".join(lines[:index])
                if "#define NOMINMAX" not in prefix:
                    fail(f"{path.relative_to(ROOT)} includes Windows.h without defining NOMINMAX first", failures)
                if "#define WIN32_LEAN_AND_MEAN" not in prefix:
                    fail(f"{path.relative_to(ROOT)} includes Windows.h without defining WIN32_LEAN_AND_MEAN first", failures)

    # Guard the CPU/HLSL constant-buffer ABI that previously relied on implicit MSVC tail padding.
    contract = (ROOT / "src/rendering/DiagnosticShaderContract.h").read_text(encoding="utf-8")
    shader = (ROOT / "shaders/Diagnostic.hlsl").read_text(encoding="utf-8")
    required_cpu = [
        "struct alignas(16) DiagnosticDrawConstants",
        "std::uint32_t padding2 = 0;",
        "sizeof(DiagnosticDrawConstants) == 240",
        "offsetof(DiagnosticDrawConstants, padding2) == 236",
    ]
    required_hlsl = ["uint padding2;"]
    for token in required_cpu:
        if token not in contract:
            fail(f"DiagnosticShaderContract.h missing ABI guard: {token}", failures)
    for token in required_hlsl:
        if token not in shader:
            fail(f"Diagnostic.hlsl missing ABI field: {token}", failures)

    # The PowerShell wrapper must expose every diagnostic/runtime acceptance feature used by the executable.
    run_script = (ROOT / "scripts/run.ps1").read_text(encoding="utf-8")
    for token in ["tangents", "StressReloads", "StressAlternateAsset", "StressResize", "ReportLiveObjects", "NoErrorDialog"]:
        if token not in run_script:
            fail(f"scripts/run.ps1 does not expose {token}", failures)

    # Catch orphaned implementation files: dead .cpp files silently rot because no compiler ever sees them.
    cmake_text = "\n".join(p.read_text(encoding="utf-8") for p in ROOT.rglob("CMakeLists.txt"))
    for path in ROOT.joinpath("src").rglob("*.cpp"):
        rel = path.relative_to(ROOT).as_posix()
        if rel not in cmake_text:
            fail(f"orphaned source file is not built by CMake: {rel}", failures)

    # High-value structural sentinels for the root CMake file.
    root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    expected_once = [
        "add_library(daedalus_assets STATIC",
        "add_library(daedalus_rendering STATIC",
        "if(DAEDALUS_BUILD_APP)",
    ]
    for token in expected_once:
        count = root_cmake.count(token)
        if count != 1:
            fail(f"CMake structural sentinel {token!r} occurs {count} times (expected exactly 1)", failures)

    if failures:
        print("Source-health preflight FAILED:", file=sys.stderr)
        for item in failures:
            print(f"  - {item}", file=sys.stderr)
        return 1

    print("Source-health preflight passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
