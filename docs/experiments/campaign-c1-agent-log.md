# Campaign C1 AI-agent experiment log

## Task

Implement the first half of Campaign C: production-structured raster metallic-roughness PBR, direct punctual lighting, HDR/exposure/tone mapping, alpha/double-sided behavior, production diagnostics, deterministic fixtures, and portable validation without claiming full Campaign C completion.

Input repository: user-supplied Campaign B audit-closure source archive `Project-Daedalus-main(9)(3).zip`.

Suggested PR after implementation: `feat/campaign-c1-forward-pbr`.

## Repository inspection

Before editing, the agent inspected the required root build/config files, Campaign B acceptance/audit/architecture/handoff documents, canonical schema and GPU-lifetime contracts, canonical scene implementation, renderer-neutral diagnostic preparation, shader ABI, orbit camera, D3D12 context, diagnostic renderer, application, command line, shader, tests, and fixture generator/manifest.

Key constraints retained:

- invalid primitive material ID means the canonical default, not source material 0;
- source material indices are stable;
- UV0/UV1 selection is explicit;
- images are canonical top-left tightly packed RGBA8;
- source tangent sign and negative world determinant remain explicit;
- colour-space intent is semantic, not filename-based;
- one primitive may be instanced by multiple nodes;
- canonical scene never owns GPU resources;
- scene replacement/resize must remain fence-safe;
- Campaign B historical BLOCKED/NOT RUN rows are not rewritten.

## Architecture selected

Conventional forward rendering was chosen after repository inspection. The diagnostic renderer was replaced rather than left as a competing implementation. Portable `PbrMath`, `RasterPreparation`, and shader ABI contracts form the renderer-neutral boundary; `PbrSceneRenderer` owns D3D12 resources and pass recording.

## Implementation highlights

- GGX/Trowbridge-Reitz + correlated Smith + Schlick metallic-roughness BRDF.
- Correct glTF G=roughness, B=metallic channel mapping.
- Slot-driven linear/sRGB SRV selection, including alpha-preserving base colour.
- Tangent-space normal mapping with source `w`, negative determinant, inverse-transpose normals, and double-sided face sign.
- Explicit missing-tangent policy: normal mapping disabled and logged.
- Emissive and AO semantics with AO restricted to provisional indirect light.
- Directional/point/spot `KHR_lights_punctual` preparation and shader evaluation; 32-light hard limit.
- Linear HDR scene colour, EV exposure, compact ACES-fit operator, explicit output sRGB encoding.
- OPAQUE/MASK/BLEND raster states and deterministic transparent sorting.
- Stable material-ID diagnostic that distinguishes canonical default from source material 0.
- Existing diagnostics retained through production path; new base-colour/metallic/roughness/emissive/material-ID diagnostics.
- C++/HLSL explicit ABI guards.
- One-mip sampler safety by LOD clamp rather than falsely claiming mip fidelity.
- 12 new self-authored deterministic C1 fixtures.

## Validation performed in this environment

Environment: Linux with GNU C++20, libpng/libjpeg; no Windows/MSVC/DXC/D3D12 runtime.

During implementation an ABI offset assertion initially caught an incorrect expected `world_handedness` offset (212 vs actual/HLSL-compatible 208); the contract/test was corrected. Existing asset tests also exposed stale links to the removed diagnostic preparation API and were migrated to production raster preparation. A test expectation for `std::invalid_argument` was corrected without weakening behavior. Warnings-as-errors caught a range-loop copy in the expanded fixture tests and it was fixed.

A later two-sided TBN review found that the first draft reversed tangent XYZ as well as the back-face normal/handedness. That would reverse authored UV derivative directions on the back face. The production shader was corrected to leave tangent XYZ unchanged while reversing `N` and the handedness sign; a portable bitangent-invariance test now guards this convention. The added test initially collided with an existing local variable name and the warnings-as-errors build stopped immediately; the test was renamed and rebuilt rather than bypassed.

Successful final pre-package validation:

- portable Debug configure/build with warnings as errors: PASS;
- portable Release configure/build with warnings as errors: PASS;
- Debug CTest Core/Scene/Assets/Rendering: 4/4 PASS;
- Release CTest Core/Scene/Assets/Rendering: 4/4 PASS;
- full controlled manifest validation in both builds: 22 valid/degraded accepted and 25 invalid rejected;
- fixture regeneration byte identity: PASS;
- `daedalus_scene` DirectX-independence scan and portable rendering dependency scan: PASS;
- local absolute-path/obvious-secret scan: PASS;
- source-health preflight: PASS.

Final Debug/Release, regeneration identity, extracted-archive validation, packaging, and SHA-256 are recorded in the delivery response/source manifest after the archive is frozen.

## Not available to this agent

- MSVC compilation of the C1 D3D12 renderer;
- DXC compilation of `RasterPbr.hlsl` and `ToneMap.hlsl`;
- hardware or WARP execution;
- visual material/light/alpha evidence;
- resize/reload/camera stress under the new renderer;
- exact C1 D3D12/DXGI live-object report;
- final Campaign C screenshot regressions, shadows, IBL, and performance baselines (C2 scope).

No runtime results are fabricated from portable validation.
