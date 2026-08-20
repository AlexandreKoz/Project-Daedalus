# Campaign B AI-agent experiment log — audit closure

## Baseline

- Input: `Project-Daedalus-main(3)(1).zip`
- SHA-256: `5397703f3cba8f4649f46f1ed2844a14f82198a9888dde2b34303bdf2348da32`
- Audit basis: Campaign B adversarial acceptance audit dated 2026-08-02.
- Branch/PR name: `fix/campaign-b-audit-closure`.

## Preserved historical chronology

1. Original Campaign B implementation passed portable tests but Windows runtime items were initially unavailable to the coding agent.
2. Developer local VS2026 evidence later proved Debug/Release builds and hardware rendering.
3. Debug WARP crashed with `0xC0000005` in `d3d10warp.dll` while `DiagnosticSceneRenderer::create_pipeline_states` called `CreateGraphicsPipelineState`.
4. Human-guided isolation tested embedded/separate shader debug data, Release DXIL in the Debug executable, `-Od`, `-Zi -O3`, and mixed vertex/pixel stages.
5. Either shader stage compiled with `-Od` reproduced the WARP crash. `-Zi -O3` passed with the D3D12 debug layer.
6. Debug HLSL policy changed to `-Zi -O3 -Qembed_debug`. This reduces unoptimized shader-debug fidelity and is recorded as an explicit limitation.

This diagnosis is a **Level-3 human intervention**: the human provided environment operation, debugger evidence, controlled experiments, and the decisive compiler-flag result. It is not counted as zero intervention and is not rewritten as autonomous success.

## Audit-closure implementation

- F-01: explicit glTF default-material sentinel and regression fixture.
- F-02: UV0/UV1 validation, runtime-neutral draw preparation, draw constants, and HLSL selection.
- F-03: full assets-layer PNG/JPEG decode and canonical RGBA8 ownership.
- F-04: checked retained/peak resource accounting and report counters.
- F-05: explicit unit-vector/quaternion repair/rejection diagnostics.
- F-06: tangent diagnostic mode.
- F-07: one-mesh/two-node instance fixture with negative scale.
- F-08: deterministic reload/alternate-asset/resize stress options and post-teardown D3D12/DXGI reports.
- F-09: evidence/documentation chronology repaired.
- F-10: deterministic explicit-entry packaging.

## Validation performed by this agent

Environment: Linux, GNU and Clang C++20 toolchains, no Win32/D3D12 runtime.

- Portable Debug and Release configure/build/test with warnings as errors.
- Clang 17 Release second-compiler build.
- GNU AddressSanitizer plus UndefinedBehaviorSanitizer Debug build and CTest run.
- Expanded CTest: Core 9, Scene 7, Assets 17.
- Controlled corpus: 10 valid/degraded accepted, 25 invalid rejected.
- Fixture generator two-run identity: complete fixture-tree SHA-256 `e5af0b17b8eb46aae3a97c279c6a3c12b275905519752f76059909d5d52811d9`.
- Deterministic report/location/dependency/settings tests.
- Canonical Python package two-run byte identity; PowerShell companion source-reviewed only.
- Fresh archive extraction, configure/build/test, validator corpus run, and prohibited-artifact/path scan.

## Honest unavailable evidence

- Updated exact-snapshot Windows compiler and HLSL build.
- Hardware and WARP rendering after image/UV/tangent changes.
- Interactive camera behavior.
- Runtime stress execution.
- DXGI live-object debugger output.
- Hosted CI run.

Those rows remain `BLOCKED` or `NOT RUN` in the acceptance matrix.

## Windows malformed-image follow-up — 2026-08-20

Developer-supplied VS2026 evidence is treated as **Level-2 diagnostic evidence** for this follow-up. The exact Windows build configured and compiled successfully, `Daedalus.Core` and `Daedalus.Scene` passed, and `Daedalus.Assets` failed only because Windows WIC accepted `invalid/corrupt_entropy_jpeg.gltf`. An earlier WIC-indexing attempt did not change that result and was removed rather than retained as ineffective validation.

The repair moves the strictness boundary into project-owned code: baseline sequential JPEG scans are walked using their declared Huffman tables, sampling factors, expected MCU/block count, restart intervals, coefficient-category limits, and marker/padding boundaries before backend pixel decode. The existing PNG zlib-envelope guard remains. Progressive JPEG is still accepted through marker validation plus full backend pixel decode; the new strict Huffman/MCU walk intentionally applies only to baseline sequential scans.

Validation performed by the coding agent for this follow-up: source-health PASS; GNU warnings-as-errors portable Debug and Release configure/build/CTest PASS; direct valid JPEG acceptance PASS; direct corrupt baseline-JPEG `--expect-failure` PASS with structured `invalid_image`. Windows/WIC execution of the patched archive remains required before B-16 can be changed from BLOCKED to PASS.

