# Project Daedalus

Project Daedalus is an AI-assisted Windows rendering laboratory. Version 0.0.1 contains Campaign A: a deliberately small Win32 and Direct3D 12 foundation that is intended to be audited before later rendering systems are added.

## Campaign A status

The source implements:

- A native resizable Win32 window and message loop.
- Hardware adapter enumeration with an explicit `--warp` diagnostic mode.
- A Direct3D 12 device, direct command queue, two swap-chain buffers, one command allocator per buffer, a graphics command list, and fence synchronization.
- A flip-discard swap chain with vertical synchronization enabled.
- DXC build-time compilation of Shader Model 6.0 vertex and pixel shaders.
- A minimal root signature and graphics pipeline that render an interpolated-colour triangle.
- Explicit `PRESENT` to `RENDER_TARGET` and `RENDER_TARGET` to `PRESENT` transitions.
- Safe minimize, restore, resize, and shutdown paths.
- Session logging, HRESULT diagnostics, CPU tests, CTest registration, and Windows CI configuration.

The repository was produced in a Linux environment. The portable core and CPU tests were compiled and run there, but the Win32 application, DXC shader build, D3D12 debug layer, and graphical output require local Windows validation. The exact evidence is recorded in `docs/campaigns/campaign-a-acceptance.md`.

## Prerequisites

Use a 64-bit Windows 11 or compatible Windows 10/11 development system with:

1. Visual Studio 2022 or Visual Studio 2022 Build Tools.
2. The **Desktop development with C++** workload.
3. MSVC v143 x64 build tools.
4. A recent Windows 10 or Windows 11 SDK.
5. CMake 3.25 or newer.
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
cmake --preset windows-msvc-debug -DDXC_PATH="C:\path\to\dxc.exe"
```

## Configure, build, and test

From a Visual Studio 2022 Developer PowerShell:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Release:

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
```

Equivalent helpers:

```powershell
.\scripts\configure.ps1 -Configuration Debug
.\scripts\build.ps1 -Configuration Debug
.\scripts\test.ps1 -Configuration Debug
```

The presets use Visual Studio 2022, x64, out-of-source build directories, C++20, `/W4`, `/permissive-`, `/Zc:__cplusplus`, `/utf-8`, and warnings-as-errors for project-owned C++.

## Run

Normal hardware adapter:

```powershell
.\build\windows-msvc-debug\Debug\Daedalus.exe
```

Explicit WARP:

```powershell
.\build\windows-msvc-debug\Debug\Daedalus.exe --warp
```

Repeatable 120-frame smoke run:

```powershell
.\build\windows-msvc-debug\Debug\Daedalus.exe --frames 120
```

Helper equivalents:

```powershell
.\scripts\run.ps1 -Configuration Debug
.\scripts\run.ps1 -Configuration Debug -Warp
.\scripts\run.ps1 -Configuration Debug -Frames 120
```

Help does not initialize Direct3D 12:

```powershell
.\build\windows-msvc-debug\Debug\Daedalus.exe --help
```

Invalid arguments return a nonzero exit code and print usage information.

## Expected result

A 1280 by 720 resizable desktop window should show a red, green, and blue interpolated triangle over a dark blue-grey clear colour. Presentation is synchronized to the display by default. The current-session log is written beside the executable under:

```text
runtime/log/Daedalus.log
```

Startup logging includes the application version, build type, process architecture, dimensions, adapter identity, dedicated video memory, feature level, debug-layer state, WARP state, shader paths, and swap-chain buffer count.

## Required local graphical validation

Perform this checklist on the target Windows system:

1. Launch the Debug hardware build and confirm the expected adapter in the log.
2. Confirm the coloured triangle is visible and stable for at least 30 seconds.
3. Resize repeatedly, including to a very small client area.
4. Minimize, restore, and maximize.
5. Move between displays when more than one is available.
6. Use Alt+Tab repeatedly.
7. Close with the window close button and confirm a clean exit.
8. Run `Daedalus.exe --frames 120` and confirm automatic clean exit.
9. Run `Daedalus.exe --warp` and confirm WARP is reported.
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
src/platform/        Win32 window ownership and message dispatch
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

`.github/workflows/windows-build.yml` configures, builds, and runs CPU tests for Debug and Release on a Windows Server 2022 GitHub-hosted runner. It validates compilation and shader compilation when the runner's SDK supplies DXC. It does not validate visible rendering, display transitions, WARP execution, or debug-layer cleanliness.

## Source packaging

After removing generated directories:

```powershell
.\scripts\clean.ps1
.\scripts\package-source.ps1
```

The packaging helper rejects generated build products and restricted binary types before creating `Project-Daedalus-Campaign-A.zip`, then inspects the resulting entry list and prints its SHA-256 hash.

## License status

A project license has not yet been selected. No license grant should be inferred from the presence of this source archive.

## Next planned campaign

Campaign B is expected to introduce the first asset-backed raster rendering path. Its exact acceptance scope should be approved only after Campaign A receives a successful Windows runtime audit.
