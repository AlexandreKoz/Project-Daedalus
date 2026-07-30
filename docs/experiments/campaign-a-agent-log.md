# Campaign A agent log

## Initial assumptions

- No repository existed at task start.
- Target identity, version, executable name, namespace, branch name, pull-request title, and initial commit title were supplied by the human specification.
- The implementation environment was expected to lack Windows graphics execution, so validation claims would be limited to observable evidence.
- Campaign A would remain a focused triangle foundation and would not prebuild later rendering systems.

## Human guidance applied

The human specification required a complete source repository rather than advice, authoritative research before implementation, Direct3D 12 and DXC, a small ownership model, correct frame synchronization, strict scope boundaries, CPU tests, Windows CI, honest acceptance reporting, source hygiene, and a clean ZIP delivery. It also prohibited silent WARP fallback, unsafe allocator reuse, per-frame full GPU waits, implicit back-buffer states, generated shader binaries in source, and speculative engine frameworks.

## Authoritative research

Research was performed first against Microsoft Learn, Microsoft DirectX repositories, Kitware CMake documentation, and GitHub documentation. The consulted sources and their exact use are recorded in `docs/references.md` with the 2026-07-30 access date.

## Architectural decisions

- Use a console-subsystem `wmain` so help and argument errors reliably reach standard streams while the application still creates a native Win32 window.
- Keep ownership as `Application -> Win32Window + D3D12Context + TriangleRenderer`.
- Use two swap-chain buffers and one command allocator per buffer.
- Store the latest queue fence value on each frame resource and wait only before unsafe reuse.
- Keep the renderer narrow: root signature, PSO, upload-heap demonstration vertex buffer, and draw recording.
- Keep adapter-ranking policy, command-line parsing, and result formatting free of live GPU dependencies so they can be tested outside Windows.
- Permit a documented `DAEDALUS_BUILD_APP=OFF` CPU-only configuration on non-Windows hosts while keeping the default application configuration Windows-only.
- Keep the delivery checksum manifest outside the ZIP. A file inside an archive cannot contain the final hash of that same archive without changing the hashed bytes.

## Implementation iterations

1. Created repository identity, formatting configuration, source-hygiene exclusions, shader source, and third-party notice.
2. Added dependency-light command-line, result, logging, version, and adapter-policy layers.
3. Added Win32 class registration, native window creation, message dispatch, coalesced resize reporting, minimize state, and deterministic destruction.
4. Added D3D12 debug setup, DXGI factory and adapter selection, device creation, command infrastructure, flip-model swap chain, RTVs, per-frame allocators, fence synchronization, resize, and shutdown.
5. Added DXC-loaded shaders, root signature, PSO, upload-heap triangle vertices, clear/draw commands, and explicit resource barriers.
6. Added application sequencing, session logging, frame-limited smoke behavior, fatal error surfacing, and command-line entry point.
7. Added CMake, presets, DXC custom commands, CTest, PowerShell helpers, and GitHub Actions workflow.
8. Added README, architecture, acceptance, references, and this log.
9. Performed marker search, generated-directory cleanup, source archive creation, ZIP entry review, counts, and checksum generation.

## Build and test failures actually encountered

### Expected platform-guard failure

Command:

```text
cmake -S . -B build-linux-default -G Ninja
```

Result: configuration exited with code 1 because the default application target requires Windows. The diagnostic explained the CPU-only configuration flags. This confirms the repository does not pretend that the D3D12 executable is portable.

### GNU warnings-as-errors failure

The first CPU-only GNU build failed because a helper used only by the Windows `FormatMessageA` path was compiled on Linux and reported as an unused function under `-Werror`.

Correction: moved that helper inside the `_WIN32` conditional. The same build then succeeded and CTest passed.

### Further defects corrected during source review

- Corrected UTF-16 to UTF-8 buffer sizing so `WideCharToMultiByte` has space for its terminating null before the string is shortened.
- Forwarded the actual `HWND` into instance message handling so messages after `WM_DESTROY` never call `DefWindowProcW` with a cleared member handle.
- Guarded local `WIN32_LEAN_AND_MEAN` definitions to avoid project warning failures when the macro is also supplied by CMake.
- Capped the adapter enumeration tie-break value to avoid unsigned underflow on an extreme enumeration index.
- Reworked PowerShell frame validation so the helper's default value is valid while an explicitly supplied zero is rejected.
- Replaced a .NET Core-only relative-path helper in the packaging script with logic compatible with Windows PowerShell 5.1.

No fabricated compiler or runtime failure is recorded.

## Commands run

```text
uname -a
cmake --version
clang++ --version
python3 --version

cmake -S . -B build-linux-default -G Ninja

cmake -S . -B build-linux-tests -G Ninja \
  -DDAEDALUS_BUILD_APP=OFF \
  -DDAEDALUS_BUILD_TESTS=ON \
  -DDAEDALUS_WARNINGS_AS_ERRORS=ON \
  -DBUILD_TESTING=ON
cmake --build build-linux-tests --parallel 2
ctest --test-dir build-linux-tests --output-on-failure

cmake -S . -B build-linux-clang -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DDAEDALUS_BUILD_APP=OFF \
  -DDAEDALUS_BUILD_TESTS=ON \
  -DDAEDALUS_WARNINGS_AS_ERRORS=ON \
  -DBUILD_TESTING=ON
cmake --build build-linux-clang --parallel 2
ctest --test-dir build-linux-clang --output-on-failure
./build-linux-clang/tests/DaedalusCoreTests

cmake -S . -B build-linux-sanitized -G Ninja \
  -DDAEDALUS_BUILD_APP=OFF \
  -DDAEDALUS_BUILD_TESTS=ON \
  -DDAEDALUS_WARNINGS_AS_ERRORS=ON \
  -DBUILD_TESTING=ON \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-linux-sanitized --parallel 2
ctest --test-dir build-linux-sanitized --output-on-failure

cmake --list-presets=all
```

Final hygiene and packaging commands are recorded after their execution in the delivery manifest.

## Test results

- GNU C++ 14.2.0: one CTest test registered; one passed; zero failed.
- Clang 17.0.0: one CTest test registered; one passed; zero failed.
- GNU AddressSanitizer and UndefinedBehaviorSanitizer build: one CTest test passed with zero sanitizer reports.
- Direct execution: five test cases passed.
- Tested behavior: default arguments, WARP and frame parsing, invalid argument rejection, result-code formatting, result exception preservation, hardware adapter ranking, and unsuitable-adapter rejection.

## Validation unavailable in this environment

- Visual Studio 2022 preset configuration.
- MSVC compilation of Win32 and D3D12 source.
- DXC execution and shader binary validation.
- D3D12 device creation on hardware or WARP.
- Debug-layer and information-queue observation.
- Visible triangle output.
- Interactive minimize, restore, resize, maximize, display movement, and Alt+Tab behavior.
- Frame-limited Windows execution.
- GitHub Actions execution.
- PowerShell script execution. Neither Windows PowerShell nor PowerShell 7 was installed, and the isolated container could not retrieve a portable PowerShell package. The final archive was therefore created and inspected with an equivalent Python procedure; the PowerShell helpers received source review only.

Exact local commands and requested return evidence are in the README.

## Source statistics

Final archive statistics are authoritative in the separate `DELIVERY_MANIFEST.md`:

- Repository file count: 41
- C and C++ source/header count: 19
- HLSL source count: 1
- Registered CTest tests: 1
- Assertion-based test cases: 5
- Approximate repository text lines: 3315

## Known technical debt

- The Windows application has received detailed source review but no MSVC compile evidence yet.
- The upload-heap vertex buffer is intentionally simple and should be replaced by an explicit staging/default-heap path when real assets arrive.
- Device removal is reported, but Campaign A does not attempt device recovery.
- No frame pacing exists beyond vertical synchronization and minimized-window sleep.
- CI assumes an installed Windows SDK exposes DXC through one of the documented discovery paths; the first hosted run must confirm the runner image.
- The adapter policy favors dedicated memory after DXGI preference ordering; future hybrid-laptop testing may motivate a user-selectable adapter identifier.
