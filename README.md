# Project Daedalus

Project Daedalus is an AI-assisted C++20 / DirectX 12 rendering laboratory and reference asset viewer. It has a forward PBR raster baseline (Campaigns A–C). On top of that, it has DXR hybrid rendering, a progressive reference path tracer, temporal reprojection/denoising, an opt-in DLSS boundary and an extended asset pipeline (Campaigns D–H).

## Status — read this first

| Campaign | Scope | Source | Acceptance |
|---|---|---|---|
| A–B | Foundation, strict glTF/GLB importer | implemented | see `docs/campaigns/campaign-a-acceptance.md`, `campaign-b-acceptance.md` |
| C (C1 + C2) | Forward PBR, shadows, analytic IBL, capture/regression/benchmark tooling | implemented | final Windows evidence outstanding (`docs/campaigns/campaign-c-acceptance.md`) |
| D | DXR foundation, RT shadows + reflections (`hybrid`) | implemented, **unverified** | NOT RUN |
| E | Progressive reference path tracer (`pathtrace`) | implemented, **unverified** | NOT RUN |
| F | Temporal contract, motion vectors, temporal + à-trous denoiser | implemented, **unverified** | NOT RUN |
| G | DLSS Super Resolution boundary + native/bilinear fallback | boundary only; vendor glue **BLOCKED** | NOT RUN / BLOCKED |
| H | OBJ/FBX/3DS (Assimp), `.blend` (Blender worker), asset cache, batch tool | implemented, **unverified**; Assimp/Blender paths BLOCKED until installed | NOT RUN |

The D–H source was written under a no-build/no-run embargo. It has **never been compiled, shader-compiled, tested or run**, and no row is PASS. A follow-up static audit found and fixed a DXR descriptor-indexing bug, D-H-STATIC-001 (commit `886b947`).

Before trusting any D–H output, follow the order in `docs/handoffs/campaign-d-h-validation-commands.md`:
1. Build.
2. Re-verify raster against the C2 goldens. `PbrSceneRenderer` was substantially reworked to share GPU resources.
3. Run DXR under the debug layer with GPU-based validation.

The matrix is in `docs/campaigns/campaign-d-h-acceptance.md`.

## Renderer modes

`Application` owns the viewer. All modes go through one `SceneRenderer` facade over a shared `GpuSceneResources`, which provides one descriptor heap, one copy of textures and geometry, and stable instance/material IDs.

| mode | pass order |
|---|---|
| `raster` (default) | shadow → opaque (+G-buffer when needed) → transparent → [temporal debug] → [upscale] → tone map |
| `hybrid` | TLAS → shadow → opaque + G-buffer → `DispatchRays` (shadows, roughness-limited reflections) → [reflection denoise] → composite → transparent → … → tone map |
| `pathtrace` | TLAS → `DispatchRays` (progressive, RGBA32F accumulation) → [temporal denoise] → … → tone map |

- **DXR requirements:** `ID3D12Device5`, ray-tracing tier ≥ 1.0, shader model 6.3 and resource-binding tier 3. Otherwise the renderer reports why and stays in raster; WARP and unsupported GPUs keep working.
- **Runtime keys:** `1`/`2`/`3` switch raster/hybrid/pathtrace, `N` toggles the denoiser, `H` resets history, `+`/`-` adjust exposure. Camera controls are left-drag orbit, right-drag pan, wheel dolly, `R` reframe, and `F5` fence-safe reload.
- **Path-tracer presets:** `reference` is unbiased (no clamp, no denoise; the CLI enforces both). `interactive` uses a denoiser and a firefly clamp and is labelled biased in metadata.
- **Environment:** environment importance sampling covers only the analytic `daedalus-procedural-sky-v1`. Image-based environment maps are not supported.
- **DLSS:** never invented in-repo. See *Optional dependencies* below.

Architecture, shader ABIs, temporal units and known hazards are documented in `docs/architecture/campaign-d-h-architecture.md`.

## Source layout

```text
core        CLI, JSON, SHA-256, render settings
  ↑
scene       API-independent canonical scene + math (no D3D12/Assimp types)
  ↑
assets      glTF/GLB importer, AssetLoader, compatibility importer (Assimp), Blender worker,
            texture resolver, content-addressed cache, scene serialization, reports, PNG/PFM I/O
  ↑
rendering   portable math/contracts: raster prep, DXR tables + SBT layout, path-tracing math,
            temporal math, reset reasons, convergence analysis (all CPU-testable)
  ↑
graphics    D3D12: context, shared scene resources, raster PBR, raytracing/, temporal/, dlss/,
            compute passes, display output, GPU timers
  ↑
Application viewer lifecycle, mode switching, capture/benchmark/reload/resize orchestration
```

Shaders live in `shaders/`:
- `common/` — the PBR, environment, sampling and frame contracts;
- the `RasterPbr`/`Shadow`/`ToneMap` raster shaders;
- `raytracing/` — `lib_6_3` DXR libraries;
- `compute/` — `cs_6_0` composite, reproject, à-trous and debug passes.

## Building

### Portable (CPU-only: core/scene/assets/rendering + tools + tests)

Requires CMake 3.25+, Ninja, a C++20 compiler, and libpng/libjpeg development packages.

```bash
cmake --preset portable-debug
cmake --build --preset portable-debug
ctest --preset portable-debug --output-on-failure

python3 scripts/source-health.py
python3 scripts/validate-fixture-manifest.py build/portable-debug/DaedalusAssetValidator
```

### Windows (application + shaders)

```powershell
cmake --preset windows-msvc-debug        # or windows-vs2026-debug; DXC via Windows SDK or -DDXC_PATH=<dxc.exe>
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```

DXC compiles all HLSL with `-HV 2021 -WX`. Debug keeps the validated `-Zi -O3 -Qembed_debug` policy. All `.dxil` files and `scripts/blender/daedalus_blend_export.py` are copied next to `Daedalus.exe`.

### Optional dependencies (all default OFF, never downloaded)

| option | effect |
|---|---|
| `-DDAEDALUS_ENABLE_ASSIMP=ON` | OBJ/FBX/3DS import through an installed Assimp ≥ `DAEDALUS_ASSIMP_MINIMUM_VERSION` (default 5.3), located with `find_package(assimp CONFIG)`. When OFF, these formats report `compatibility_importer_unavailable`. |
| `-DDAEDALUS_ENABLE_DLSS=ON` | Requires `DAEDALUS_DLSS_VENDOR_GLUE_SOURCE` (your `.cpp` implementing `create_dlss_vendor_runtime` against a locally installed, licensed SDK), plus `DAEDALUS_DLSS_INCLUDE_DIRECTORY`, `DAEDALUS_DLSS_LIBRARIES` and optionally `DAEDALUS_DLSS_RUNTIME_FILES`. Configuration fails with a BLOCKED message if any are missing. Without DLSS, `--upscaler dlss` reports BLOCKED and renders native. |
| Blender (runtime, not a build option) | `.blend` files are converted by a separately installed Blender (`--blender <path>` or `PATH`). Daedalus never parses `.blend` itself. |

## Viewer command line

```text
Daedalus.exe [--asset <file> | --builtin-scene triangle|cornell-box|alpha-instancing] [--scene <index-or-name>]
  Renderer   [--renderer raster|hybrid|pathtrace]
             [--rt-shadows on|off] [--rt-reflections on|off] [--rt-denoise on|off]
             [--rt-reflection-max-roughness <r>] [--rt-debug <view>]
  Path trace [--pt-preset reference|interactive] [--pt-spp <n>] [--pt-max-depth <n>] [--pt-rr-depth <n>]
             [--pt-seed <n>] [--pt-target-samples <n>] [--pt-clamp <v>] [--pt-denoise on|off] [--pt-debug <view>]
  Temporal   [--jitter on|off] [--history-length <n>] [--history-clip <gamma>] [--atrous-iterations <n>]
             [--temporal-debug <view>] [--camera-path static|orbit:<deg>|teleport:<frame>]
             [--animate-node <i>:rotate-y|translate-x|bob-y:<rate>] [--animate-emissive <frames>]
  Upscaling  [--upscaler none|bilinear|dlss] [--render-scale <0.25..1>]
             [--dlss-quality dlaa|quality|balanced|performance|ultra-performance]
  Assets     [--asset-cache <dir>] [--texture-search-root <dir>]... [--blender <exe>] [--blender-timeout <s>]
  Raster/C2  [--diagnostic shaded|normals|uv|tangents|bounds|base-color|metallic|roughness|emissive|material-id|depth|overdraw]
             [--exposure <ev>] [--environment-intensity <0..64>]
             [--no-shadows] [--shadow-bias <v>] [--shadow-normal-bias <v>]
  Capture    [--capture <png>] [--capture-frame <n>] [--capture-samples <n>] [--capture-hdr <pfm>]
             [--capture-metadata <json>] [--validate-hdr] [--benchmark-output <json>] [--benchmark-warmup <n>]
  Runtime    [--warp] [--frames <n>] [--stress-reloads <n>] [--stress-alternate-asset <file>] [--stress-resize]
             [--report-live-objects] [--no-error-dialog]
```

Debug views:
- `--rt-debug`: `shadow-visibility`, `reflection`, `reflection-hit-distance`, `direct-lighting`, `instance-id`.
- `--pt-debug`: `sample-count`, `variance`, `direct`, `indirect`, `path-length`, `material-id`, `nonfinite`.
- `--temporal-debug`: `motion`, `depth`, `normals`, `disocclusion`, `history-length`, `variance`, `reactive`, `raw-signal`.

Raster `--diagnostic` modes other than `shaded` are raster-only and are rejected with `hybrid`/`pathtrace`.

`scripts/run.ps1` mirrors the main switches: `-Renderer`, `-BuiltinScene`, `-CaptureHdr`, `-CaptureSamples`, `-Upscaler`, `-AssetCache`, `-TextureSearchRoot` and `-Blender`. It passes anything else through `-RendererArguments`.

Capture metadata (`daedalus.capture-metadata/2`) and benchmark JSON (`daedalus.benchmark/2`) record:
- renderer mode;
- device capability and reasons;
- reset reasons;
- path-tracer counters;
- upscaler status, including BLOCKED fallbacks;
- per-pass GPU timings.

## Tools

| tool | purpose |
|---|---|
| `DaedalusAssetValidator` | strict glTF/GLB validation with JSON import report |
| `DaedalusAssetBatch convert\|validate <inputs>... --output-dir <dir>` | batch import through the same `AssetLoader` as the viewer; writes reports, `.dscene` and `batch-manifest.json`; accepts `--cache`, `--texture-search-root`, `--up-axis`, `--metres-per-unit`, `--blender`, `--blender-timeout` |
| `DaedalusImageCompare` | linear-sRGB image comparison + difference PNG |
| `DaedalusConvergence --reference <pfm> --sample <spp>=<pfm>...` | path-tracer error metrics and log-log RMSE slope (measurements only) |

## Asset pipeline (Campaign H)

- **Dispatch:**
  - `.gltf/.glb` go to the canonical importer.
  - `.obj/.fbx/.3ds` go to the compatibility importer.
  - `.blend` goes to Blender, then GLB, then the canonical importer.
- **Reports:** every import report (`daedalus.import-report/2`) carries `provenance`: adapter and converter versions, axis/unit policy, and cache disposition. See `docs/assets/import-report-schema.md`.
- **Textures:** the resolver tries exact → case-insensitive → search roots → basename, in that order. Authoring-machine absolute paths are never opened. URIs and escapes outside the allowed roots are rejected, and ambiguity is reported.
- **Cache:** content-addressed `.dcache` files.
  - Entries carry a checksum, are written atomically, and have their dependencies re-hashed on every hit.
  - Corrupt or stale entries are deleted and reported.

## Known limits

- **Raster:** at most 32 punctual lights; one directional/spot shadow caster; no point-light shadows or cascades; one mip level per texture; normal maps require source tangents; object-level transparency sorting.
- **Materials:** transmission/IOR/volume/emissive-strength are factor-only and path-tracer only (textures ignored and reported). Clearcoat, sheen, specular, iridescence, anisotropy, texture transform, Draco, meshopt, BasisU and sparse accessors are unsupported.
- **Path tracer:** analytic sky only (no HDR environment maps). The raster background is black while the path tracer shows the sky, so raster and path-traced images are not expected to match.
- **Temporal:** no skinned motion (no skinning). Exposure intentionally invalidates no history.
- **DLSS:** Super Resolution boundary only. Frame Generation and Ray Reconstruction are not exposed.
- **Compatibility import:** FBX axis parity and spot-cone conventions vary by exporter, and OBJ/3DS axis assumptions need confirmation with real files. Skinning, morphs and animation are discarded with diagnostics.

## Fixtures

`tests/assets/generate_fixtures.py` produces all self-authored CC0 fixtures:
- 26 valid/degraded and 25 invalid glTF fixtures in the manifest;
- the Campaign H `compat/` set.

`compat/quad.obj` and `compat/quad.mtl` are committed as text. `compat/textures/checker.png` and `compat/quad_equivalent.gltf` only exist after the generator runs. The OBJ/glTF equivalence test is built only with `DAEDALUS_ENABLE_ASSIMP=ON` and those files present.

## Source packaging

```bash
python3 scripts/package-source.py --output ../Project-Daedalus-source.zip
```

The archive is deterministic (sorted entries, fixed timestamps, one `Project-Daedalus/` root). It rejects build products (`.pdb/.obj/.exe/.dll/.lib/.dxil`), logs, caches, nested archives, `.git`, secrets and restricted SDK material. The one exception is the text OBJ fixtures in `tests/assets/compat/`. `scripts/package-source.ps1` is the Windows companion.

Historical Campaign A–C evidence is preserved as recorded; later campaigns never rewrite earlier acceptance rows.
