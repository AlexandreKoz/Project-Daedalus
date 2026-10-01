# Project Daedalus

Project Daedalus is an AI-assisted C++20 / DirectX 12 rendering laboratory and reference asset viewer. This source snapshot implements **Campaign C1: Raster PBR Core, Material Correctness, and Direct Lighting** on top of the audited Campaign B canonical scene.

Campaign C1 is deliberately **not** the end of Campaign C. It establishes the production forward raster/material/direct-lighting/HDR foundation. Shadows, image-based lighting, full screenshot/image-regression infrastructure, performance baselines, and final Campaign C acceptance remain Campaign C2 work.

## Campaign C1 implementation

The Windows viewer now has one production raster path, `PbrSceneRenderer`; the temporary Campaign B diagnostic renderer has been retired rather than grown into a second competing implementation.

Implemented C1 behavior includes:

- glTF metallic-roughness base colour, vertex colour, metallic/roughness, tangent-space normal, emissive, occlusion, alpha, and double-sided material semantics;
- slot-correct texture interpretation: base colour/emissive through sRGB SRVs, metallic-roughness/normal/occlusion through linear SRVs;
- explicit roughness **G** / metallic **B** channel mapping;
- source tangent `w`, negative world determinant, non-uniform-scale normal transformation, and back-face shading-frame handling;
- explicit no-tangent policy: normal-map evaluation is disabled and logged when the canonical primitive lacks source tangents;
- GGX/Trowbridge-Reitz + correlated Smith visibility + Schlick Fresnel metallic-roughness BRDF with dielectric F0 = 0.04 and documented finite roughness regularization;
- canonical `KHR_lights_punctual` directional, point, and spot lights, transformed from glTF local -Z, with inverse-square distance attenuation, smooth finite range, and spot-cone falloff;
- a hard, reported C1 limit of 32 punctual lights rather than silent truncation;
- `R16G16B16A16_FLOAT` linear HDR scene colour followed by explicit EV exposure, compact ACES-fit display mapping, and one linear-to-sRGB transfer into the UNORM swap chain;
- OPAQUE, MASK, and deterministic object-level back-to-front straight-alpha BLEND rendering;
- material-aware culling/double-sided behavior, including mirrored instances;
- production-path diagnostics for normals, UVs, tangents, bounds, base colour, metallic, roughness, emissive, and motion-independent material ID;
- stable material diagnostic IDs where canonical default material = 0 and source material 0 = 1;
- portable renderer-neutral material/light/draw preparation and numeric PBR reference tests;
- 12 new deterministic self-authored C1 material/light fixtures.

Campaign B contracts remain authoritative: an invalid primitive `MaterialId` is the canonical glTF default material and never source material 0; UV0/UV1 selection is explicit; images are canonical top-left RGBA8; instances share canonical geometry safely; and GPU resources remain outside `daedalus_scene`.

## Architecture

Campaign C1 chooses a **conventional forward renderer**. At the current repository scale a forward+ light-list subsystem or deferred G-buffer would add state/lifetime/ABI complexity without a demonstrated need and would complicate transparent materials. The bounded forward path is also a clean baseline for Campaign D hybrid DXR.

```text
core
  ↑
scene          API-independent canonical scene/math
  ↑
assets         strict glTF/GLB import, image decode, validation, reports
  ↑
rendering      portable PBR math + draw/light preparation + camera + ABI contracts
  ↑
graphics       D3D12 resources, descriptors, HDR/depth, forward pass, tone/output pass
  ↑
Application    viewer lifecycle, reload/resize/stress orchestration, command line
```

Key documents:

- `docs/architecture/campaign-c1-raster-architecture.md`
- `docs/architecture/pbr-material-and-lighting-contract.md`
- `docs/architecture/campaign-c1-shader-contract.md`
- `docs/campaigns/campaign-c1-acceptance.md`
- `docs/handoffs/campaign-c2-handoff.md`

## Supported raster subset

The importer continues to support the Campaign B glTF/GLB subset described in `docs/assets/gltf-supported-subset.md`. C1 consumes its core metallic-roughness material and punctual-light data through the canonical scene.

Important current raster limitations:

- no shadows or IBL yet;
- AO modulates only a small provisional indirect diffuse term, never punctual direct light or emissive output;
- exactly one mip level is uploaded for each canonical image; sampler LOD is clamped to level 0, so full glTF mip-filter fidelity is not claimed;
- normal maps require source tangents; no derivative fallback is implemented;
- transparent sorting is object/draw level and cannot solve every intersecting-transparency case;
- material extensions such as transmission/clearcoat/specular are not part of the declared C1 subset;
- the compact ACES fit is a display operator, not full ACES colour management.

## Portable prerequisites and validation

Portable builds exercise `core`, `scene`, `assets`, and the GPU-independent C1 rendering mathematics/preparation/contracts. They do not build the Win32/D3D12 application.

- CMake 3.25+
- Ninja
- C++20 compiler
- libpng and libjpeg development packages

```bash
cmake --preset portable-debug
cmake --build --preset portable-debug --clean-first
ctest --preset portable-debug --output-on-failure

cmake --preset portable-release
cmake --build --preset portable-release --clean-first
ctest --preset portable-release --output-on-failure

python3 scripts/source-health.py
python3 scripts/validate-fixture-manifest.py build/portable-debug/DaedalusAssetValidator
```

The controlled manifest currently contains **22 valid/degraded top-level fixtures and 25 invalid fixtures**. See `docs/assets/fixture-manifest.md`.

## Windows build

```powershell
cmake --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-debug --clean-first
ctest --preset windows-vs2026-debug --output-on-failure

cmake --preset windows-vs2026-release
cmake --build --preset windows-vs2026-release --clean-first
ctest --preset windows-vs2026-release --output-on-failure
```

The shader build compiles `RasterPbr.hlsl` and `ToneMap.hlsl` through DXC. Debug shaders intentionally retain the Campaign B validated `-Zi -O3 -Qembed_debug` policy; the historical Windows SDK/WARP `-Od` crash is not silently reintroduced.

The delivery environment used for this source snapshot cannot execute Windows/MSVC/DXC/D3D12 validation. The exact C1 Windows rows therefore remain `NOT RUN`/`BLOCKED` in `docs/campaigns/campaign-c1-acceptance.md` rather than being inferred from portable tests.

## Viewer command line

```text
Daedalus.exe [--asset <path.gltf|path.glb>]
             [--scene <index-or-name>]
             [--dump-scene]
             [--import-report <report.json>]
             [--diagnostic shaded|normals|uv|tangents|bounds|base-color|metallic|roughness|emissive|material-id]
             [--exposure <ev-from--24-to-24>]
             [--warp]
             [--frames <positive-count>]
             [--stress-reloads <positive-count>]
             [--stress-alternate-asset <path>]
             [--stress-resize]
             [--report-live-objects]
             [--no-error-dialog]
```

Camera controls: left-drag orbit, right-drag pan, wheel dolly, `R` reframe, `F5` fence-safe reload.

Representative C1 runs are documented in `docs/campaigns/campaign-c1-acceptance.md` and are also available through `scripts/run.ps1`, whose `-Diagnostic` and `-Exposure` parameters map to the same application options.

## Source packaging

After generated build trees are removed, use the deterministic source-only workflow:

```bash
python3 scripts/package-source.py --output ../Project-Daedalus-Campaign-C1-PBR-source.zip
```

The archive workflow sorts entries under one `Project-Daedalus/` root, applies a fixed ZIP timestamp, and rejects build products, `.pdb/.obj/.exe/.dll/.lib/.dxil`, logs, IDE state, caches, nested archives, `.git`, obvious secret files, and restricted SDK material. `scripts/package-source.ps1` is the Windows companion.

Historical Campaign B evidence is preserved as history. Campaign C1 does **not** rewrite Campaign B's exact-snapshot blocked/not-run rows, and it does **not** claim full Campaign C completion.
