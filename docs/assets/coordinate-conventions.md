# Coordinate, transform, winding, tangent, unit, and colour conventions

- Canonical space preserves glTF's right-handed Cartesian convention, metres as the declared unit, and counter-clockwise triangle front faces.
- Matrices are stored column-major and multiply column vectors. Hierarchy composition is `world = parent_world * local`.
- glTF node matrices are copied in source order. TRS is composed as translation × rotation × scale.
- No handedness conversion occurs in the importer. The D3D12 diagnostic boundary uses right-handed view/projection math with a Direct3D zero-to-one depth range and configures front faces as counter-clockwise.
- Negative scale is not baked into vertices. Each node records a negative determinant. The Campaign B diagnostic PSO disables culling to prevent mirrored instances from disappearing; Campaign C must define material-aware culling.
- Tangent XYZ and W handedness are preserved. Missing tangents default to `(1,0,0,+1)` with a warning; Campaign B does not reconstruct tangent space.
- Bounds are recomputed in canonical local space and transformed using all eight corners for world bounds.
- Base-colour and emissive textures are classified sRGB; normal, metallic-roughness, and occlusion data are linear. A texture referenced in conflicting roles is conservatively retained with deterministic metadata and should be split or diagnosed in future schema revisions.
