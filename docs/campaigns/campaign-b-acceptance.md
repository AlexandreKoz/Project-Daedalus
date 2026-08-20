# Campaign B acceptance matrix — audit-closure snapshot

Status vocabulary is limited to `PASS`, `FAIL`, `BLOCKED`, and `NOT RUN`.

This document separates source/portable evidence from developer-supplied Windows execution. On 2026-08-20 the developer ran the strict-JPEG closure predecessor under VS2026 on RTX 4060 Ti and explicit WARP, including Debug/Release CTest, all diagnostic modes, interactive camera use, and 100-cycle reload/resize stress. The final patch in this archive changes only DXGI live-object acquisition, shutdown idempotence, and PowerShell relative-output-path handling; those final Windows-only deltas still require a short exact-archive rerun.

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
| B-08 | Hierarchy and transforms | PASS | Propagation/cycle/parent/negative-scale tests plus Windows instance/bounds visual and stress evidence. | Yes portable + developer Windows predecessor | None for the declared subset. |
| B-09 | Coordinate/unit/winding/tangent policy | PASS | Convention docs, explicit repair rules, tangent sign tests. | Yes | Runtime visual check blocked below. |
| B-10 | Accessor decoding and validation | PASS | Interleaving, normalized integers, bounds/range/index tests. | Yes | Sparse accessors rejected by design. |
| B-11 | Material/texture/sampler metadata | PASS | Invalid material sentinel resolves to glTF default; source indices stable; UV1 preparation tests; missing UV rejection; UV diagnostic executed and captured on Windows. | Yes portable + developer Windows predecessor | Campaign C still owns production PBR correctness. |
| B-12 | Camera and supported light metadata | PASS | Perspective/orthographic and directional punctual-light fixture. | Yes | Point/spot not required by current declared fixture subset. |
| B-13 | Bounds | PASS | Decoded bounds, stale min/max repair, multi-instance world bounds, and Windows bounds overlay capture enclosing both instances. | Yes portable + developer Windows predecessor | None for the declared diagnostic subset. |
| B-14 | Import report | PASS | Deterministic JSON, resource counters, diagnostics, human summary. | Yes | None. |
| B-15 | Deterministic identity | PASS | Repeated/location/dependency/settings tests. | Yes | Persistent cache remains out of scope. |
| B-16 | Malformed-input handling | PASS | VS2026 Debug CTest passed 3/3 and direct `corrupt_entropy_jpeg.gltf --expect-failure` produced structured `invalid_image` with exit code 0 after the strict entropy repair. | Developer-tested predecessor; importer unchanged by this final patch | Progressive JPEG remains backend-decoded after marker validation; strict MCU walk is baseline sequential only. |
| B-17 | Controlled fixture corpus | PASS | Generator reproducibility; 10 valid/degraded and 25 invalid assets; CC0. | Yes | None. |
| B-18 | Portable CPU tests | PASS | CTest: Core 9, Scene 7, Assets 17; GNU Debug/Release, Clang Release, and ASan+UBSan Debug. | Yes | GPU tests separate by design. |
| B-19 | GPU staging upload | PASS | Imported textured and instanced fixtures rendered through the real D3D12 path on RTX 4060 Ti and explicit WARP after the image-ownership changes. | Developer-tested predecessor; upload/renderer code unchanged by final patch | Final B-24 teardown rerun remains separate. |
| B-20 | Diagnostic asset rendering | PASS | `shaded`, `normals`, `uv`, `tangents`, and `bounds` executed on hardware and WARP. Visual captures confirm UV variation, two-instance tangent response, and aggregate bounds placement. | Developer-tested predecessor; diagnostic renderer/shaders unchanged by final patch | Captures are external evidence, not embedded in the source ZIP. |
| B-21 | Orbit camera | PASS | Developer manually exercised orbit, pan, dolly and reframe while diagnostic scenes were running successfully. CPU finite/input/reset tests also pass. | Developer-tested predecessor; camera code unchanged by final patch | Manual observation is recorded evidence rather than machine-detected input. |
| B-22 | Hierarchy/report inspection | PASS | Validator/application dump and JSON report paths. | Yes portable | Windows application path historically passed. |
| B-23 | Unload/reload and resize safety | PASS | Hardware and WARP stress runs completed 100 alternating scene/renderer reloads plus small/large resize, minimize/restore and maximize/restore with clean process exit. | Developer-tested predecessor; stress sequence unchanged; final patch only makes shutdown idempotent | Final live-object inspection remains B-24. |
| B-24 | DirectX validation cleanliness | BLOCKED | Debug-layer runs were clean in the supplied transcript and D3D12 reporting executed, but DXGI reporting failed because `DXGIGetDebugInterface1` was searched in the wrong DLL. This patch calls the DXGI 1.3 API directly and prevents duplicate shutdown reporting. | Final fix not yet run on Windows | Rebuild the exact archive in Debug, run `--report-live-objects`, and inspect D3D12/DXGI debugger output for unexpected objects. |
| B-25 | Debug and Release builds | BLOCKED | Immediate predecessor passed VS2026 Debug and Release configure/build/DXC/CTest 3/3 plus hardware/WARP smoke. This final patch changes Windows-only `Application.cpp`/`D3D12Context.cpp`. | Final archive not yet rebuilt on Windows | One exact-final VS2026 Debug build is mandatory after the DXGI fix; Release predecessor evidence remains strong. VS2022 was not run. |
| B-26 | CI | NOT RUN | Workflow source updated/retained. | No hosted run | Run GitHub Actions and retain links/logs. |
| B-27 | Documentation | PASS | README/controls, acceptance, audit, agent log, handoff, Windows evidence record and packaging notes reflect the 2026-08-20 validation and final plumbing fixes without claiming the unrun final DXGI check. | Yes | Hosted CI remains separately NOT RUN. |
| B-28 | Source hygiene and reproducibility | PASS | Canonical Python packager uses explicit sorted/fixed-time entries; package generated twice with identical hash, extracted, rebuilt, retested, and scanned. | Yes | PowerShell companion was source-reviewed but not run; decoder libraries are documented system dependencies rather than vendored source. |
| B-29 | AI experiment evidence | PASS | Chronology records the Level-3 WARP shader intervention plus the 2026-08-20 Level-2 Windows malformed-image/runtime evidence and the final DXGI/path follow-up. | Yes | Final exact-snapshot Windows live-object rerun is still pending. |
| B-30 | Adversarial audit and Campaign C handoff | PASS | F-01–F-10 re-audit and Campaign C handoff were refreshed after the Windows runtime evidence; final blockers are explicitly limited to B-24/B-25 exact-snapshot closure and B-26 hosted CI. | Yes | No final Campaign B acceptance claim yet. |

## Current disposition

Semantic closure plus the major Windows runtime proof is now complete: malformed JPEG rejection, hardware/WARP asset rendering, five diagnostic modes, camera interaction, reload/resize stress, and predecessor Debug/Release builds have evidence. Campaign B is **not yet declared finally accepted** because the final DXGI/shutdown patch still needs an exact-archive VS2026 Debug build/live-object inspection (B-24/B-25), and hosted CI remains NOT RUN (B-26).
