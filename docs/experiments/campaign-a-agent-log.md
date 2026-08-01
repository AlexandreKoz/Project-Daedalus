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

- A pre-closure Windows Debug snapshot compiled and ran successfully; the closure-modified Windows files still require Debug and Release revalidation.
- The upload-heap vertex buffer is intentionally simple and should be replaced by an explicit staging/default-heap path when real assets arrive.
- Device removal is reported, but Campaign A does not attempt device recovery.
- No frame pacing exists beyond vertical synchronization and minimized-window sleep.
- CI assumes an installed Windows SDK exposes DXC through one of the documented discovery paths; the first hosted run must confirm the runner image.
- Version 0.0.1 is repeated in CMake, `VERSION`, and the source version header; a later maintenance patch should generate the source constant from one authoritative value.
- The adapter policy favors dedicated memory after DXGI preference ordering; future hybrid-laptop testing may motivate a user-selectable adapter identifier.

## 2026-08-01 developer Windows baseline

The developer supplied a complete local Debug transcript from a Windows 11 system after the initial archive was placed in a Git repository. The observed environment and results were:

- Visual Studio 2026 Developer PowerShell 18.8.2.
- MSVC 19.51.36252 with v145 and x64 host/target tools.
- Windows SDK 10.0.26100.0.
- DXC 1.8.2502.11.
- Successful `Visual Studio 18 2026` CMake generation.
- Successful build of the application, CPU test executable, and both DXIL shaders.
- CTest one of one passed and five direct test cases passed.
- RTX 4060 Ti selected with 7949 MiB dedicated video memory.
- D3D12 feature level 12.1 and debug layer enabled.
- Two-buffer flip-discard swap chain with vertical synchronization.
- A 120-frame hardware run exited with code zero.
- An interactive run presented 1468 frames, logged minimize/restore and a resize to 1920 by 1017, then exited cleanly.
- Explicit WARP selection reported Microsoft Basic Render Driver.
- The captured hardware log contained no match for the requested error, corruption, device-removal, reset, or failure patterns.

The transcript did not include Release results, a complete WARP exit, a 30-second run, very-small-window stress, multi-display movement, repeated Alt+Tab, or a hosted CI run.

## 2026-08-01 closure audit defects

A follow-up source audit identified three concrete defects:

1. `Application::shutdown` destroyed `TriangleRenderer` before the no-throw context flush on exceptional paths. Previously submitted work could still reference the renderer's PSO, root signature, or vertex buffer.
2. The D3D12 fence event was a raw `HANDLE`. If context construction threw after event creation, the class destructor would not run and the handle could leak. Window-class registration had a similar constructor-failure cleanup gap.
3. `package-source.ps1` classified `.git` as a prohibited directory during preflight, so it rejected an ordinary Git checkout even though the later copy stage already excluded `.git`.

The audit also noted stale documentation and the absence of first-class Visual Studio 2026 presets despite the developer's actual environment.

## 2026-08-01 closure implementation

The closure patch made these changes:

- Added `UniqueWin32Handle`, a narrow move-only RAII owner used for the D3D12 fence event.
- Added `gpu_idle_proven_` tracking and a no-throw `try_wait_for_gpu` path with explicit logging for signal, event-registration, and wait failures.
- Added `prepare_for_shutdown`, called before renderer destruction on every application cleanup path.
- Added a fatal-path abandonment policy: when GPU idleness cannot be proven, ownership is detached and left for operating-system process cleanup instead of releasing objects potentially referenced by queued work.
- Preserved the throwing `wait_for_gpu` path for normal successful shutdown and resize validation.
- Made Win32 window construction clean up owned class registration and any partially created window on failure.
- Unregisters a window class only when the object actually registered it and clears `GWLP_USERDATA` on `WM_NCDESTROY`.
- Modified source packaging to ignore Git metadata during preflight while continuing to exclude it from the archive.
- Added Visual Studio 2026 Debug and Release presets using `Visual Studio 18 2026` and `v145,host=x64`.
- Added `-VisualStudioVersion 2022|2026` selection to configure, build, test, and run helpers while keeping VS2022 as the default and CI baseline.
- Reconciled README, architecture, acceptance, and this experiment log with the developer evidence and the exact-snapshot validation boundary.

## Closure validation performed

The closure environment remained Linux and could not execute PowerShell, MSVC, DXC, Win32, or D3D12. Validation on the repaired snapshot therefore included:

```text
cmake -S . -B build-linux-gcc -G Ninja \
  -DDAEDALUS_BUILD_APP=OFF \
  -DDAEDALUS_BUILD_TESTS=ON \
  -DDAEDALUS_WARNINGS_AS_ERRORS=ON \
  -DBUILD_TESTING=ON
cmake --build build-linux-gcc --parallel 2
ctest --test-dir build-linux-gcc --output-on-failure

cmake -S . -B build-linux-clang -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DDAEDALUS_BUILD_APP=OFF \
  -DDAEDALUS_BUILD_TESTS=ON \
  -DDAEDALUS_WARNINGS_AS_ERRORS=ON \
  -DBUILD_TESTING=ON
cmake --build build-linux-clang --parallel 2
ctest --test-dir build-linux-clang --output-on-failure

cmake --list-presets=all
```

The final command results, source counts, marker scan, archive inspection, and SHA-256 are recorded in the external delivery manifest generated after packaging.

## Closure validation limitations

- The modified Win32 and D3D12 translation units were not compiled under MSVC in the closure environment.
- PowerShell helper syntax received source review but could not be executed in the closure environment.
- Exceptional shutdown behavior was repaired by ownership and ordering analysis but still needs a Windows regression run.
- The VS2026 presets need local Debug and Release execution on the developer machine.
- GitHub Actions remains unexecuted.

## Closure source statistics before packaging

- Repository file count: 43.
- C and C++ source/header count: 20.
- HLSL source count: 1.
- Registered CTest tests: 1.
- Assertion-based test cases: 5.
- Approximate repository text lines: 3759.
