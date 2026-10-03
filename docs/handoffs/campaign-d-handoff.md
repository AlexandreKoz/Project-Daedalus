# Campaign D handoff from Campaign C

Campaign D may consume the following source-level contracts after the final Campaign C Windows acceptance rows are executed. Items explicitly marked unproven in `docs/campaigns/campaign-c-acceptance.md` must not be assumed runtime-proven merely because the source exists.

## Stable contracts

- **Canonical geometry/materials:** renderer-independent canonical scene remains authoritative; no D3D12 resources are stored there.
- **Material evaluation:** glTF metallic-roughness base colour, G roughness/B metallic, tangent normal, AO, emissive, alpha and double-sided semantics are defined by the C1/C2 PBR contract.
- **Texture colour space:** base colour/emissive sRGB; metallic-roughness/normal/AO linear; selection is semantic, never filename based.
- **Camera:** deterministic orbit framing, right-handed view/projection contract, explicit view-projection matrices.
- **Depth:** raster depth uses `D32_FLOAT`; the depth diagnostic exposes normalized hardware depth. Future DXR effects must document any linearization they require.
- **Normals/TBN:** inverse-transpose normal transform, source tangent `w`, world determinant and double-sided face orientation are established.
- **Lights:** canonical directional/point/spot lights use glTF local -Z direction; direct-light attenuation and cone semantics are portable/tested.
- **Shadows:** Campaign C raster shadows select one directional/spot caster. Campaign D ray-traced shadows must not depend on or mutate this limitation.
- **HDR scene colour:** `R16G16B16A16_FLOAT` linear scene radiance precedes exposure/tone mapping.
- **Environment:** `daedalus-procedural-sky-v1` is deterministic renderer configuration. A DXR path may sample the same analytic contract without moving it into `CanonicalScene`.
- **Output boundary:** exposure + compact ACES-fit + linear-to-sRGB occur after HDR rendering.
- **Diagnostics/capture:** deterministic fixed-frame PNG capture, metadata, HDR non-finite scan, linear-sRGB image comparison and explicit golden policy exist as validation infrastructure.
- **Resource lifetime:** frame-partitioned upload constants, explicit transitions, fence-safe reload/resize and readback synchronization remain required.
- **Shader ABI:** fixed-width/aligned frame/draw/light contracts remain explicit and tested; Campaign D must add separate DXR ABI contracts rather than overloading raster structures indiscriminately.

## Known limits relevant to Campaign D

- raster point lights do not cast shadow maps;
- one directional/spot raster shadow map, no cascades;
- procedural analytic IBL rather than arbitrary HDR environment import/prefilter;
- one mip uploaded for canonical glTF textures; sampler LOD clamped to zero;
- object-level transparency sorting only;
- no temporal history/motion vectors yet;
- final C2 Windows image-regression/performance/live-object evidence must be completed before treating Campaign C as exited.
