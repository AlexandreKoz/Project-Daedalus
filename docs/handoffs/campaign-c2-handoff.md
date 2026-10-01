# Campaign C2 handoff from Campaign C1

Campaign C1 establishes the forward PBR/direct-lighting foundation. Campaign C2 may rely on the following source-level contracts, subject to the Windows runtime rows in `docs/campaigns/campaign-c1-acceptance.md` being executed on the developer machine:

- a single production `PbrSceneRenderer`, with the Campaign B diagnostic renderer removed;
- portable renderer-neutral material/draw/light preparation;
- canonical default-material sentinel preserved and stable diagnostic IDs that distinguish it from source material 0;
- glTF base colour, metallic-roughness, normal, occlusion, emissive, alpha, double-sided, sampler, and punctual-light data consumed through the canonical scene;
- dual linear/sRGB SRV treatment over canonical RGBA8 images;
- documented GGX/Smith/Schlick BRDF and portable independent numeric tests;
- directional/point/spot direct lights with local -Z orientation, inverse-square attenuation, optional smooth range, and cone attenuation;
- an HDR `R16G16B16A16_FLOAT` scene-colour pass and explicit tone/output pass;
- EV exposure and documented compact ACES-fit display operator;
- opaque/masked first and deterministic straight-alpha transparent ordering;
- retained normal/UV/tangent/bounds diagnostics plus base-colour/metallic/roughness/emissive/material-ID views;
- deterministic Campaign C1 fixtures and portable tests;
- explicit one-mip limitation with LOD clamped to zero.

## C2 required work

C2 should concentrate on the Campaign C remainder instead of rewriting C1:

1. image-based diffuse/specular environment lighting with reproducible environment preprocessing or precomputed fixtures;
2. shadow mapping for directional/point/spot lights appropriate to the forward path;
3. replace the C1 provisional ambient term with the final documented IBL/indirect contract and revisit AO against that contract;
4. depth and overdraw diagnostics if still required by the final Campaign C specification;
5. deterministic screenshot capture, reference storage, difference images, numeric thresholds, and renderer/GPU metadata;
6. Material Sphere Grid, PBR Production Asset, Textured Cube, and Architectural Interior campaign references as appropriate;
7. GPU pass timing, CPU frame timing, resource counts/VRAM estimates, and performance baselines;
8. Windows hardware/WARP stress, resize/reload/camera evidence, live-object inspection, and NaN/Inf render-target validation;
9. Campaign C adversarial audit and final acceptance matrix.

Do not claim final Campaign C completion until those C2 evidence gates are met.

## Intentional C1 limitations C2 should understand

- maximum 32 punctual lights; hard failure above the bound;
- one mip uploaded per canonical image and mip LOD clamped to zero;
- normal maps are disabled when source tangents are absent; no derivative fallback;
- object-level transparency sorting cannot solve intersecting transparent surfaces;
- no transmission/clearcoat/specular extensions or broad glTF material extension coverage;
- compact ACES fit is a display operator, not full ACES colour management;
- singular world transforms remain degraded inputs and use a finite identity normal-matrix fallback;
- no IBL/shadows/screenshot regression/performance baseline yet.

Historical Campaign B acceptance status remains authoritative in its own matrix. C1/C2 work must not silently promote its B-24/B-25/B-26 evidence rows.
