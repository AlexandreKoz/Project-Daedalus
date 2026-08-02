# Project Daedalus

Project Daedalus is an AI-assisted C++20 rendering laboratory and reference asset viewer. This snapshot implements the portable and source-level portions of **Campaign B: Canonical Asset Pipeline** while preserving the Campaign A Win32/Direct3D 12 ownership model.

The canonical scene and importer are the Campaign B product. They compile without Windows or a GPU. The Windows viewer consumes only canonical scene data, stages immutable geometry and decoded textures into default-heap resources, and renders diagnostic shaded, normal, UV, and bounds views. It is not a Campaign C PBR renderer.

## Campaign status

Portable Campaign B implementation and validation are complete on this snapshot: GNU and Clang builds, CPU tests, fixture imports, malformed-input checks, deterministic-report checks, and clean archive revalidation pass. Windows/D3D12 compilation, shader execution, hardware/WARP smoke tests, interactive orbit/resize/reload checks, and debug-layer inspection were unavailable in the delivery environment and remain **BLOCKED** or **NOT RUN**. Campaign A's exact closure snapshot also still lacks its required Windows revalidation, so Campaign B is not declared finally accepted. See `docs/campaigns/campaign-b-acceptance.md`.

## Architecture

```text
core
  ↑
scene          canonical CPU-only types and math
  ↑
assets         glTF/GLB parse, validation, normalization, reports
  ↑
rendering      orbit-camera policy
  ↑
graphics       D3D12 staging, descriptors, depth, diagnostics
  ↑
Application    reference viewer lifecycle and command line
```

Rules enforced by source organization and tests:

- `src/scene` contains no D3D12, DXGI, COM, WIC, or importer-private type.
- The importer returns owned canonical arrays and strings; parser DOM and temporary buffers do not escape.
- The renderer consumes `CanonicalScene`, not JSON or glTF object graphs.
- GPU resources are not stored in the canonical scene.
- The application owns the window, device context, importer result, and renderer lifecycle.

## Supported Campaign B subset

- `.gltf` JSON and `.glb` 2.0 containers.
- External, data-URI, and GLB buffers.
- External, data-URI, and buffer-view PNG/JPEG image payloads; portable validation checks PNG chunk/CRC structure or JPEG marker/frame/scan structure and records source dimensions/components, while the Windows runtime decodes through WIC.
- Multiple scenes, roots, nodes, meshes, and triangle primitives.
- Matrix or TRS nodes, hierarchy propagation, negative scale, and recomputed bounds.
- Float and normalized integer vertex attributes, interleaved accessors, VEC3/VEC4 colours, 8/16/32-bit indices, and declared `KHR_mesh_quantization`.
- Positions, normals, tangents, `TEXCOORD_0/1`, and `COLOR_0` in the declared subset.
- Metallic-roughness material metadata, texture references, sampler modes, perspective/orthographic cameras, and `KHR_lights_punctual` metadata.
- Deterministic JSON import reports and content/settings/dependency-based asset keys.

Sparse accessors, non-triangle primitive modes, texture-transform extensions, animation, skins, morph targets, Draco/meshopt compression, KTX/Basis, production PBR, transparency rendering, and broad extension coverage are explicitly unsupported in Campaign B. The committed corpus contains 6 valid/degraded and 18 invalid top-level fixtures.

## Portable configure, build, and test

Prerequisites: CMake 3.25+, Ninja, and a C++20 compiler.

```bash
cmake --preset portable-debug
cmake --build --preset portable-debug
ctest --preset portable-debug

cmake --preset portable-release
cmake --build --preset portable-release
ctest --preset portable-release
```

Equivalent direct configuration:

```bash
cmake -S . -B build/portable-debug -G Ninja \
  -DDAEDALUS_BUILD_APP=OFF \
  -DDAEDALUS_BUILD_TESTS=ON \
  -DDAEDALUS_BUILD_TOOLS=ON \
  -DDAEDALUS_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build/portable-debug
ctest --test-dir build/portable-debug --output-on-failure
```

## Asset validator

```bash
build/portable-debug/DaedalusAssetValidator tests/assets/valid/external_scene.gltf \
  --dump-scene --report import-report.json --expect-success

build/portable-debug/DaedalusAssetValidator tests/assets/invalid/accessor_oob.gltf \
  --expect-failure
```

The validator exits `0` for a successful import, `1` for a normal rejected import, `2` for tool misuse/internal failure, and `3` when an explicit expectation is violated.

## Windows build

Use Windows 11 or a compatible Windows 10/11 x64 host with Visual Studio 2022/v143 or Visual Studio 2026/v145, a recent Windows SDK, CMake, and DXC.

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug

cmake --preset windows-vs2026-release
cmake --build --preset windows-vs2026-release
ctest --preset windows-vs2026-release
```

DXC discovery accepts `-DDXC_PATH=<dxc.exe-or-directory>` or the `DXC_PATH` environment variable. Project C++ and HLSL warnings are errors in the documented presets.

## Viewer command line

```text
Daedalus.exe --asset <path.gltf|path.glb>
             [--scene <index-or-name>]
             [--dump-scene]
             [--import-report <report.json>]
             [--diagnostic shaded|normals|uv|bounds]
             [--warp]
             [--frames <positive-count>]
```

With no `--asset`, the viewer uses a canonical built-in diagnostic triangle. A requested asset that fails import terminates with structured diagnostics; it never silently falls back.

Camera controls: left-drag orbits, right-drag pans, mouse wheel dollies, `R` reframes the selected scene bounds, and `F5` reloads the asset after a GPU-idle fence wait.

## Important documents

- `docs/architecture/campaign-b-architecture.md`
- `docs/architecture/canonical-scene-schema.md`
- `docs/assets/gltf-supported-subset.md`
- `docs/assets/coordinate-conventions.md`
- `docs/assets/import-report-schema.md`
- `docs/architecture/gpu-upload-lifetime.md`
- `docs/assets/fixture-manifest.md`
- `docs/campaigns/campaign-b-acceptance.md`
- `docs/campaigns/campaign-b-adversarial-audit.md`
- `docs/handoffs/campaign-c-handoff.md`

## Source packaging

After removing generated directories:

```powershell
.\scripts\clean.ps1
.\scripts\package-source.ps1
```

The packaging script rejects build trees, binaries, shader bytecode, logs, runtime output, IDE state, caches, nested archives, and restricted SDK files before creating the source-only ZIP.
