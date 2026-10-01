# glTF 2.0 supported subset

## Canonical ingestion accepted

- JSON `.gltf` and binary `.glb` version 2.
- External relative, data-URI, GLB-buffer, and image-bufferView resources under the documented security/path policy.
- PNG and supported JPEG payloads fully decoded by the assets layer before import success into owned top-left RGBA8 canonical pixels.
- Multiple scenes, roots, nodes, meshes, triangle primitives, materials, textures, images, samplers, cameras, and punctual lights.
- BYTE/UNSIGNED_BYTE/SHORT/UNSIGNED_SHORT/UNSIGNED_INT/FLOAT accessors where allowed by the declared semantic; normalized integer conversion and supported interleaving.
- `POSITION`, `NORMAL`, `TANGENT`, `TEXCOORD_0`, `TEXCOORD_1`, and VEC3/VEC4 `COLOR_0` under the repository's validated schema rules.
- `KHR_mesh_quantization` only when declared consistently with the source use.
- Explicit or generated sequential indices; canonical indices are 32-bit.
- Core metallic-roughness factors and texture references, normal scale, occlusion strength, emissive, alpha metadata, and double-sided metadata.
- Perspective/orthographic cameras.
- `KHR_lights_punctual` directional, point, and spot metadata.

## Campaign C1 raster consumption

The production forward raster path consumes the canonical core metallic-roughness subset as follows:

- base colour factor × sRGB base-colour texture × vertex colour;
- roughness from metallic-roughness **G**, metallic from **B**, both linear-data sampled and factor-scaled;
- tangent-space normal map sampled as linear data, including normal scale, source tangent sign, negative world determinant, inverse-transpose normal transform, and double-sided face handling;
- emissive factor × sRGB emissive texture, independent of direct-light multiplication;
- occlusion from linear-data **R** with strength, applied only to C1's provisional indirect term;
- OPAQUE, MASK/alphaCutoff, and straight-alpha BLEND with deterministic object-level sorting;
- `doubleSided` raster/shading behavior;
- directional, point, and spot punctual direct lighting with canonical glTF local -Z orientation.

The canonical default-material sentinel remains distinct from source material index 0.

## Rejected or warned during ingestion

- sparse accessors: unsupported/rejected;
- primitive modes other than TRIANGLES: rejected;
- unknown required extensions: rejected; unknown optional extensions: inventoried/warned;
- `KHR_texture_transform`: reported unsupported rather than silently approximated;
- network/drive/absolute/backslash/path-traversal URIs, encoded NUL, malformed strict base64, invalid references/ranges/strides, non-finite values, invalid indices, malformed graphs, and invalid/unsupported images: rejected;
- accessor min/max is audited but decoded bounds are authoritative;
- degenerate triangles, missing optional normals/tangents, and singular transforms remain visible through structured diagnostics under the documented policies.

## Not implemented / not claimed

- animation, skins, morph targets, sparse accessors, Draco/meshopt, KTX/Basis, `KHR_texture_transform`, or broad material-extension support;
- mip generation: C1 uploads one mip and clamps sampler LOD to zero, therefore full glTF mip-filter fidelity is not claimed;
- derivative normal-map basis fallback when source tangents are missing;
- shadows and image-based lighting (Campaign C2);
- weighted OIT or exact intersecting-transparency resolution;
- full colour-profile management beyond the declared sRGB/linear material-slot contract.

See `docs/architecture/pbr-material-and-lighting-contract.md` for the exact C1 shading equations and limitations.
