# Coordinate, transform, winding, tangent, unit, and colour conventions

- Canonical space preserves glTF's right-handed Cartesian convention, metres as the declared unit, and counter-clockwise triangle front faces.
- Matrix/TRS world propagation is deterministic and remains renderer/API independent.
- No handedness conversion occurs in the importer. The D3D12 boundary uses right-handed view/projection math with Direct3D zero-to-one depth.
- Negative scale is not baked into vertices. Each node records negative determinant.
- Tangent XYZ and source W handedness are preserved. Missing source tangents remain explicitly represented by `Primitive::has_tangents == false`; storage defaults do not make the basis semantically present.
- Decoded canonical images have top-left origin and tightly packed RGBA8 rows.
- Colour-space intent is material-slot driven rather than inferred from filenames.

## Campaign C1 raster winding and culling

Campaign C1 replaces the Campaign B cull-disabled diagnostic policy with material-aware raster state:

- single-sided positive-determinant instances use counter-clockwise front faces and back-face culling;
- single-sided negative-determinant instances flip the D3D12 front-face convention so mirrored canonical geometry remains visible without rewriting indices;
- `doubleSided` disables culling;
- double-sided back-facing fragments reverse the shading normal and tangent handedness using `SV_IsFrontFace` while preserving authored UV tangent directions, so disabling culling does not leave backside normal-map lighting mathematically wrong.

## Normal/tangent transform

For PBR shading, normals use the inverse-transpose world matrix and are normalized. Tangent XYZ uses the world matrix and is Gram-Schmidt orthogonalized against the shading-side normal. Tangent-space handedness is:

```text
source tangent w * sign(world determinant) * face sign
```

The bitangent is `cross(N,T) * handedness`. `normalTexture.scale` modifies tangent-space X/Y before final normalization/TBN transformation.

A normal texture is not evaluated if the source primitive lacks tangents. C1 intentionally has no derivative fallback and never invents an arbitrary tangent basis.

## UV and material-slot convention

Each canonical material texture reference retains its own `texcoord_set` (supported values 0/1). No runtime UV0 fallback occurs when a material requests UV1; the importer rejects the mismatch.

Base-colour and emissive RGB use sRGB SRVs. Metallic-roughness, normal, and occlusion use linear SRVs. sRGB decode does not alter base-colour alpha semantics.

## Diagnostics

`--diagnostic normals` displays the actual shading normal after double-sided and normal-map handling. `uv`, `tangents`, `base-color`, `metallic`, `roughness`, `emissive`, and `material-id` expose production-path intermediates rather than a separate fake material path.
