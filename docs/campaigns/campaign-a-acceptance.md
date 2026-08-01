# Campaign A acceptance matrix

## Evidence boundary

Two evidence sets are distinguished deliberately:

1. **Developer Windows baseline, 2026-08-01.** The pre-closure snapshot configured and built in Visual Studio 2026 Developer PowerShell with MSVC 19.51/v145, Windows SDK 10.0.26100.0, and DXC 1.8.2502.11. CTest passed, the RTX 4060 Ti hardware path presented frames, resize/minimize/restore succeeded, and explicit WARP initialization succeeded.
2. **Closure-patch validation.** The current snapshot repairs shutdown ordering, constructor-time Win32 resource ownership, packaging from a Git checkout, and Visual Studio 2026 presets. The closure environment is Linux x86-64 and cannot compile or execute the modified Win32/D3D12 translation units. Portable CPU tests, preset parsing, source review, hygiene checks, and archive inspection are run on this exact snapshot.

The baseline runtime evidence remains useful, but it is not presented as execution evidence for source files changed by the closure patch.

## Developer Windows baseline evidence

The supplied Windows transcript recorded:

- Visual Studio 2026 18.8.2 and MSVC 19.51.36252 with the v145 x64 toolset.
- Windows SDK 10.0.26100.0 and 64-bit DXC 1.8.2502.11.
- Successful CMake generation with `Visual Studio 18 2026`, `-A x64`, and `-T v145,host=x64`.
- Successful compilation of `Daedalus.exe`, `DaedalusCoreTests.exe`, `TriangleVS.dxil`, and `TrianglePS.dxil`.
- CTest: one registered test, one passed, zero failed.
- Direct test execution: five test cases passed.
- Hardware selection: NVIDIA GeForce RTX 4060 Ti, 7949 MiB dedicated memory.
- D3D12 feature level 12.1, debug layer enabled, two flip-discard buffers, vertical synchronization enabled.
- A 120-frame hardware run with exit code zero and clean shutdown.
- An interactive hardware run that presented 1468 frames, minimized/restored, resized the swap chain to 1920 by 1017, and exited cleanly.
- Explicit WARP selection reported as Microsoft Basic Render Driver at feature level 12.1.
- No matches for `ERROR`, `CORRUPTION`, `DEVICE_REMOVED`, `DEVICE_RESET`, or `failed` in the captured hardware-session log.

Release execution, a complete captured WARP shutdown, multi-display movement, repeated Alt+Tab, a very-small-window stress case, and GitHub Actions were not supplied.

## Acceptance criteria for the closure snapshot

| Requirement | Status | Evidence | Validation command | Actually run on this snapshot | Remaining limitation |
|---|---|---|---|---|---|
| A-01 — Clean Configuration | BLOCKED | VS2022 and VS2026 Debug/Release presets are present, out-of-source, and parse correctly. | `cmake --preset windows-msvc-debug`; `cmake --preset windows-vs2026-debug`; corresponding Release presets | Preset parsing yes; Windows generation no | Exact closure snapshot requires Windows CMake generation. |
| A-02 — Clean Compilation | BLOCKED | Portable core compiles under GNU and Clang with warnings-as-errors. Windows sources were changed by the closure patch. | Build all four Windows presets. | Portable builds yes; MSVC build no | Debug and Release MSVC builds are required. |
| A-03 — Shader Compilation | BLOCKED | DXC commands and dependencies are unchanged; the baseline compiled both shaders. | Build a Windows preset. | No on closure snapshot | DXC must run again with the repaired snapshot. |
| A-04 — CPU Tests | PASS | One CTest registration executes five assertion-based cases for command-line behavior, result diagnostics, and adapter policy. GNU and Clang runs pass. | `ctest --test-dir build-linux-gcc --output-on-failure`; Clang equivalent | Yes | Windows CTest should also be repeated. |
| A-05 — Window Lifecycle | BLOCKED | Constructor cleanup now unregisters only a class owned by the object, destroys partial windows on failure, and clears `GWLP_USERDATA` at `WM_NCDESTROY`. Baseline lifecycle execution passed. | Manual Debug lifecycle checklist | Source reviewed; not executed after repair | Requires Windows regression run. |
| A-06 — Device Initialization | BLOCKED | D3D12 device/queue/swap-chain/frame infrastructure remains implemented; fence event now has RAII ownership during partial construction. | Launch Debug hardware build. | No on closure snapshot | Requires live D3D12 initialization. |
| A-07 — Hardware Selection | BLOCKED | Adapter policy is unchanged and CPU-tested; baseline selected and logged the RTX 4060 Ti. | `Daedalus.exe --frames 120` | No on closure snapshot | Requires hardware rerun. |
| A-08 — WARP Selection | BLOCKED | `--warp` remains explicit and baseline selected Microsoft Basic Render Driver. | `Daedalus.exe --warp --frames 120` | No on closure snapshot | Capture clean WARP exit and exit code. |
| A-09 — Triangle Rendering | BLOCKED | Draw path and shaders are unchanged; baseline visually rendered and presented. | Launch and visually inspect. | No on closure snapshot | Requires visual rerun. |
| A-10 — Frame Synchronization | BLOCKED | Two allocator/fence frame resources remain. New `gpu_idle_proven_` tracking prevents redundant flushes while retaining fence-gated reuse. | Debug-layer runtime and source review | Source review only | Requires D3D12 debug-layer regression run. |
| A-11 — Resource States | BLOCKED | Explicit `PRESENT -> RENDER_TARGET -> PRESENT` barriers remain unchanged. | Debug-layer runtime | No on closure snapshot | Requires command execution. |
| A-12 — Resize Safety | BLOCKED | Existing flush/release/`ResizeBuffers`/reacquire path remains; baseline resize/minimize/restore passed. | Repeated resize/minimize/restore checklist | No on closure snapshot | Repeat including very small client size. |
| A-13 — Shutdown Safety | BLOCKED | Exceptional cleanup now requires proof of GPU idleness before destroying renderer resources. The fence event is RAII-owned. If synchronization cannot be proven during fatal process exit, ownership is deliberately detached for operating-system cleanup rather than releasing potentially referenced GPU objects. | Hardware, WARP, close-button, and injected-failure testing | Source reviewed; normal runtime baseline predates fix | Exact repaired failure path needs Windows validation. |
| A-14 — Diagnostics | PASS | Result helpers preserve code, operation, and source location; cleanup failures now log signal, event-registration, wait, window-destruction, and class-unregistration failures. CPU tests pass. | CPU tests and source review | Yes | Windows system-message expansion remains to be rerun. |
| A-15 — Validation Honesty | PASS | This matrix separates baseline execution from exact-snapshot execution and does not promote unrerun Windows checks to PASS. | Review matrix against command logs. | Yes | Update after local closure rerun. |
| A-16 — CI | NOT RUN | Workflow configures/builds/tests Debug and Release with the VS2022 presets. | Push branch or open pull request. | No | A successful hosted run is required. |
| A-17 — Documentation | PASS | README, architecture, acceptance matrix, references, and agent log describe the repaired implementation and evidence boundary. | Documentation review | Yes | Add final Windows rerun results when available. |
| A-18 — Source Hygiene | PASS | Exact archive is produced from a clean staging tree; `.git` is excluded rather than treated as an error; prohibited artefacts are inspected and absent. | Packaging and ZIP inspection recorded in delivery manifest | Yes through equivalent container procedure | PowerShell 5.1 execution remains to be confirmed locally. |

## Manual runtime checklist status

| Check | Baseline result | Closure snapshot status |
|---|---|---|
| Debug hardware launch and expected adapter | PASS | NOT RUN |
| Coloured triangle visible | PASS by developer observation | NOT RUN |
| Stable run for at least 30 seconds | NOT RUN; captured interactive run was about 19 seconds | NOT RUN |
| Repeated resize | PASS | NOT RUN |
| Very small client-area resize | NOT RUN | NOT RUN |
| Minimize and restore | PASS | NOT RUN |
| Maximize | Part of resize sequence but not explicitly recorded | NOT RUN |
| Move between displays | NOT RUN | NOT RUN |
| Repeated Alt+Tab | NOT RUN | NOT RUN |
| Close-button clean exit | PASS | NOT RUN |
| `--frames 120` clean exit | PASS, exit code zero | NOT RUN |
| WARP selection | PASS for initialization | NOT RUN; complete exit evidence still needed |
| Debugger/log diagnostics inspection | Application log scan passed | NOT RUN after repair |
| Debug and Release execution | Debug PASS | Debug and Release NOT RUN after repair |

## Current campaign conclusion

The closure patch addresses the three concrete audit defects: exceptional renderer teardown ordering, constructor-time raw-resource leakage, and packaging failure inside a normal Git checkout. It also adds first-class Visual Studio 2026 presets and helper-script selection.

Campaign A is **not declared fully accepted yet** because the modified Windows source has not been rebuilt and rerun in Debug and Release, the hosted CI workflow has not supplied a successful run, and several manual stress checks remain outstanding. The remaining work is validation rather than another architectural implementation campaign.
