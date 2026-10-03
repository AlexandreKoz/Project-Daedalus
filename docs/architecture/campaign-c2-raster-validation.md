# Campaign C2 raster validation architecture

Status: implemented source architecture; Windows/D3D12 runtime evidence must be produced on the developer machine before Campaign C can exit.

## Scope and decision

Campaign C2 preserves Campaign C1's conventional forward renderer. No forward+, deferred renderer, render graph, ECS, DXR, temporal system, or gameplay layer is introduced. C2 adds a shadow depth pass, deterministic analytic environment lighting, validation/capture paths, performance instrumentation, and the remaining diagnostics around the existing forward PBR pass.

The pass order is:

1. optional directional/spot shadow depth pass into a 2048x2048 `D32_FLOAT` shadow map;
2. forward opaque/masked PBR into `R16G16B16A16_FLOAT` + `D32_FLOAT` scene depth;
3. forward transparent PBR with depth test and no depth writes;
4. tone/output pass to the `R8G8B8A8_UNORM` swap-chain buffer;
5. optional validation readback of the HDR scene target and/or presented LDR buffer.

Overdraw is a dedicated variant of the production material path. It disables depth and uses additive blending so the accumulated value represents alpha-tested fragment shader invocations after material culling. It is not draw-order colouring.

## Shadow contract

Campaign C uses one deterministic shadow map. The first canonical directional or spot light is selected as the shadow caster. Point-light cubemap shadows are intentionally **not** implemented in Campaign C2; point lights continue to contribute unshadowed direct light.

Directional light projection is fitted to canonical selected-scene bounds transformed into light space with a small deterministic margin. Spot projection derives its FOV from the canonical outer cone and its far plane from the optional range (or a documented scene-size fallback). No giant arbitrary world-space orthographic volume is used.

The production shader uses 3x3 PCF with a comparison sampler. Bias has a constant term plus a normal-dependent term. Shadow resources are renderer-owned, explicitly transitioned between `DEPTH_WRITE` and `PIXEL_SHADER_RESOURCE`, survive swap-chain resize unchanged, and are destroyed/recreated with the renderer on scene reload.

Alpha-masked casters execute the same base-colour alpha/cutoff semantics as the production material path. BLEND draws do not cast in this bounded Campaign C policy.

## IBL contract

The environment is renderer configuration, not glTF scene ownership. C2 deliberately avoids introducing a general HDR-image importer.

The source contains a deterministic self-authored analytic environment, `daedalus-procedural-sky-v1`:

- diffuse irradiance is a deterministic hemispherical approximation;
- specular environment response blends sharp directional environment radiance toward a fixed integrated average as perceptual roughness increases;
- the split-sum environment BRDF uses a documented analytic fit equivalent to a small BRDF LUT for the supported metallic-roughness model;
- dielectric F0 remains 0.04, matching direct lighting;
- metallic suppresses diffuse and transfers energy to specular;
- AO modulates only indirect illumination;
- emissive remains independent.

This is a reproducible analytic/precomputed-equivalent path rather than texture-mip prefiltering. `roughness_to_environment_lod()` remains portable/tested so a later image-based environment implementation can preserve the same roughness contract without changing materials.

## Diagnostics

Production data is exposed through one material shader path:

- hardware normalized depth (`SV_Position.z` after viewport depth mapping);
- world/shading normal;
- UV;
- tangent;
- bounds overlay;
- base colour;
- metallic;
- roughness;
- emissive;
- stable material ID;
- overdraw fragment invocation accumulation.

Default material diagnostic ID 0 remains distinct from source material 0 (ID 1).

## Deterministic capture and regression

`--capture` requests an explicit GPU copy of the presented back buffer into a readback resource on a fixed frame. The application waits for that frame before PNG encoding. `--validate-hdr` copies `R16G16B16A16_FLOAT` scene colour before tone mapping and counts half-float NaN and infinity components. A valid 8-bit PNG alone is never treated as finite-HDR evidence.

Capture metadata records build, asset key, scene, diagnostic mode, dimensions, frame, exposure, tone-map contract, environment, shadow settings, orbit camera eye/target/FOV, adapter, WARP identity, dedicated VRAM, feature level, debug-layer state, and HDR non-finite counts. Driver version is not invented because the current context does not expose a reliable version string.

`DaedalusImageCompare` decodes reference/candidate PNGs, converts sRGB RGB bytes back to linear sRGB, and reports MAE, RMSE, maximum absolute error, and the fraction of channels above threshold. It emits a magnified visual difference PNG and machine-readable JSON. Normal validation never overwrites golden images; reference promotion is an explicit separate script.

## Timing and resource accounting

D3D12 timestamp queries record frame start, shadow completion, opaque completion, transparent completion, and tone-map completion. Query data is resolved into a frame-owned readback allocation and consumed only when that swap-chain frame index is safely reused. Queue timestamp frequency converts ticks to milliseconds. CPU frame work uses `std::chrono::steady_clock` and remains separately labelled.

Resource reporting distinguishes:

- canonical retained bytes and canonical geometry payload;
- renderer resource counts;
- an estimated sum of D3D12 committed resource allocation sizes from `GetResourceAllocationInfo`.

The committed allocation estimate is explicitly not process working set, heap residency, or total driver VRAM use.

## Lifetime invariants

Campaign A/B/C1 synchronization remains authoritative. Frame/light/draw/shadow constants are partitioned by swap-chain frame ownership. Scene reload waits for GPU idle before replacing canonical-bound resources. Capture readbacks are per-request and consumed only after `wait_for_gpu()`. Resize recreates only resolution-dependent HDR/depth resources after context synchronization. Shadow resources are scene-dependent, not resolution-dependent.
