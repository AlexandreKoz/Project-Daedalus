# Campaign C handoff

Campaign C may rely on the following **portable contracts**, which passed current tests:

- `daedalus.canonical-scene/1` owned scene arrays and typed handles.
- Right-handed, metre-scale, CCW canonical geometry and parent × local transforms.
- Explicit 32-bit canonical triangle indices and packed canonical vertices, including normalized/quantized source decoding under the documented subset.
- Source-presence flags for normals, tangents, UV0/UV1, and vertex colours.
- Tangent W handedness retained; missing normals/tangents visibly diagnosed/defaulted.
- Metallic-roughness material factors and texture references, alpha metadata, double-sided flag, normal scale, occlusion strength, and emissive metadata.
- Texture/image/sampler links and sRGB versus linear intent.
- Perspective/orthographic cameras and supported punctual-light metadata.
- Recomputed primitive/mesh/node/selected-scene bounds; source accessor min/max are audit inputs, not trusted runtime bounds.
- Deterministic source/dependency identity and structured import report.

Campaign C must **not** assume the following are validated:

- Production PBR shading, alpha blending/masking, normal-map reconstruction, IBL, shadows, exposure, or tone mapping.
- Mip generation, compressed textures, KTX/Basis, texture transforms, colour profiles, or every glTF extension.
- Culling behavior under negative scale; Campaign B deliberately disables culling.
- Live Windows correctness of texture upload, descriptor binding, reload/resize, or DirectX validation. These require closure evidence before Campaign C acceptance.
- Stable persistent asset-cache ABI; Campaign B provides deterministic keys, not a production cache.
- Animation, skins, morph targets, or compatibility formats.

Campaign C should first run and close all blocked Windows Campaign A/B checks. It may then replace the diagnostic shader while preserving the canonical schema boundary, import reports, source metadata, staging-lifetime rules, and diagnostic views as regression tools.
