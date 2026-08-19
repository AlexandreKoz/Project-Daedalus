# Campaign B audit-closure adversarial re-audit

**Date:** 2026-08-07  
**Baseline archive:** `Project-Daedalus-main(3)(1).zip`  
**Baseline SHA-256:** `5397703f3cba8f4649f46f1ed2844a14f82198a9888dde2b34303bdf2348da32`  
**Suggested branch/PR:** `fix/campaign-b-audit-closure`

## Verdict

All original source-level semantic findings F-01 through F-07 and process/package findings F-09/F-10 are repaired and covered by new tests or deterministic delivery evidence. F-08 now has a focused real-runtime stress path and post-teardown live-object reporting implementation, but execution on the exact closure snapshot is blocked in the Linux delivery environment. Campaign B therefore receives semantic/portable closure, not unconditional final acceptance.

## Finding-by-finding disposition

### F-01 — omitted material semantics: CLOSED

`Primitive::material` remains invalid when glTF omits `material`; `CanonicalScene::default_material` represents the normative default. Source material indices are not shifted. `material_default.gltf` and runtime-neutral draw-preparation assertions prove explicit material 0 and omitted default remain distinct.

### F-02 — `textureInfo.texCoord`: CLOSED

All supported texture references retain set 0/1. Primitive/material validation rejects missing referenced sets with `missing_texture_coordinate`. `PreparedDiagnosticDraw::texture_coord_set` reaches `DrawConstants`, and HLSL selects `uv0`/`uv1`. `uv1_scene.gltf` makes accidental UV0 selection numerically and visually obvious.

### F-03 — full image decode boundary: CLOSED

`assets/ImageDecoder` performs full supported PNG/JPEG decode before import success. Canonical images own RGBA8 pixels and row stride. The D3D12 renderer no longer invokes WIC or hides decode failure. `corrupt_entropy_png.gltf` and `corrupt_entropy_jpeg.gltf` pass superficial container/marker inspection but are rejected by the real decoder as `invalid_image`.

### F-04 — resource accounting: CLOSED

Checked cumulative retained and conservative-peak budgets cover source, buffers, encoded images, canonical vertices/indices, decoded pixels, and decode/accessor scratch estimates. Boundary-equal and one-byte-over tests assert `resource_budget_exceeded` without large committed files.

### F-05 — silent normalization: CLOSED

Non-finite/zero/grossly non-unit normals, tangent XYZ, and quaternions are rejected. Small deviations are normalized only within documented tolerance and emit `attribute_normalized` or `rotation_normalized`. Tangent `w` is preserved and validated.

### F-06 — tangent usability: CLOSED IN SOURCE; WINDOWS EVIDENCE BLOCKED

`--diagnostic tangents` reaches parser, renderer, and HLSL; tangent direction and signed handedness are encoded. Portable tests prove source values, draw preparation, and CLI. Exact updated GPU execution remains blocked.

### F-07 — multiple instances: CLOSED IN SOURCE; WINDOWS EVIDENCE BLOCKED

`instanced_tangents.gltf` stores one mesh/primitive referenced by two nodes with distinct transforms and one negative determinant. Draw count, shared primitive ID, matrices, handedness, and world bounds are asserted. Updated visual run remains blocked.

### F-08 — lifetime and DirectX closure: IMPLEMENTED; EXECUTION BLOCKED

`--stress-reloads`, `--stress-alternate-asset`, and `--stress-resize` exercise real scene destruction/recreation, uploads, descriptors, fences, and swap-chain resize events. `--report-live-objects` reports DXGI objects after application graphics resources are released. No Windows execution is claimed by this agent.

### F-09 — stale evidence: CLOSED

Documentation preserves chronology: original WARP failure, debugger location, isolation matrix, `-Od` root trigger, `-Zi -O3` repair, reduced unoptimized-shader debugging fidelity, historical developer-supplied Windows runs, and current blocked exact-snapshot checks. The intervention is recorded as human-guided Level 3.

### F-10 — deterministic packaging: CLOSED

The canonical Python packager enumerates files in UTF-8 lexical order, rejects prohibited paths, creates entries explicitly under `Project-Daedalus/`, fixes every timestamp to 2000-01-01 UTC, strips extra/comment metadata, and verifies CRC/order/root/timestamps. Delivery packaging was independently generated twice with identical bytes, extracted, built, tested, and scanned. The PowerShell companion implements the same policy but was source-reviewed rather than executed here.

## Hostile checks performed

- searched canonical headers for D3D12/DXGI/COM/WIC/importer leakage;
- searched renderer for JSON/glTF parser objects and renderer-time image decode;
- searched omitted-material assignments and UV0-only sampling;
- exercised real corrupt image entropy and all invalid fixtures under GNU, Clang, and ASan/UBSan builds;
- exercised cumulative/peak boundary limits and overflow-safe arithmetic paths;
- inspected normalization decisions and diagnostic ordering;
- proved one mesh creates two node-instance draws;
- inspected upload heap/fence and renderer replacement ownership;
- searched for `-Od`, broad debug-message suppression, silent WARP fallback, local absolute paths, and prohibited archive artifacts;
- regenerated fixtures twice and compared hashes;
- generated the final ZIP twice and compared hashes;
- extracted and revalidated the archive portably.

## Remaining limitations and blockers

Portable PNG/JPEG decoding uses separately installed libpng/libjpeg-turbo rather than vendored byte-pinned decoder source; exact versions used by this delivery environment are recorded in `THIRD_PARTY_NOTICES.md`. The exact final archive still needs Windows VS2022/VS2026 build/test as available, hardware/WARP diagnostic runs, automated stress execution, interactive camera checks, debugger/live-object inspection, and hosted CI. These are not converted into PASS by source review.


## Post-closure Windows hardening follow-up (2026-08-08)

Developer-side VS2026 testing of a later closure snapshot exposed two source-level Windows build defects: missing effective `NOMINMAX` protection for the WIC-backed asset decoder and an MSVC C4324 warning-as-error caused by implicit tail padding in the diagnostic constant-buffer mirror. The subsequent Windows-hardening pass also fixed acceptance-wrapper drift, modal error-dialog blocking in unattended runs, unbounded D3D12 fence waits, and orphaned dead Windows implementations. A portable CPU/HLSL ABI contract and `scripts/source-health.py` were added so these classes of regression are caught earlier. Exact repaired Windows execution remains developer-side evidence and is not inferred from portable tests.
