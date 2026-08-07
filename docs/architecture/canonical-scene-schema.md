# Canonical scene schema and ownership

Schema identifier: `daedalus.canonical-scene/1`.

`CanonicalScene` owns vectors for scenes, nodes, meshes, primitives, materials, textures, images, samplers, cameras, lights, and source metadata. There are no raw owning pointers and no references into parser memory. Canonical public headers contain no D3D12, DXGI, COM, WIC, JSON-DOM, or importer-library types.

## Identity

`Handle<Tag>` provides strongly typed 32-bit indices. The all-ones value is invalid. Handles are valid only within the owning `CanonicalScene`; import validates every external index before constructing a handle.

## Transforms

A node retains source provenance through `TransformSource` and TRS fields, plus canonical local and propagated world matrices. Parents own child-handle arrays, children record one parent, and cycles or multiple-parent structures are rejected. Singular transforms are reported; negative determinant is retained for renderer policy.

## Geometry

A primitive owns packed canonical `Vertex` values and explicit 32-bit indices. Source component widths, normalization, byte offsets, and strides are decoded at import. Attribute-presence flags distinguish source data from deterministic defaults. Bounds are recomputed from decoded positions. Source accessor `min`/`max` values are retained only as validation input: mismatches create `bounds_recomputed` repair diagnostics and never replace computed bounds.

## Materials and images

Materials store metallic-roughness metadata and optional typed texture references. Textures connect one image and optional sampler and carry slot-derived colour-space intent. Images retain encoded provenance bytes, detected MIME type, source dimensions/components, source identity, and fully decoded top-left-origin RGBA8 pixels with an explicit row stride. Full decode is completed in the assets layer before import success; the renderer uploads the canonical pixels and performs no independent image-validity decision.

## Cameras, lights, and source metadata

Perspective and orthographic camera values are API-independent. Directional, point, and spot light metadata follows the supported `KHR_lights_punctual` subset. Source metadata records a display-safe source name, format/version, generator/copyright, source hash, normalized relative dependency hashes, extension inventories, and deterministic identity.

## Audit-closure schema additions

- `CanonicalScene::default_material` is the exact glTF default material. `Primitive::material == invalid` means this default; source material index zero remains source material zero.
- `Image::decoded_rgba8` contains fully validated pixels before import success. `row_stride` is `width * 4` for Campaign B.
- `SourceMetadata::resource_usage` records source, buffer, encoded-image, geometry, decoded-image, retained, and conservative-peak byte counters.
- `TextureReference::texcoord_set` is operational metadata. The importer rejects a primitive/material combination when the selected `TEXCOORD_n` attribute is absent. Runtime preparation carries the selected set to the shader.
- Normal/tangent/quaternion repairs are importer decisions and generate structured diagnostics. Math helpers are not used as a silent source-data repair policy.
