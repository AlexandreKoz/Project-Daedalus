#!/usr/bin/env python3
"""Fast source-health preflight for common cross-platform/Windows integration regressions."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hlsl", ".hlsli"}


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

    # Guard the Campaign C1 CPU/HLSL constant-buffer ABI and stable material-ID contract.
    contract = (ROOT / "src/rendering/RasterShaderContract.h").read_text(encoding="utf-8")
    shader = (ROOT / "shaders/RasterPbr.hlsl").read_text(encoding="utf-8")
    required_cpu = [
        "struct alignas(16) RasterFrameConstants",
        "struct alignas(16) RasterDrawConstants",
        "struct alignas(16) RasterLightGpu",
        "sizeof(RasterFrameConstants) == 112",
        "sizeof(RasterDrawConstants) == 224",
        "offsetof(RasterDrawConstants, world_handedness) == 208",
        "sizeof(RasterLightGpu) == 64",
    ]
    required_hlsl = [
        "cbuffer FrameConstants : register(b0)",
        "cbuffer DrawConstants : register(b1)",
        "cbuffer LightConstants : register(b2)",
        "float world_handedness;",
        "uint material_diagnostic_id;",
    ]
    for token in required_cpu:
        if token not in contract:
            fail(f"RasterShaderContract.h missing ABI guard: {token}", failures)
    for token in required_hlsl:
        if token not in shader:
            fail(f"RasterPbr.hlsl missing ABI field/contract: {token}", failures)

    preparation = (ROOT / "src/rendering/RasterPreparation.cpp").read_text(encoding="utf-8")
    for token in ["return kDefaultMaterialDiagnosticId;", "return material.value() + 1U;"]:
        if token not in preparation:
            fail(f"RasterPreparation.cpp missing stable material-ID sentinel rule: {token}", failures)

    # The PowerShell wrapper must expose every diagnostic/runtime acceptance feature used by the executable.
    run_script = (ROOT / "scripts/run.ps1").read_text(encoding="utf-8")
    for token in ["tangents", "base-color", "metallic", "roughness", "emissive", "material-id", "Exposure", "StressReloads", "StressAlternateAsset", "StressResize", "ReportLiveObjects", "NoErrorDialog"]:
        if token not in run_script:
            fail(f"scripts/run.ps1 does not expose {token}", failures)
    for token in ["$InvocationDirectory = (Get-Location).Path", "Resolve-OutputPath $ImportReport $InvocationDirectory"]:
        if token not in run_script:
            fail(f"scripts/run.ps1 missing caller-relative output-path guard: {token}", failures)
    if "GetFullPath($ImportReport)" in run_script:
        fail("scripts/run.ps1 resolves ImportReport through process CWD instead of the PowerShell caller directory", failures)

    package_script = (ROOT / "scripts/package-source.ps1").read_text(encoding="utf-8")
    for token in ["$InvocationDirectory = (Get-Location).Path", "IsPathFullyQualified($OutputPath)", "Join-Path $InvocationDirectory $OutputPath"]:
        if token not in package_script:
            fail(f"scripts/package-source.ps1 missing caller-relative output-path guard: {token}", failures)

    # Guard the post-teardown DXGI path: DXGIGetDebugInterface1 is exported by dxgi.dll, not dxgidebug.dll.
    d3d12_context = (ROOT / "src/graphics/D3D12Context.cpp").read_text(encoding="utf-8")
    if "DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))" not in d3d12_context:
        fail("D3D12Context.cpp is missing the direct DXGIGetDebugInterface1 live-object path", failures)
    if 'GetProcAddress(module, "DXGIGetDebugInterface1")' in d3d12_context or 'LoadLibraryW(L"dxgidebug.dll")' in d3d12_context:
        fail("D3D12Context.cpp contains the obsolete/wrong dynamic DXGIGetDebugInterface1 lookup", failures)

    application = (ROOT / "src/Application.cpp").read_text(encoding="utf-8")
    if "if (shutdown_complete_)" not in application:
        fail("Application.cpp is missing idempotent shutdown protection", failures)

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
