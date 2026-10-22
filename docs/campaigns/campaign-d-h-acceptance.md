# Campaigns D–H acceptance matrix

Allowed statuses: `PASS`, `FAIL`, `BLOCKED`, `NOT RUN`. **No row is PASS.**

The D–H work was delivered as source under an explicit no-build/no-run embargo. No compiler, DXC, CMake, CTest, fixture generator, Blender, Assimp, SDK or application was executed, so every row that depends on execution is `NOT RUN`. `BLOCKED` means a required external input is missing, independent of the embargo. Historical Campaign C rows are unchanged.

"Source" names where the implementation lives; it is not evidence that it works.

## D — DXR foundation and hybrid effects

| ID | Requirement | Status | Source / what is missing |
|---|---|---|---|
| D-01 | Capability query with graceful raster fallback | NOT RUN | `raytracing/DxrDeviceSupport.*`; needs hardware + WARP runs |
| D-02 | `lib_6_3` DXC build rules | NOT RUN | `cmake/CompileShaders.cmake` `daedalus_compile_shader`; never compiled |
| D-03 | State objects + global root signature | NOT RUN | `RaytracingPipeline.*`, `RaytracingScene.*` |
| D-04 | Correct shader binding table | NOT RUN | `compute_shader_table_layout` + portable test source (not executed) |
| D-05 | BLAS (compacted) / TLAS (update + periodic rebuild) | NOT RUN | `AccelerationStructureManager.*` |
| D-06 | Alpha-mask any-hit, blend coverage, instance masks | NOT RUN | `HybridRayTracing.hlsl`, `RayTracingCommon.hlsli` |
| D-07 | RT shadows composited into linear HDR | NOT RUN | `HybridRayTracer.*`, `HybridComposite.hlsl` |
| D-08 | RT reflections (roughness-limited, demodulated) | NOT RUN | same |
| D-09 | Hybrid mode + debug views | NOT RUN | `SceneRenderer.*`, `--rt-debug` |
| D-10 | GPU-based validation clean | NOT RUN | Requires a D3D12 debug-layer + GBV session |
| D-11 | 50–100 reloads/resizes/mode switches without leaks | NOT RUN | `--stress-reloads`, `--stress-resize`, `--report-live-objects` |

## E — Reference path tracer

| ID | Requirement | Status | Source / what is missing |
|---|---|---|---|
| E-01 | Deterministic RNG (pixel, sample, seed) | NOT RUN | `PathTracingMath`, `PathTracing.hlsl`; CPU test source not executed |
| E-02 | Multi-bounce GGX + diffuse BSDF | NOT RUN | `PathTracingBsdf.hlsli` |
| E-03 | Declared transmission model | NOT RUN | Factor-only `KHR_materials_transmission/ior/volume`; transmission textures are reported as ignored |
| E-04 | NEE + MIS (punctual, emissive alias table, environment) | NOT RUN | `PathTracing.hlsl`, `prepare_ray_tracing_tables`. Environment importance sampling covers ONLY the analytic `daedalus-procedural-sky-v1` (sun-lobe/uniform mixture); image-based environment maps are neither imported nor importance-sampled (narrower than the charter objective; reported as `image_environment_maps: unsupported` in renderer status) |
| E-05 | Russian roulette, numeric guards, counters | NOT RUN | `RayTracingCounters.*` |
| E-06 | RGBA32F progressive accumulation + exact reset policy | NOT RUN | `ReferencePathTracer.*`, `FrameRevision.*` |
| E-07 | Fixed-sample capture (PNG + linear PFM + metadata) | NOT RUN | `--capture-samples`, `--capture-hdr` |
| E-08 | Convergence at 1/4/16/64/256 spp (slope ≈ −0.5) | NOT RUN | `DaedalusConvergence`; no captures exist |
| E-09 | Cornell box / material validation scenes | NOT RUN | `make_builtin_cornell_box_scene`, `make_builtin_alpha_instancing_scene` |
| E-10 | Path-tracer debug views | NOT RUN | `--pt-debug` |

## F — Temporal contract

| ID | Requirement | Status | Source / what is missing |
|---|---|---|---|
| F-01 | Jittered/unjittered/previous matrices (704-byte ABI) | NOT RUN | `TemporalContract.h`, `FrameContract.hlsli` |
| F-02 | Jitter-free motion vectors incl. moving objects | NOT RUN | `PSMainGBuffer`, `TemporalMath` |
| F-03 | Halton(2,3) jitter | NOT RUN | `halton_jitter_pixels` |
| F-04 | History rejection (ID / depth / normal / bounds / clip) | NOT RUN | `TemporalReproject.hlsl` |
| F-05 | À-trous denoiser | NOT RUN | `AtrousFilter.hlsl`, `TemporalDenoiser.*` |
| F-06 | Reset reasons + per-history policies | NOT RUN | `FrameRevision.*` |
| F-07 | Scripted camera/object/emissive motion | NOT RUN | `--camera-path`, `--animate-node`, `--animate-emissive` |
| F-08 | Ghosting/disocclusion visual cases | NOT RUN | No captures |
| F-09 | Temporal debug views | NOT RUN | `--temporal-debug` |

## G — Optional DLSS Super Resolution

| ID | Requirement | Status | Source / what is missing |
|---|---|---|---|
| G-01 | Default-OFF build option with BLOCKED configure errors | NOT RUN | `DAEDALUS_ENABLE_DLSS` in `CMakeLists.txt`; never configured |
| G-02 | Vendor integration (feature creation/evaluate) | BLOCKED | No NVIDIA SDK, headers, libraries or runtime were available; no vendor API was invented. Needs a user-supplied glue `.cpp` |
| G-03 | Explicit fallback (native / labelled bilinear) and status reporting | NOT RUN | `DlssBackend.*`, capture/benchmark metadata |
| G-04 | DLSS vs native at equal display resolution | BLOCKED | Depends on G-02 |

## H — Asset compatibility pipeline

| ID | Requirement | Status | Source / what is missing |
|---|---|---|---|
| H-01 | OBJ/FBX/3DS through Assimp (optional, version-pinned, no network) | BLOCKED | Assimp is not installed in the delivery environment; `CompatibilityImporter.*`, `DAEDALUS_ENABLE_ASSIMP` |
| H-02 | Explicit unavailable path without Assimp | NOT RUN | `compatibility_importer_unavailable` diagnostic |
| H-03 | Headless Blender worker + versioned script | BLOCKED | Blender is not installed; `BlenderWorker.*`, `scripts/blender/daedalus_blend_export.py` (never executed) |
| H-04 | Case-aware, root-confined texture resolution | NOT RUN | `TextureResolver.*`; test source not executed |
| H-05 | Content-addressed cache (corruption/staleness/atomic publish) | NOT RUN | `AssetCache.*`, `SceneSerialization.*` |
| H-06 | Batch convert/validate tool | NOT RUN | `DaedalusAssetBatch` |
| H-07 | Report provenance (`import-report/2`) | NOT RUN | `ImportReport.*`, `AssetLoader.*` |
| H-08 | OBJ ↔ glTF equivalence fixture | NOT RUN | `quad.obj`/`quad.mtl` are committed (a root `.gitignore` `*.obj` rule previously dropped `quad.obj`; now excepted). `checker.png` and `quad_equivalent.gltf` exist only after `generate_fixtures.py` runs; the test is compiled in only with `DAEDALUS_ENABLE_ASSIMP=ON` and those files present at configure time |

## Unsupported glTF extensions (unchanged scope)

Transmission/volume/IOR/emissive-strength are factor-only; their textures are ignored and reported. `KHR_texture_transform`, `KHR_materials_clearcoat/sheen/specular/iridescence/anisotropy/dispersion`, `KHR_draco_mesh_compression`, `EXT_meshopt_compression`, `KHR_texture_basisu`, sparse accessors and non-triangle primitive modes remain unsupported.
