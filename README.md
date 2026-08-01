# Project Daedalus

Project Daedalus is an AI-assisted Windows rendering laboratory. Version 0.0.1 contains Campaign A: a deliberately small Win32 and Direct3D 12 foundation intended to be audited before later rendering systems are added.

## Campaign A status

The source implements:

- A native resizable Win32 window and message loop.
- Hardware adapter enumeration with an explicit `--warp` diagnostic mode.
- A Direct3D 12 device, direct command queue, two swap-chain buffers, one command allocator per buffer, a graphics command list, and fence synchronization.
- A flip-discard swap chain with vertical synchronization enabled.
- DXC build-time compilation of Shader Model 6.0 vertex and pixel shaders.
- A minimal root signature and graphics pipeline that render an interpolated-colour triangle.
- Explicit `PRESENT` to `RENDER_TARGET` and `RENDER_TARGET` to `PRESENT` transitions.
- Minimize, restore, resize, frame-limited execution, and clean shutdown paths.
- Session logging, HRESULT diagnostics, CPU tests, CTest registration, and Windows CI configuration.

A pre-closure Campaign A snapshot was compiled and run locally on Windows 11 on 2026-08-01 with Visual Studio 2026, MSVC 19.51/v145, Windows SDK 10.0.26100.0, DXC 1.8.2502.11, and an NVIDIA GeForce RTX 4060 Ti. That run passed Debug configuration and compilation, both shader builds, five CPU test cases, a 120-frame hardware smoke test, an interactive resize/minimize/restore run, and explicit WARP initialization.

The current closure patch repairs exceptional shutdown ordering, constructor-time Win32 resource ownership, source packaging from a normal Git checkout, and Visual Studio 2026 preset support. Because those source files changed after the recorded Windows run, the exact current snapshot still requires the documented Windows Debug and Release revalidation. The authoritative status is in `docs/campaigns/campaign-a-acceptance.md`.

## Prerequisites

Use a 64-bit Windows 11 or compatible Windows 10/11 development system with:

1. Visual Studio 2022/Build Tools with MSVC v143, or Visual Studio 2026 with MSVC v145.
2. The **Desktop development with C++** workload.
3. A recent Windows 10 or Windows 11 SDK.
4. CMake 3.25 or newer for the Visual Studio 2022 presets.
5. A CMake build that lists the `Visual Studio 18 2026` generator for the Visual Studio 2026 presets.
6. DirectX Shader Compiler (`dxc.exe`) from the Windows SDK or an official DXC installation.
7. A Direct3D 12-capable adapter for hardware execution. Microsoft WARP can be selected explicitly for diagnostics.

For Debug validation, install the Windows **Graphics Tools** optional feature. Without it, Daedalus logs a clear warning and continues without the D3D12 debug layer.

## DXC discovery

Configuration searches in this order:

1. The CMake cache variable `DXC_PATH`, which may name `dxc.exe` or its directory.
2. The `DXC_PATH` environment variable.
3. The active Windows SDK environment.
4. Installed Windows SDK `x64` binary directories.
5. `PATH`.

Configuration fails clearly when DXC cannot be found. There is no FXC fallback.

Example override:

```powershell
cmake --preset windows-vs2026-debug -DDXC_PATH="C:\path\to\dxc.exe"
```

## Configure, build, and test

### Visual Studio 2022

From a Visual Studio 2022 Developer PowerShell:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug

cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
```

Equivalent helpers:

```powershell
.\scripts\configure.ps1 -Configuration Debug -VisualStudioVersion 2022
.\scripts\build.ps1 -Configuration Debug -VisualStudioVersion 2022
.\scripts\test.ps1 -Configuration Debug -VisualStudioVersion 2022
```

### Visual Studio 2026

From a Visual Studio 2026 Developer PowerShell:

```powershell
cmake --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-debug
ctest --preset windows-vs2026-debug

cmake --preset windows-vs2026-release
cmake --build --preset windows-vs2026-release
ctest --preset windows-vs2026-release
```

Equivalent helpers:

```powershell
.\scripts\configure.ps1 -Configuration Debug -VisualStudioVersion 2026
.\scripts\build.ps1 -Configuration Debug -VisualStudioVersion 2026
.\scripts\test.ps1 -Configuration Debug -VisualStudioVersion 2026
```

All Windows presets target x64, use out-of-source build directories, enable C++20, apply `/W4`, `/permissive-`, `/Zc:__cplusplus`, and `/utf-8`, and treat project-owned warnings as errors. The Visual Studio 2026 presets explicitly select `v145,host=x64`.

## Run

Visual Studio 2022 Debug output:

```powershell
.\build\windows-msvc-debug\Debug\Daedalus.exe
```

Visual Studio 2026 Debug output:

```powershell
.\build\windows-vs2026-debug\Debug\Daedalus.exe
```

Helper examples:

```powershell
.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026
.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026 -Warp
.\scripts\run.ps1 -Configuration Debug -VisualStudioVersion 2026 -Frames 120
```

The application supports:

```text
Daedalus.exe
Daedalus.exe --warp
Daedalus.exe --frames 120
Daedalus.exe --help
```

`--help` exits without initializing Direct3D 12. Invalid arguments return a nonzero exit code and print usage information.

## Expected result

A 1280 by 720 resizable desktop window should show a red, green, and blue interpolated triangle over a dark blue-grey clear colour. Presentation is synchronized to the display by default. The current-session log is written beside the executable under:

```text
runtime/log/Daedalus.log
```

Startup logging includes the application version, build type, process architecture, dimensions, adapter identity, dedicated video memory, feature level, debug-layer state, WARP state, shader paths, and swap-chain buffer count.

## Required local graphical validation

Perform this checklist on the exact source snapshot being accepted:

1. Launch the Debug hardware build and confirm the expected adapter in the log.
2. Confirm the coloured triangle is visible and stable for at least 30 seconds.
3. Resize repeatedly, including to a very small client area.
4. Minimize, restore, and maximize.
5. Move between displays when more than one is available.
6. Use Alt+Tab repeatedly.
7. Close with the window close button and confirm a clean exit.
8. Run `Daedalus.exe --frames 120` and confirm automatic clean exit and exit code zero.
9. Run `Daedalus.exe --warp --frames 120` and confirm WARP plus clean exit.
10. Inspect the debugger and session log for D3D12 corruption, errors, and warnings.
11. Repeat the relevant checks in Release.

For a follow-up audit, preserve:

- Complete configure, build, and CTest output for Debug and Release.
- `runtime/log/Daedalus.log` from hardware, `--frames 120`, and WARP runs.
- Debugger output containing any D3D12 or DXGI diagnostics.
- A screenshot of the triangle after launch and after a resize.
- The GPU model, driver version, Windows version, Windows SDK version, CMake version, and DXC version.

## Debug-layer troubleshooting

**The debug layer is unavailable**

Install **Settings → System → Optional features → View features → Graphics Tools**, then rerun the Debug executable. The application reports the HRESULT rather than terminating without context.

**DXC cannot be found**

Confirm that a recent Windows SDK is installed. Run `where.exe dxc`, or pass `-DDXC_PATH=<path>` to configuration.

**The Visual Studio 2022 preset reports MSB8020**

Install the MSVC v143 x64/x86 build tools, or use the Visual Studio 2026 presets on a machine with v145:

```powershell
.\scripts\configure.ps1 -Configuration Debug -VisualStudioVersion 2026 -DxcPath $Dxc
```

**Hardware initialization fails**

Read the logged HRESULT and adapter enumeration output. Daedalus deliberately does not switch to software rendering. Use `--warp` only as an explicit diagnostic comparison.

**Shader binaries are missing at runtime**

Build the `Daedalus` target rather than launching an incomplete intermediate output. The build copies `TriangleVS.dxil` and `TrianglePS.dxil` to the executable's `shaders` directory.

**Resize-related failure**

Return the session log and debugger output. The intended sequence waits for outstanding GPU work, releases all old back-buffer references, calls `ResizeBuffers`, obtains the new current index, and recreates RTVs.

## Repository layout

```text
.github/workflows/   Windows compile-and-CPU-test workflow
cmake/               DXC discovery and shader build logic
docs/                Architecture, acceptance evidence, research, and agent log
scripts/             Configure, build, test, run, clean, and source-package helpers
shaders/             HLSL source
src/core/            Command-line, diagnostics, logging, and version utilities
src/platform/        Win32 window and operating-system handle ownership
src/graphics/        Adapter policy, D3D12 frame infrastructure, and triangle renderer
tests/               Dependency-free CPU test executable
```

## Intentional Campaign A simplifications

- The three immutable demonstration vertices live in an upload heap. A later asset-upload design should stage immutable geometry into default memory.
- There is no depth buffer because one triangle does not require it.
- The root signature is empty because the shaders consume only vertex attributes.
- There are two frames in flight, matching the swap-chain buffer count.
- Rendering is single-threaded.
- Shader bytecode is compiled during the build and loaded from disk at startup.

## Explicit non-goals

Campaign A excludes ray tracing, acceleration structures, path tracing, denoising, DLSS, Streamline, NGX, Agility SDK deployment, model import, PBR, textures, user-interface frameworks, render graphs, scene frameworks, gameplay, audio, networking, plug-ins, custom allocators, shader-reflection systems, shader hot reload, multithreaded rendering, cross-API abstraction, Vulkan, OpenGL, and editor tooling.

## CI boundary

`.github/workflows/windows-build.yml` configures, builds, and runs CPU tests for Debug and Release on a Windows Server 2022 GitHub-hosted runner using the Visual Studio 2022 presets. It validates compilation and shader compilation when the runner's SDK supplies DXC. It does not validate visible rendering, display transitions, WARP execution, or debug-layer cleanliness.

## Source packaging

The packaging helper is designed to run from a normal Git checkout. It ignores `.git` metadata while scanning and copying, but rejects generated build products and restricted binaries elsewhere in the working tree.

```powershell
.\scripts\clean.ps1
.\scripts\package-source.ps1
```

It creates `Project-Daedalus-Campaign-A.zip`, inspects the resulting entry list, and prints the SHA-256 hash.

## License status

A project license has not yet been selected. No license grant should be inferred from the presence of this source archive.

## Next planned campaign

Campaign B is expected to introduce the first asset-backed raster rendering path. Its exact acceptance scope should be approved only after the current closure patch passes Windows Debug, Release, WARP, and CI revalidation.
