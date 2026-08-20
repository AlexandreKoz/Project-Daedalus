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

Developer Windows evidence from 2026-08-20 now covers RTX and explicit WARP execution, all five diagnostic modes, UV1/tangent/instance/bounds visual proof, interactive camera use, and 100-cycle alternating reload/resize stress. The final source patch fixes the DXGI live-object acquisition path and duplicate shutdown reporting; that exact patched archive still needs one Debug Windows rebuild and post-teardown D3D12/DXGI debugger inspection. Canonical/importer semantics and the demonstrated diagnostic/stress behavior may be used as Campaign C inputs, but final Campaign B acceptance remains withheld until B-24/B-25 exact-snapshot closure and B-26 hosted CI evidence are resolved or explicitly waived by project governance.
