# Campaign B acceptance matrix — audit-closure snapshot

Status vocabulary is limited to `PASS`, `FAIL`, `BLOCKED`, and `NOT RUN`.

This document separates historical developer-supplied Windows evidence from checks performed on the audit-closure source snapshot. The delivery environment is Linux and cannot compile or execute Win32/D3D12. Therefore no updated Windows result is inferred from portable success.

Portable commands actually run on the repaired source and again after final extraction:

```text
cmake --preset portable-debug
cmake --build --preset portable-debug -j2
ctest --preset portable-debug --output-on-failure -V
cmake --preset portable-release
cmake --build --preset portable-release -j2
ctest --preset portable-release --output-on-failure -V
```

| ID | Requirement | Status | Evidence / exact procedure | Exact closure snapshot? | Remaining limitation |
|---|---|---|---|---|---|
| B-01 | Canonical scene independent of D3D12 | PASS | Portable builds; source contamination scan. | Yes | None. |
| B-02 | Canonical ownership and stable identifiers | PASS | Owned arrays/strings/encoded and decoded images; typed IDs; tests. | Yes | IDs remain scene-local. |
| B-03 | glTF loading | PASS | Valid `.gltf` fixtures and validator. | Yes | Declared subset only. |
| B-04 | GLB loading | PASS | `minimal.glb`, `embedded_image.glb`, corrupt GLB rejection. | Yes | Declared subset only. |
| B-05 | External buffers/images | PASS | `external_scene.gltf`, dependency hashes, missing/traversal rejection. | Yes | Network resources prohibited. |
| B-06 | Embedded buffers/images | PASS | GLB BIN, buffer-view image, data URI, full decode tests. | Yes | PNG/JPEG only. |
| B-07 | Multiple scenes/roots/primitives | PASS | External fixture exact counts and scene selection. | Yes | None for declared subset. |
| B-08 | Hierarchy and transforms | PASS | Propagation/cycle/parent/negative-scale tests. | Yes | Interactive visual confirmation is B-20/B-23. |
| B-09 | Coordinate/unit/winding/tangent policy | PASS | Convention docs, explicit repair rules, tangent sign tests. | Yes | Runtime visual check blocked below. |
| B-10 | Accessor decoding and validation | PASS | Interleaving, normalized integers, bounds/range/index tests. | Yes | Sparse accessors rejected by design. |
| B-11 | Material/texture/sampler metadata | PASS | Invalid material sentinel resolves to glTF default; source indices stable; UV1 preparation tests; missing UV rejection. | Yes | Updated D3D12 visual run blocked under B-20. |
| B-12 | Camera and supported light metadata | PASS | Perspective/orthographic and directional punctual-light fixture. | Yes | Point/spot not required by current declared fixture subset. |
| B-13 | Bounds | PASS | Decoded bounds, stale min/max repair, multi-instance world bounds. | Yes | Visual bounds overlay update blocked under B-20. |
| B-14 | Import report | PASS | Deterministic JSON, resource counters, diagnostics, human summary. | Yes | None. |
| B-15 | Deterministic identity | PASS | Repeated/location/dependency/settings tests. | Yes | Persistent cache remains out of scope. |
| B-16 | Malformed-input handling | PASS | Full PNG/JPEG decode, resource budgets, vector/quaternion repair/rejection, 25 invalid fixtures. | Yes | Decoder backends differ by platform but share one contract; portable decoder source is system-supplied rather than vendored. |
| B-17 | Controlled fixture corpus | PASS | Generator reproducibility; 10 valid/degraded and 25 invalid assets; CC0. | Yes | None. |
| B-18 | Portable CPU tests | PASS | CTest: Core 9, Scene 7, Assets 17; GNU Debug/Release, Clang Release, and ASan+UBSan Debug. | Yes | GPU tests separate by design. |
| B-19 | GPU staging upload | BLOCKED | Existing source uses default heaps, explicit upload/copy/barriers/fences; historical pre-closure hardware/WARP runs passed. | No updated Windows run | Full image ownership changed; exact closure snapshot must be run on Windows. |
| B-20 | Diagnostic asset rendering | BLOCKED | UV1/tangent/instance preparation and HLSL paths implemented; portable numeric tests pass. | No Windows execution | Run all five modes on hardware and WARP. |
| B-21 | Orbit camera | NOT RUN | CPU finite/input/reset test passes. | No interactive run | Record orbit/pan/dolly/reframe behavior. |
| B-22 | Hierarchy/report inspection | PASS | Validator/application dump and JSON report paths. | Yes portable | Windows application path historically passed. |
| B-23 | Unload/reload and resize safety | BLOCKED | Deterministic stress CLI implemented using real renderer replacement and Win32 state changes. | No Windows execution | Run stress sequence on hardware and WARP. |
| B-24 | DirectX validation cleanliness | BLOCKED | Debug info queue and post-teardown D3D12 and DXGI live-object reports implemented without blanket suppression. | No Windows execution | Inspect exact-final-snapshot debugger output. |
| B-25 | Debug and Release builds | BLOCKED | Portable Debug/Release pass. Historical VS2026 Debug/Release passed before closure. | Windows final snapshot not run | Rebuild VS2022/VS2026 as available. |
| B-26 | CI | NOT RUN | Workflow source updated/retained. | No hosted run | Run GitHub Actions and retain links/logs. |
| B-27 | Documentation | PASS | README, architecture, subset, schema, controls, acceptance, audit, log, handoff, manifest updated. | Yes | Windows evidence rows remain honest. |
| B-28 | Source hygiene and reproducibility | PASS | Canonical Python packager uses explicit sorted/fixed-time entries; package generated twice with identical hash, extracted, rebuilt, retested, and scanned. | Yes | PowerShell companion was source-reviewed but not run; decoder libraries are documented system dependencies rather than vendored source. |
| B-29 | AI experiment evidence | PASS | Chronology and human Level-3 WARP intervention recorded. | Yes | No claim of current Windows execution. |
| B-30 | Adversarial audit and Campaign C handoff | PASS | Fresh F-01–F-10 hostile re-audit and corrected handoff included. | Yes | Campaign B final acceptance still waits on blocked Windows rows. |

## Current disposition

Semantic and portable audit closure is complete. Campaign B is **not declared finally accepted** because B-19, B-20, B-23, B-24, and B-25 require execution of the exact closure archive on Windows, B-21 requires interactive evidence, and B-26 has no hosted run.
