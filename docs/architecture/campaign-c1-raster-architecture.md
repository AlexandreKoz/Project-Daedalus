# Campaign C1 raster architecture decision

Status: accepted for Campaign C1. This document does **not** close Campaign C as a whole.

## Decision: conventional forward rendering

Campaign C1 uses a conventional forward raster path rather than forward+ or deferred rendering.

The repository is still a rendering laboratory/reference viewer with a bounded punctual-light set and no requirement yet for hundreds of dynamic lights. A forward path therefore has the smallest state surface, preserves direct access to material inputs for diagnostics, avoids an unnecessary G-buffer ABI before Campaign F defines temporal outputs, and remains straightforward to reuse when Campaign D adds hybrid ray-traced effects. Forward+ would add tile/cluster construction and GPU light-list lifetime without a demonstrated need; deferred would add several persistent render targets, bandwidth, transparent-material special cases, and an early commitment to a G-buffer contract.

The C1 bounded light contract is `kMaximumPunctualLights = 32`. Exceeding it is a hard, reported preparation failure; lights are never silently dropped. A later campaign may replace the bounded constant buffer with clustered/forward+ light lists if measured scenes justify it.

## Dependency and ownership direction

```text
scene
  CanonicalScene, renderer-independent materials/images/samplers/lights/transforms
    ↑
rendering
  PbrMath + RasterPreparation + OrbitCamera + CPU/HLSL ABI definitions
  (portable, no D3D12 dependency)
    ↑
graphics
  PbrSceneRenderer
  D3D12 resources, descriptors, PSOs, HDR/depth targets, shader binding
    ↑
Application
  window/viewer lifecycle, reload/resize/stress policy
```

`daedalus_scene` remains DirectX-independent. `RasterPreparation` resolves scene traversal, default-material semantics, material bindings, stable material IDs, light transforms, and deterministic transparent ordering without a GPU. `PbrMath` contains independent CPU reference mathematics for colour conversion, material factors, BRDF, attenuation, exposure, and tone mapping. `PbrSceneRenderer` owns only GPU representations and pass recording.

The Campaign B `DiagnosticSceneRenderer` was retired rather than grown into a second renderer. Existing normal/UV/tangent/bounds diagnostics now run through the same production geometry/material preparation and raster shader used by PBR. Campaign C1 adds base-colour, metallic, roughness, emissive, and stable material-ID views.

## Pass structure

1. **Forward material/direct-light pass** into `R16G16B16A16_FLOAT` scene colour with a `D32_FLOAT` depth target.
2. **Tone/output pass** samples HDR scene colour and writes the existing `R8G8B8A8_UNORM` swap-chain target.

Resource transitions are explicit:

```text
HDR: PIXEL_SHADER_RESOURCE -> RENDER_TARGET -> PIXEL_SHADER_RESOURCE
Back buffer: PRESENT -> RENDER_TARGET -> PRESENT
Depth: DEPTH_WRITE for the raster pass
```

Resolution-dependent HDR/depth resources are recreated after the application/context has synchronized resize. Scene reload already waits for GPU idle before the renderer and canonical scene are replaced.

## Draw partition and raster state

Opaque and masked draws preserve canonical traversal order and execute first with depth writes enabled. Blend draws execute afterward with depth test enabled, depth writes disabled, straight-alpha blending, and deterministic back-to-front object ordering by transformed primitive-bounds centre. Stable tie breakers use material diagnostic ID, primitive ID, and original draw index. Object sorting cannot correctly order every pair of intersecting transparent surfaces; C1 documents that limitation rather than introducing OIT.

Single-sided materials cull back faces. Canonical glTF front faces are counter-clockwise. A negative world determinant flips the front-face PSO convention so mirrored instances do not disappear. Double-sided materials disable culling and the pixel shader uses `SV_IsFrontFace` to reverse the shading frame on the back side rather than merely exposing the back face with front-side normals.

## Constant-data lifetime

Frame, light, and draw constant data are written to persistently mapped upload heaps partitioned by the D3D12 swap-chain frame index. `D3D12Context::begin_frame()` waits for the fence associated with that frame resource before the partition is rewritten. This preserves the Campaign A/B fence discipline without allocating per-draw upload resources every frame.

Textures are uploaded from the already-decoded canonical top-left RGBA8 image. The renderer never re-decodes PNG/JPEG and canonical structures never own D3D12 resources.

## Descriptor/material binding strategy

C1 intentionally uses five explicit texture/SRV tables and five sampler tables per draw:

- base colour;
- metallic-roughness;
- normal;
- occlusion;
- emissive.

Each uploaded RGBA8 texture has both linear and sRGB SRV interpretations. Material slot semantics choose the view; filenames do not. This also handles a canonical image reused by slots with different colour-space intent without duplicating decoded data.

This is deliberately not bindless. The current scale does not justify descriptor indexing complexity, and Campaigns D–G can evolve the descriptor strategy behind the renderer boundary when there is evidence for it.

## C1 mip policy

C1 uploads one mip level per canonical image. All samplers clamp `MinLOD = MaxLOD = 0`, so glTF minification modes that name mip filters cannot access nonexistent levels. Magnification and base-level minification filtering plus wrap modes are respected. Full glTF mip-filter fidelity is therefore **not** claimed; coherent mip generation/preprocessing is deferred.
