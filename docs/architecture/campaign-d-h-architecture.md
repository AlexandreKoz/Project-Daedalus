# Campaigns D–H: architecture and contracts

Status: **source only, unverified.** Nothing described here has been compiled, shader-compiled, run, profiled or captured. Acceptance status lives in `docs/campaigns/campaign-d-h-acceptance.md`; the commands that would verify it are in `docs/handoffs/campaign-d-h-validation-commands.md`.

## Frame structure (`src/graphics/SceneRenderer.*`)

`SceneRenderer` is the single renderer facade used by `Application`. It owns `GpuSceneResources` (shared descriptor heap, textures, raw vertex/index buffers, samplers), the C2 raster renderer, the DXR scene, and the optional hybrid/path-tracing/temporal/upscaler stages.

| mode | pass order |
| --- | --- |
| `raster` (default, unchanged C2 output) | shadow → opaque (+G-buffer only when a temporal/upscaler consumer needs it) → transparent → [temporal debug] → [upscale] → tone map |
| `hybrid` | TLAS → shadow → opaque + G-buffer → `DispatchRays` (shadows, reflections) → [reflection denoise] → composite → transparent → [debug] → [upscale] → tone map |
| `pathtrace` | TLAS → `DispatchRays` (progressive) → [temporal denoise → remodulate] → [debug] → [upscale] → tone map |

Mode switches, resizes and reloads happen in `prepare_frame()`/`resize()` before `begin_frame`, after the GPU is proven idle. Recording a frame never frees resources. Keys: `1/2/3` raster/hybrid/pathtrace, `N` denoiser toggle, `H` history reset, `+/-` exposure.

## D — DXR foundation and hybrid effects

- **Capability:** `DxrDeviceSupport` requires `ID3D12Device5`, `ID3D12GraphicsCommandList4`, ray-tracing tier ≥ 1.0, shader model 6.3 and resource-binding tier 3 (unbounded tables). Failure keeps raster available and reports the reason; it never crashes.
- **Shaders:** `lib_6_3` DXIL libraries (`HybridRayTracing.dxil`, `PathTracing.dxil`) built with `-HV 2021 -WX`. State objects use one global root signature and no local root arguments. Payloads are ≤ 32 bytes, attributes 8 bytes, and recursion depth is 1 (all tracing happens in raygen).
- **Shader binding table:** 32-byte records, each section 64-byte aligned (`compute_shader_table_layout`). Ray types are radiance and visibility. Hit groups are closest-hit plus any-hit for alpha mask/blend.
- **Acceleration structures:**
  - One compacted BLAS per canonical primitive (postbuild-size readback).
  - One TLAS instance per raster draw, `InstanceID == draw index`. The TLAS is updated in place and rebuilt every 32 updates; instance descriptors go into per-frame-slot upload buffers.
  - Mask `0x01` covers opaque and masked instances, `0x02` blend. Double-sided and transmissive instances disable culling.
  - Facing is evaluated in object space, so negative-determinant instances need no flag.
- **Global root signature:** b0 view, b1 lights, b2 pass constants, t0 TLAS, t1–t4 instance/geometry/material/emissive tables, bindless `textures[]` (t0 space1), `buffers[]` (t0 space2) and `samplers[]` (s0 space1), 16 pass SRVs (t5–t20), 16 pass UAVs (u0–u15).
- **Hybrid:**
  - The raster pass writes the G-buffer: HDR, normal/roughness, albedo/metallic, position/depth, motion (RGBA16F: xy motion, z previous depth), and ids (instance+1, material+1, octahedral facet normal, flags).
  - For RT-eligible pixels, raster defers direct lighting and low-roughness specular.
  - Raygen re-shades direct lighting with the raster BRDF and real shadow rays. It traces one GGX VNDF reflection ray per pixel below `--rt-reflection-max-roughness`, demodulated by the split-sum weight.
  - The optional F denoiser filters reflections. A compute composite writes linear HDR, then transparents raster on top.
  - Debug views: `--rt-debug shadow-visibility|reflection|reflection-hit-distance|direct-lighting|instance-id`.

## E — Reference path tracer

- **Integrator:** an iterative path in raygen.
  - Next-event estimation picks one light per vertex among punctual (delta), emissive triangles (power-proportional alias table) and the analytic sky (sun-lobe/uniform mixture). Category probabilities come from the CPU.
  - Scope limit: the environment sampler is specific to the analytic sky. Image-based environment maps (import, CDF construction, importance sampling) are **not implemented**.
  - Next-event estimation is combined with BSDF sampling using the power heuristic. Russian roulette starts at `--pt-rr-depth`.
  - Non-finite samples are discarded and counted (`RtCounter`).
- **RNG:** `pcg4d(pixel, global sample index, seed)` followed by a PCG stream. The CPU mirror is in `PathTracingMath`.
- **BSDF (declared model):**
  - glTF metallic-roughness composition.
  - `KHR_materials_transmission` is factor-only. When `thickness > 0` it uses Walter rough refraction with exact Fresnel and Beer–Lambert absorption; otherwise it is thin-walled.
  - Alpha blend is stochastic coverage, not transmission.
- **Accumulation:** RGBA32F sums plus a sample count, giving an exact mean. A reset follows `accumulation_invalidating_reasons()`. Fixed-sample capture uses `--capture-samples N --capture-hdr out.pfm` (PFM, linear).
- **Presets:**
  - `reference` is unbiased: no clamp and no denoise, both enforced by the CLI.
  - `interactive` defaults to the denoiser plus a firefly clamp of 16, and is declared biased.
- **Validation scenes:** `--builtin-scene cornell-box|alpha-instancing` (procedural, no binary fixtures). Convergence utility: `DaedalusConvergence`.
- **Debug views:** `--pt-debug sample-count|variance|direct|indirect|path-length|material-id|nonfinite`.

## F — Temporal contract

- `GpuViewConstants` (704 bytes) carries jittered, unjittered and previous matrices. Halton(2,3)×16 jitter is applied only to the projection used for rasterization and primary rays.
- **Motion** is `previous_pixel − current_pixel` in render pixels and is jitter-free. It comes from the unjittered current/previous view-projection and the previous object transform. Fetch history at `(p + 0.5 + motion) / size`.
- **Resets:** `ResetReason` is a bitmask with three separate policies:
  - Accumulation is reset by any change.
  - Temporal history survives smooth camera/object motion.
  - Upscaler history is reset by cuts, resolution and settings changes.
  - Exposure invalidates none of them, because it is applied after all histories.
- **Denoiser:**
  - Reprojection uses per-tap rejection by bounds, instance ID, previous depth and normal, with an optional YCoCg clip. It tracks history length and moments, with spatial variance for short histories.
  - This is followed by N à-trous iterations with depth/normal/luminance stops.
  - Histories ping-pong by the denoiser's own parity, never by swap-chain slot.
- **Validation inputs:**
  - Scripted motion: `--camera-path orbit:<deg>|teleport:<frame>`, `--animate-node i:rotate-y|translate-x|bob-y:rate`, `--animate-emissive`.
  - Debug views: `--temporal-debug motion|depth|normals|disocclusion|history-length|variance|reactive|raw-signal`.

## G — Optional DLSS Super Resolution

- `DAEDALUS_ENABLE_DLSS` defaults to OFF. When ON, CMake requires a user-supplied glue `.cpp` (`DAEDALUS_DLSS_VENDOR_GLUE_SOURCE`), the SDK include directory and the SDK libraries; otherwise configuration fails with a BLOCKED message.
- The repository contains no SDK identifiers. The glue implements `create_dlss_vendor_runtime` and `DlssVendorRuntime` (see `src/graphics/dlss/DlssVendorRuntime.h` for colour/depth/motion/jitter/state conventions).
- Without the glue, `--upscaler dlss` reports BLOCKED in renderer status and capture metadata, and renders native. `--upscaler bilinear --render-scale s` is an explicit, non-temporal resample, labelled as such. Frame Generation and Ray Reconstruction are not exposed.

## H — Asset compatibility pipeline

- **`AssetLoader`** dispatches by extension:
  - `.gltf/.glb` go to the canonical importer (unchanged).
  - `.obj/.fbx/.3ds` go to `CompatibilityImporter`. This needs an installed Assimp at version `DAEDALUS_ASSIMP_MINIMUM_VERSION` or later and `-DDAEDALUS_ENABLE_ASSIMP=ON`; otherwise the result is `compatibility_importer_unavailable`.
  - `.blend` goes to `BlenderWorker`, then GLB, then the glTF importer.
- **Compatibility policy** (also recorded in report provenance):
  - Geometry is triangulated; points and lines are discarded, as are skinning, morphs and animation (each reported).
  - UVs are flipped to the top-left origin by Daedalus. Normals are generated when missing. Tangents are kept from the source when present, otherwise generated in glTF convention.
  - Node hierarchy and instancing are preserved.
  - Axes and units come from FBX `GlobalSettings` (`UnitScaleFactor` in cm). OBJ is assumed +Y up in metres; 3DS is taken as Assimp delivers it. `--up-axis`/`--metres-per-unit` overrides exist in the batch tool.
  - Phong maps to PBR as roughness `(2/(n+2))^¼` with metallic 0. Bump, separate metal/roughness and separate opacity maps are discarded with diagnostics.
- **Textures:** `TextureResolver` tries exact, then case-insensitive, then search roots, then basename. Authoring-machine absolute paths are reduced to their basename. URIs and escapes outside the asset directory or search roots are rejected; ambiguity is reported. Assimp file IO is confined to the same roots through a tracking IO system, which also records MTL dependencies.
- **Blender:**
  - Runs as `blender --background --factory-startup --python-exit-code 3 --python scripts/blender/daedalus_blend_export.py -- src out.glb manifest.json`.
  - Arguments are passed as a vector (no shell). Logs go to a file. Timeout and cancellation kill the whole process tree (a Job object on Windows, a process group on POSIX).
  - The script version `daedalus-blend-export/1` is part of the cache key. A missing Blender produces an explicit `external_converter_failed`.
- **Cache:** content-addressed `<sha256>.dcache`.
  - The key covers the format version, canonical/serialization versions, adapter identity, settings, search roots and source hash.
  - Dependencies are re-hashed on every hit.
  - Entries are validated by magic, version, size and SHA-256 checksum. Writes go to a temporary file followed by an atomic rename. Corrupt or stale entries are deleted and reported.
- **Batch:** `DaedalusAssetBatch convert|validate` writes a per-input report, a `.dscene` (convert only) and `batch-manifest.json` (`daedalus.asset-batch/1`).

## Known source-level hazards (unverified)

- **Command-list ordering:**
  - In-place TLAS updates and shared output UAVs rely on the in-order execution of one direct queue across frames. No cross-queue use exists.
  - An empty TLAS (scene with no draws) takes an explicit early-out path, but this is unproven on drivers.
- **Shader compilation and hardware:**
  - The new HLSL has never been through DXC. `-WX` with HLSL 2021 (vector `&&`/`||`/`?:` rules, implicit truncation) is the main compile risk.
  - Unbounded descriptor tables require resource-binding tier 3; tier-2 hardware falls back to raster.
- **Visual consistency:**
  - Hybrid direct lighting matches the raster BRDF, but the path tracer's diffuse/specular weighting differs slightly from the C1 raster approximation, so raster and pathtrace images are not expected to be identical.
  - The raster background is black, while the path tracer and the hybrid reflection misses show the analytic sky.
- **Asset import:**
  - FBX front-axis parity and Assimp spot-cone angle conventions differ between exporters (cones are treated as full angles). OBJ/3DS axis assumptions need fixture confirmation.
  - The cache hashes dependencies after import, which leaves a small TOCTOU window if files change during import. Blender identity in the key is path+size+mtime because the version is only known after running Blender.
