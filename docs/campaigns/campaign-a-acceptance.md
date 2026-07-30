# Campaign A acceptance matrix

## Validation environment

Repository generation and available validation were performed in a Linux x86-64 container on 2026-07-30. The container provided CMake 3.31.6, GNU C++ 14.2.0, Clang 17.0.0, Ninja, Python 3.13.5, and ZIP tooling. It did not provide Windows, Visual Studio, the Windows SDK, DXC, PowerShell, a desktop session, DXGI, a D3D12 device, or Graphics Tools.

A deliberate default configuration attempt on Linux failed with the repository's platform guard and explained how to run CPU-only validation. Two CPU-only configurations then compiled with warnings-as-errors and passed CTest.

## Acceptance criteria

| Requirement | Status | Evidence | Validation command | Actually run | Remaining limitation |
|---|---|---|---|---|---|
| A-01 — Clean Configuration | NOT RUN | Debug and Release Visual Studio 2022 x64 presets are present and listed by CMake. | `cmake --preset windows-msvc-debug`; `cmake --preset windows-msvc-release` | No | Visual Studio generator and Windows SDK were unavailable. |
| A-02 — Clean Compilation | NOT RUN | Targets, warning flags, x64 guard, and source dependencies are defined. GNU and Clang compiled the portable core with warnings-as-errors. | `cmake --build --preset windows-msvc-debug`; `cmake --build --preset windows-msvc-release` | No for Windows application; yes for portable core | MSVC compilation of Win32 and D3D12 translation units remains required. |
| A-03 — Shader Compilation | NOT RUN | DXC custom commands declare VSMain `vs_6_0` and PSMain `ps_6_0` outputs with configuration-specific flags and HLSL dependencies. | Build either Windows preset. | No | DXC was unavailable in the container. |
| A-04 — CPU Tests | PASS | One CTest registration executes five assertion-based cases covering command-line parsing, invalid arguments, HRESULT-style formatting, result preservation, and adapter policy. GNU and Clang runs passed. | `ctest --test-dir build-linux-tests --output-on-failure`; `ctest --test-dir build-linux-clang --output-on-failure` | Yes | Windows CTest presets still require local execution. |
| A-05 — Window Lifecycle | NOT RUN | `Win32Window` owns registration, creation, message dispatch, size state, close handling, destruction, and class unregistration. | Launch Debug and perform the manual lifecycle checklist. | No | No Windows desktop session. |
| A-06 — Device Initialization | NOT RUN | `D3D12Context` creates factory, adapter, device, queue, swap chain, RTV heap, frame allocators, command list, fence, and event. | Launch Debug hardware build. | No | No Windows SDK or D3D12 runtime. |
| A-07 — Hardware Selection | NOT RUN | DXGI 1.6 high-performance enumeration is preferred, software candidates are rejected, D3D12 support is probed, and adapter properties are logged. | `Daedalus.exe --frames 120` | No | Requires a physical D3D12 adapter. |
| A-08 — WARP Selection | NOT RUN | `--warp` calls `EnumWarpAdapter`; normal mode never silently switches to WARP. | `Daedalus.exe --warp --frames 120` | No | WARP is unavailable outside Windows. |
| A-09 — Triangle Rendering | NOT RUN | The application loads DXC outputs, creates an empty root signature and PSO, binds three coloured vertices, clears, draws, and presents. | Launch Debug and visually inspect. | No | Visible output cannot be established in this environment. |
| A-10 — Frame Synchronization | NOT RUN | Two frame resources each own an allocator and latest fence value; allocator reset is gated by completion. Full waits are limited to resize, explicit flush, and shutdown. | Debug-layer runtime plus repeated operation. | No | Requires queue execution and debug-layer observation. |
| A-11 — Resource States | NOT RUN | Renderer records explicit back-buffer transitions in both directions every frame. | Debug-layer runtime. | No | Requires D3D12 command execution. |
| A-12 — Resize Safety | NOT RUN | Zero-sized rendering is skipped; resize flushes, releases old buffers, calls `ResizeBuffers`, refreshes index, reacquires buffers, and recreates RTVs. | Manual repeated resize/minimize/restore checklist. | No | Requires interactive Windows execution. |
| A-13 — Shutdown Safety | NOT RUN | Normal exit waits for GPU work; no-throw destruction signals and waits when possible, closes the event, and releases resources in dependency order. | Close button, frame-limited run, and WARP run. | No | Requires live queue work. |
| A-14 — Diagnostics | PASS | Result helpers preserve the code, operation, and source location; tests verify formatting and exception preservation. Logging writes console, debugger, and session-file output. | CPU tests; source review. | Yes for portable helper tests | Windows system-message expansion and fatal message-box presentation remain unobserved. |
| A-15 — Validation Honesty | PASS | This matrix separates source evidence from executed evidence and marks graphical checks unrun. | Review this document against command logs. | Yes | Local results must replace unrun rows after developer execution. |
| A-16 — CI | NOT RUN | Workflow uses a Windows Server 2022 hosted runner, official checkout action, Debug configure/build/test, then Release configure/build/test. | Push branch or open a pull request. | No | No GitHub workflow run occurred during repository generation. |
| A-17 — Documentation | PASS | README, architecture, acceptance, experiment log, and authoritative references match the delivered source and recorded limitations. | Source review and link check. | Yes | Runtime sections await local evidence. |
| A-18 — Source Hygiene | PASS | Generated directories were removed; requested unfinished-work markers returned no matches; final ZIP entry inspection found no prohibited build products or restricted binaries. | Equivalent Python packaging and ZIP inspection recorded in the delivery manifest. | Yes | PowerShell was unavailable, so `package-source.ps1` received source review but was not executed. The separate delivery manifest remains outside the archive so it can contain the final archive hash. |

## Manual runtime checklist status

Every item below is **NOT RUN** because the execution environment lacked a Windows graphical session and D3D12 runtime:

1. Debug hardware launch and adapter confirmation.
2. Visible coloured triangle confirmation.
3. Thirty-second stable run.
4. Repeated resize.
5. Very small client-area resize.
6. Minimize.
7. Restore.
8. Maximize.
9. Multi-display movement.
10. Repeated Alt+Tab.
11. Close-button exit.
12. `--frames 120` exit.
13. WARP execution.
14. Debugger and log inspection for D3D12 diagnostics.
15. Debug and Release graphical execution.

## Current campaign conclusion

Campaign A has a complete source implementation and passes all CPU-only validation available in the generation environment. It cannot yet be declared fully accepted because A-01 through A-03, A-05 through A-13, and A-16 require Windows/MSVC/DXC, live D3D12 execution, or GitHub-hosted CI evidence.
