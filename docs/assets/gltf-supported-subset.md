# glTF 2.0 supported subset

## Accepted

- JSON `.gltf` and binary `.glb` version 2.
- One JSON chunk and an optional BIN chunk in GLB.
- External relative URIs, strict base64 or percent-decoded data URIs, GLB buffers, and image bufferViews.
- PNG and baseline/progressive JPEG payloads in the declared source-component subset. Import first validates container/marker metadata, then performs a full assets-layer pixel decode before success and stores owned top-left-origin RGBA8 pixels in the canonical image. Windows uses WIC and non-Windows builds use libpng/libjpeg behind the same project-owned contract. PNG zlib framing is validated by the importer before backend decode so decoder-specific permissiveness cannot admit an invalid PNG stream; corrupt image data is reported as structured `invalid_image`, not deferred to renderer construction.
- Multiple scenes, roots, nodes, meshes, triangle primitives, materials, textures, images, samplers, cameras, and punctual lights.
- Accessor component types BYTE, UNSIGNED_BYTE, SHORT, UNSIGNED_SHORT, UNSIGNED_INT, and FLOAT; normalized integer conversion; tightly packed and interleaved data.
- `POSITION`, `NORMAL`, `TANGENT`, `TEXCOORD_0`, `TEXCOORD_1`, and VEC3/VEC4 `COLOR_0` under glTF semantic/type/component rules.
- `KHR_mesh_quantization` when declared in both `extensionsUsed` and `extensionsRequired`; integer mesh attributes that require this extension are rejected without that declaration.
- Explicit or generated sequential indices; 8/16/32-bit source indices become canonical 32-bit indices.
- Core metallic-roughness factors and texture references, normal scale, occlusion strength, emissive, alpha metadata, and double-sided metadata, with schema-range and finite-value checks.
- Perspective and orthographic cameras.
- `KHR_lights_punctual` directional, point, and spot metadata.

## Rejected or warned

- Sparse accessors: unsupported and rejected.
- Primitive modes other than TRIANGLES: rejected.
- Unknown required extensions: rejected; unknown optional extensions: inventoried and warned.
- `KHR_texture_transform`: reported as unsupported; transformation is not silently treated as equivalent.
- Network/drive/absolute URIs, backslashes, path traversal outside the asset root, encoded NUL, malformed strict base64, invalid references/ranges/strides, non-finite values, invalid indices, malformed graphs, and unsupported or structurally invalid images: rejected.
- Source accessor `min`/`max` are audited but never trusted. A mismatch produces a deterministic repair diagnostic and recomputed bounds.
- Degenerate triangles, missing optional normals/tangents, singular transforms, and deterministic defaults remain visible through structured diagnostics.

## Not implemented

Animation, skins, morph targets, sparse accessors, Draco/meshopt compression, KTX/Basis, material extensions, texture transforms, mip generation, colour-profile conversion, alpha rendering, and complete extension coverage belong to later work.

## Audit-closure clarifications

- Omitted primitive material uses the glTF default material, never source material 0.
- Base-colour, metallic-roughness, normal, occlusion, and emissive references preserve `texCoord` 0 or 1. A referenced set absent from a primitive is an `invalid_source` / `missing_texture_coordinate` error.
- PNG/JPEG acceptance requires full decode to RGBA8 before import success. Structurally plausible but undecodable entropy is `invalid_image`.
- Slight unit-length deviations up to 0.01 for normals, tangent XYZ, and rotation quaternions may be normalized with explicit repair diagnostics. Zero, non-finite, or larger deviations are rejected.
- Sparse accessors and texture transforms remain unsupported.
