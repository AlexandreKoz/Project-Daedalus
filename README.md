# Project Daedalus

Project Daedalus is an AI-assisted C++20 / DirectX 12 rendering laboratory and reference asset viewer. This source snapshot implements the **Campaign C2 source architecture: Shadows, IBL, Image Regression, Performance Baselines, and Campaign C Closure tooling** on top of the validated C1 forward PBR foundation.

Campaign C is **not declared complete by this source archive alone**. The new C2 D3D12/shader/capture/timing paths still require the final Windows hardware/WARP, image-regression, NaN/Inf, benchmark, and live-object evidence listed in `docs/campaigns/campaign-c-acceptance.md`.

## Campaign C2 implementation

The renderer remains one conventional forward `PbrSceneRenderer`. C2 adds the remaining raster-validation infrastructure without introducing DXR, temporal accumulation, DLSS, an ECS, or a render graph.

Implemented source behavior includes:

- the C1 glTF metallic-roughness material/direct-lighting/HDR path and diagnostics;
- one deterministic 2048x2048 shadow map for the first directional or spot light, with bounds/cone-fitted projection, alpha-mask casters, explicit state transitions, 3x3 PCF, and configurable constant/normal bias;
- point lights remain valid direct lights but do not cast cubemap shadows in Campaign C;
- deterministic self-authored analytic IBL (`daedalus-procedural-sky-v1`) with diffuse irradiance, roughness-dependent specular environment response, split-sum BRDF approximation, configurable intensity, AO restricted to indirect lighting, and emissive kept separate;
- depth and additive overdraw diagnostics in the same production material path;
- deterministic fixed-frame back-buffer capture to PNG plus machine-readable metadata;
- optional pre-tone-map `R16G16B16A16_FLOAT` readback that counts NaN/Inf components and fails validation when any are present;
- portable linear-sRGB image comparison with MAE, RMSE, maximum error, fraction-over-threshold, visual difference PNG, and explicit reference-promotion policy;
- four self-authored/generated Campaign C reference scenes: Textured Cube, 5x5 Material Sphere Grid, PBR Production Asset, and Architectural Interior;
- D3D12 timestamp-query instrumentation for shadow/opaque/transparent/tone-map/total GPU time, separately labelled `steady_clock` CPU frame timing, renderer resource counts, canonical payload bytes, and committed-resource allocation estimates;
- fixed-frame JSON benchmark output and PowerShell capture/regression/reference-promotion workflows.

Campaign B/C1 contracts remain authoritative: canonical scene data is DirectX-independent; invalid material IDs mean the canonical default material rather than source material 0; texture colour space is slot-semantic; tangent sign/negative determinant behavior is preserved; and GPU lifetime remains fence-safe.

## Architecture

Campaign C retains the **conventional forward renderer** selected in C1. C2 adds bounded validation passes around it rather than replacing it.

```text
core
  ↑
scene          API-independent canonical scene/math
  ↑
assets         strict glTF/GLB import, image decode/encode, validation, reports
  ↑
rendering      portable PBR/C2 math + draw/light prep + camera + ABI contracts
  ↑
graphics       D3D12 shadow + HDR/depth + forward PBR + diagnostics + timing/readback
  ↑
Application    viewer lifecycle, capture/benchmark/reload/resize orchestration
```

Key documents:

- `docs/architecture/campaign-c1-raster-architecture.md`
- `docs/architecture/campaign-c2-raster-validation.md`
- `docs/architecture/pbr-material-and-lighting-contract.md`
- `docs/campaigns/campaign-c-acceptance.md`
- `docs/campaigns/campaign-c-adversarial-audit.md`
- `docs/handoffs/campaign-d-handoff.md`

## Supported raster subset and known limits

- maximum 32 punctual lights; excess is a reported failure, never silent truncation;
- one directional/spot shadow caster at a time; no point-light cubemap shadows or cascades;
- deterministic analytic environment rather than arbitrary HDR environment import;
- exactly one mip level is uploaded for canonical glTF textures and sampler LOD remains clamped to zero;
- normal maps require source tangents; no derivative fallback;
- transparent sorting is object/draw level;
- no clearcoat/transmission/specular material extensions;
- compact ACES fit is a display operator, not full ACES colour management;
- final C2 Windows/D3D12 evidence is still required before Campaign C exit.

## Portable prerequisites and validation

Portable builds exercise `core`, `scene`, `assets`, and the GPU-independent Campaign C rendering mathematics/preparation/contracts and image-comparison tool. They do not build the Win32/D3D12 application.

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

The controlled manifest currently contains **26 valid/degraded top-level fixtures and 25 invalid fixtures**. See `docs/assets/fixture-manifest.md`.

## Windows build

```powershell
cmake --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-debug --clean-first
ctest --preset windows-vs2026-debug --output-on-failure

cmake --preset windows-vs2026-release
cmake --build --preset windows-vs2026-release --clean-first
ctest --preset windows-vs2026-release --output-on-failure
```

The shader build compiles `RasterPbr.hlsl`, `Shadow.hlsl`, and `ToneMap.hlsl` through DXC. Debug shaders intentionally retain the Campaign B validated `-Zi -O3 -Qembed_debug` policy; the historical Windows SDK/WARP `-Od` crash is not silently reintroduced.

The delivery environment used for this C2 patch cannot execute Windows/MSVC/DXC/D3D12. New C2 runtime rows therefore remain `NOT RUN`/`BLOCKED`; source inspection is never promoted to GPU evidence. Exact closure commands are in `docs/campaigns/campaign-c-acceptance.md`.

## Viewer command line

```text
Daedalus.exe [--asset <path.gltf|path.glb>] [--scene <index-or-name>]
             [--diagnostic shaded|normals|uv|tangents|bounds|base-color|metallic|roughness|emissive|material-id|depth|overdraw]
             [--exposure <ev>] [--environment-intensity <0..64>]
             [--no-shadows] [--shadow-bias <0..0.05>] [--shadow-normal-bias <0..0.05>]
             [--capture <png>] [--capture-frame <n>] [--capture-metadata <json>] [--validate-hdr]
             [--benchmark-output <json>] [--benchmark-warmup <n>]
             [--warp] [--frames <count>] [--stress-reloads <count>]
             [--stress-alternate-asset <path>] [--stress-resize]
             [--report-live-objects] [--no-error-dialog]
```

Camera controls: left-drag orbit, right-drag pan, wheel dolly, `R` reframe, `F5` fence-safe reload. `scripts/run.ps1` exposes the same C2 switches.

Reference images are never changed by normal validation. After reviewing initial captures on the intended baseline machine, promote them explicitly with `scripts/update-campaign-c-references.ps1 -ConfirmReferenceUpdate`; later runs use `scripts/validate-campaign-c.ps1`.

## Source packaging

After generated build trees are removed, use the deterministic source-only workflow:

```bash
python3 scripts/package-source.py --output ../Project-Daedalus-Campaign-C2-Raster-source.zip
```

The archive workflow sorts entries under one `Project-Daedalus/` root, applies a fixed ZIP timestamp, and rejects build products, `.pdb/.obj/.exe/.dll/.lib/.dxil`, logs, IDE state, caches, nested archives, `.git`, obvious secret files, and restricted SDK material. `scripts/package-source.ps1` is the Windows companion.

Historical Campaign B evidence remains preserved. Campaign C2 does not retroactively rewrite its acceptance rows, and this snapshot does not claim Campaign C exit until the final Windows evidence matrix is satisfied.
