# Campaign B Windows hardening follow-up

**Date:** 2026-08-08  
**Input archive:** `Project-Daedalus-main(5).zip`  
**Input SHA-256:** `6acb9714c676b32657b3cafe4ed375a5ad5d843b48772853211f4dbf8de2b16d`  
**Suggested branch/PR:** `fix/campaign-b-windows-hardening`

## Why this follow-up exists

Developer-side VS2026 validation exposed Windows-only integration failures that the portable Campaign B closure could not execute. This pass audits for the same classes of failure plus higher-risk runtime shutdown/automation problems, applies source fixes, and adds a fast preflight so future campaigns fail earlier.

## Findings closed in source

### WH-01 — Win32 `min`/`max` macro leakage

`src/assets/ImageDecoder.cpp` uses `std::numeric_limits<T>::max()` and includes `Windows.h`. The baseline depended on target-level `NOMINMAX`; a missing target definition produced the observed MSVC C4003/C2589 failure. Every active source/header that directly includes `Windows.h` now defensively defines both `WIN32_LEAN_AND_MEAN` and `NOMINMAX` before the include. The existing `daedalus_assets` CMake definition remains as a second layer.

### WH-02 — implicit constant-buffer tail padding

The diagnostic CPU constant block contained 236 explicit bytes with `alignas(16)` and relied on MSVC to add four bytes of tail padding. `/W4 /WX` therefore raised C4324. The CPU/HLSL ABI now includes an explicit final `padding2` word. The CPU contract moved to portable `rendering/DiagnosticShaderContract.h` with compile-time size/alignment/offset assertions, and portable tests exercise the same layout.

### WH-03 — acceptance wrapper drift

`scripts/run.ps1` lagged the executable and could not invoke `tangents`, reload stress, alternate-asset stress, resize stress, or live-object reporting. The wrapper now exposes all of those options plus automation-safe error-dialog suppression.

### WH-04 — modal error dialog could deadlock unattended validation

Fatal runtime errors always opened `MessageBoxA`. Expected malformed-asset tests or CI smoke runs could therefore block forever waiting for human input. Frame-limited and stress runs now suppress modal fatal-error dialogs automatically. `--no-error-dialog` provides explicit suppression for other automation.

### WH-05 — unbounded D3D12 fence waits during failure/shutdown

Fence waits used `INFINITE`, including the no-throw shutdown path. A hung/removed GPU could permanently wedge stress validation or shutdown. Fence waits now use a bounded 30-second timeout, detect a fence/device-removal sentinel, query the device-removed reason, and retain graphics resources instead of releasing potentially in-flight objects when idle cannot be proven.

### WH-06 — orphaned dead Windows implementations

The repository still carried unbuilt `TriangleRenderer.*` and `WicImageDecoder.*` implementations. They were not part of the canonical Campaign B path and no compiler/CI job validated them, so they could silently rot and confuse audits. They were removed; Campaign A documents remain historical records.

### WH-07 — missing source-level regression preflight

`scripts/source-health.py` now rejects:
- Markdown code fences accidentally pasted into C++/HLSL/CMake source;
- direct `Windows.h` includes without `NOMINMAX`/`WIN32_LEAN_AND_MEAN`;
- loss of the explicit CPU/HLSL padding sentinel;
- `run.ps1` feature drift for Campaign B diagnostics/stress controls;
- orphaned `.cpp` files not built by CMake;
- duplicated/missing high-value root CMake structural sentinels.

GitHub Actions runs this preflight on Ubuntu and Windows before configure/build.

## Validation performed in the patch environment

- GNU 14.2 portable Debug with warnings-as-errors: PASS.
- GNU 14.2 portable Release with warnings-as-errors: PASS.
- Clang 17 Release with warnings-as-errors: PASS.
- Clang 17 ASan+UBSan Debug: PASS.
- CTest: Core 9, Scene 8, Assets 17; all PASS.
- Controlled corpus: 10 valid/degraded accepted, 25 invalid rejected as expected.
- Fixture regeneration repeated with identical tree hash: `4a327e4c2a7ea1a4d275548fc539ccb0f5dd9c2acf7feb60c88da2e307ba4bc9`.
- Source-health preflight: PASS.

## Still requiring developer-side Windows evidence

This environment cannot execute MSVC/DXC/D3D12. The repaired snapshot must still be configured and built on VS2026/v145 and then run on hardware and WARP. In particular, verify that MSVC no longer reports C4003/C2589 in `ImageDecoder.cpp` or C4324 for the diagnostic constant contract, then continue the Campaign B hardware/WARP/stress/live-object acceptance matrix.

## Final Windows closure follow-up — 2026-08-20

Developer execution of the strict-image-validation snapshot passed VS2026 Debug/Release CTest, hardware/WARP rendering, diagnostic modes, camera use, and 100-cycle reload/resize stress. The run exposed three final plumbing defects that are repaired in the current source:

- `DXGIGetDebugInterface1` is now called through the DXGI 1.3 API instead of being incorrectly searched in `dxgidebug.dll`;
- `Application::shutdown()` is idempotent, preventing the destructor from requesting a second live-object report after normal shutdown;
- relative `run.ps1 -ImportReport` and explicit relative `package-source.ps1 -OutputPath` values resolve against the PowerShell caller directory rather than the host process environment directory.

`source-health.py` guards these contracts. The exact final archive still requires one Windows Debug rebuild/live-object debugger inspection before B-24/B-25 can be promoted.
