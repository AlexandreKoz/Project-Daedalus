# Campaign C handoff after Campaign B audit closure

Campaign C may design production shading against the following **canonical CPU contracts**, which are now covered by portable regression tests:

- source material indices are stable;
- invalid primitive material ID means `CanonicalScene::default_material`, whose factors match glTF defaults;
- supported texture references preserve `texcoord_set` 0/1 and import fails if the primitive lacks the referenced set;
- canonical vertices contain positions, unit normals, unit tangent XYZ plus signed `w`, UV0, UV1, and colour;
- supported images are fully decoded before import success and own top-left-origin tightly packed RGBA8 pixels plus encoded provenance bytes;
- texture colour-space intent remains metadata-driven: colour slots are sRGB, data slots linear, and shared conflicting use is reported;
- scene/node/mesh/primitive bounds are recomputed from decoded data;
- selected scene instances are represented by node world transforms and may share one mesh/primitive;
- negative determinant is explicit and tangent handedness is adjusted only at the renderer boundary;
- resource-use counters and deterministic asset identity are available;
- D3D12 resources remain outside the canonical scene and are uploaded through explicit fence-safe staging.

Campaign C must not:

- reinterpret invalid material ID as source material 0;
- assume UV0 for every texture;
- re-decode supported images independently and disagree with canonical validation;
- silently renormalize invalid vectors;
- store GPU/descriptor objects in canonical structures;
- treat the tangent diagnostic as production normal mapping;
- reintroduce Debug `-Od` for the current Windows SDK/WARP path without a new controlled validation.

## Runtime evidence caveat

The closure source implements the updated D3D12 consumption path, tangent mode, stress options, and live-object reporting, but this agent could not run Windows. Before Campaign C treats the **GPU implementation** as accepted, execute the exact closure archive on hardware and WARP, including UV1, tangents, instances, reload/resize stress, and post-teardown live-object inspection. Canonical/importer semantics may be used now; final graphics acceptance remains gated by those runs.
