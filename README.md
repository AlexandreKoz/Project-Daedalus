# Project Daedalus

Project Daedalus is an AI-assisted C++20 rendering laboratory and reference asset viewer. This source snapshot closes the audited semantic and evidence defects in **Campaign B: Canonical Asset Pipeline** without beginning Campaign C.

The Campaign B product is the portable canonical scene plus the strict glTF/GLB importer. The Windows application consumes that canonical representation, uploads immutable geometry and validated RGBA8 images through explicit D3D12 staging, and renders diagnostic views. It does not claim production PBR.

## Audit-closure status

The remediation snapshot closes the source-level findings F-01 through F-10 from the 2026-08-02 adversarial audit:

- omitted primitive materials now use an explicit canonical glTF default material sentinel without shifting source material indices;
- `textureInfo.texCoord` 0/1 is validated and reaches runtime draw preparation and HLSL selection;
- supported PNG/JPEG images are fully decoded before import success, producing owned RGBA8 canonical pixels;
- retained and conservative-peak resource budgets cover source, buffers, encoded images, canonical geometry, decoded images, and decode scratch;
- slightly non-unit normals, tangents, and quaternions are reported repairs; zero or grossly invalid values are rejected;
- `--diagnostic tangents` visualizes tangent direction and handedness;
- controlled fixtures prove default materials, UV1, one-mesh/multiple-node instances, negative scale, decode failure, budgets, and vector repair/rejection;
- focused runtime stress and post-teardown DXGI live-object reporting options are implemented;
- evidence documents preserve the original Debug WARP `-Od` crash and the validated `-Zi -O3` policy;
- source packaging explicitly sorts entries, fixes timestamps, enforces one `Project-Daedalus/` root, and rejects prohibited artifacts.

Portable Debug and Release builds, all portable tests, the expanded controlled corpus, deterministic reports, fixture regeneration, and final archive re-extraction are validated in the delivery environment. Updated Windows/D3D12 runtime behavior is **not inferred** from portable validation: exact-final-snapshot Windows tests remain `NOT RUN` here and commands are documented for the developer.

## Architecture

```text
core
  ↑
scene          API-independent canonical types and math
  ↑
assets         glTF/GLB parsing, full image decode, validation, budgets, reports
  ↑
rendering      renderer-neutral draw preparation and orbit camera
  ↑
graphics       D3D12 staging, descriptors, depth, diagnostics
  ↑
Application    reference viewer lifecycle, stress orchestration, command line
```

Rules:

- `src/scene` contains no D3D12, DXGI, COM, WIC, or importer-private type.
- Import results own all strings, geometry, encoded bytes, and decoded RGBA8 pixels.
- The renderer consumes `CanonicalScene`; parser/JSON objects do not escape the importer.
- GPU resources are absent from canonical structures.
- An invalid `MaterialId` on a primitive means the glTF default material; source material indices remain unchanged.
- Full image decode validity belongs to the assets layer, not to renderer construction.

## Supported subset

- glTF 2.0 `.gltf` and `.glb`.
- External, data-URI, and GLB buffers.
- External, data-URI, and buffer-view PNG/JPEG images.
- Full PNG/JPEG decode to top-left-origin, tightly packed RGBA8 before import success.
- Multiple scenes, roots, nodes, meshes, primitives, and node instances of one mesh.
- Matrix/TRS hierarchy, negative scale, deterministic world propagation and recomputed bounds.
- Positions, normals, tangents with sign, `TEXCOORD_0/1`, colours, interleaving, normalized integer attributes, and 8/16/32-bit indices.
- Metallic-roughness metadata, texture/sampler metadata, perspective/orthographic cameras, and declared `KHR_lights_punctual` subset.
- Deterministic JSON reports, dependency hashes, settings-sensitive asset keys, and resource-use counters.

Sparse accessors, non-triangle topology, `KHR_texture_transform`, animation, skins, morph targets, compression extensions, KTX/Basis, production transparency/PBR, IBL, shadows, DXR, and broad extension coverage are out of scope.

## Portable prerequisites

- CMake 3.25+
- Ninja
- C++20 compiler
- libpng and libjpeg development packages

```bash
cmake --preset portable-debug
cmake --build --preset portable-debug
ctest --preset portable-debug --output-on-failure

cmake --preset portable-release
cmake --build --preset portable-release
ctest --preset portable-release --output-on-failure
```

Windows uses Windows Imaging Component behind the same `assets/ImageDecoder` contract; non-Windows portable builds use separately installed libpng/libjpeg. No image-decoder source is vendored, so the exact decoder implementations remain a documented build-environment dependency rather than part of the source ZIP.

## Asset validator

```bash
build/portable-debug/DaedalusAssetValidator \
  tests/assets/valid/uv1_scene.gltf --dump-scene --report report.json --expect-success

build/portable-debug/DaedalusAssetValidator \
  tests/assets/invalid/corrupt_entropy_png.gltf --expect-failure
./build/portable-debug/DaedalusAssetValidator \
  tests/assets/invalid/corrupt_entropy_jpeg.gltf --expect-failure
```

Exit codes: `0` accepted/expected behavior, `1` normal rejected import, `2` misuse/internal tool failure, `3` expectation mismatch.

## Windows build

```powershell
cmake --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-debug
ctest --preset windows-vs2026-debug --output-on-failure

cmake --preset windows-vs2026-release
cmake --build --preset windows-vs2026-release
ctest --preset windows-vs2026-release --output-on-failure
```

Debug diagnostic HLSL intentionally uses `-Zi -O3 -Qembed_debug`. `-Od` is not used because Windows SDK 10.0.26100 WARP reproducibly crashed inside `d3d10warp.dll` during graphics PSO creation for either unoptimized shader stage.

## Viewer command line

```text
Daedalus.exe [--asset <path.gltf|path.glb>]
             [--scene <index-or-name>]
             [--dump-scene]
             [--import-report <report.json>]
             [--diagnostic shaded|normals|uv|tangents|bounds]
             [--warp]
             [--frames <positive-count>]
             [--stress-reloads <positive-count>]
             [--stress-alternate-asset <path>]
             [--stress-resize]
             [--report-live-objects]
```

A failed requested asset never silently falls back to the built-in triangle. Stress reloads wait for GPU idle, destroy the renderer, reload the real canonical scene, recreate uploads/descriptors, and may alternate two assets. `--stress-resize` exercises resize/minimize/restore/maximize states. `--report-live-objects` requests a Debug post-teardown DXGI report.

Camera controls: left-drag orbit, right-drag pan, wheel dolly, `R` reframe, `F5` reload.

## Source packaging

After removing generated directories, the canonical delivery command is:

```bash
python3 scripts/package-source.py --output ../Project-Daedalus-Campaign-B-audit-closure-source.zip
```

The Windows companion is `scripts/package-source.ps1`. The canonical Python packaging script creates sorted entries beneath `Project-Daedalus/`, applies a fixed `2000-01-01T00:00:00Z` ZIP timestamp, and rejects build trees, binaries, DXIL/PDBs, logs, IDE state, caches, nested archives, secrets, and restricted SDK material.

See `docs/campaigns/campaign-b-acceptance.md`, `docs/campaigns/campaign-b-adversarial-audit.md`, and `docs/handoffs/campaign-c-handoff.md` for exact evidence and limitations.
