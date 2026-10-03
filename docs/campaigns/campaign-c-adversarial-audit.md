# Campaign C adversarial self-audit

Status: source-level self-audit complete; independent review and final Windows evidence remain separate requirements.

## Findings checked and disposition

- **Double/missing gamma:** base colour and emissive continue to use sRGB SRVs; metallic/roughness, normal and AO remain linear. The output pass performs the only explicit linear-to-sRGB transfer.
- **Metallic/roughness reversal:** production shader still reads roughness `.g` and metallic `.b`; portable tests and orthogonal fixture retained.
- **AO on direct light:** C2 IBL is the only term multiplied by AO. Punctual direct lighting is not.
- **Normal/TBN sign:** C1 tangent `w`, world determinant, inverse-transpose normal and double-sided face-sign logic remain in the production path.
- **Alpha:** MASK discard happens before diagnostics/expensive PBR; BLEND retains depth test/no depth write and deterministic object sorting.
- **Roughness singularities:** direct BRDF regularization remains in PBR math; environment approximation clamps the supported perceptual roughness domain.
- **Shadow projection:** directional fit derives from selected-scene bounds; spot FOV derives from outer cone. No giant arbitrary orthographic range.
- **Shadow bias:** constant + normal-dependent bias and 3x3 PCF are explicit. Runtime acne/peter-panning judgment is NOT RUN in this environment.
- **Point shadows:** deliberately unsupported instead of silently pretending the point-light path is shadowed.
- **IBL orientation/encoding:** analytic environment lives directly in linear radiance; there is no HDR texture decode or gamma path to double-encode.
- **Tone map hiding NaNs:** validation copies the HDR half-float target and counts non-finite components before tone mapping. Tone-map shader displays non-finite source as magenta for interactive visibility.
- **Capture synchronization:** application requests copy during the target frame and calls `wait_for_gpu()` before mapping/encoding.
- **Image comparison domain:** PNG RGB bytes are converted from sRGB to linear sRGB before metrics; reports name that domain.
- **Golden auto-update:** regression and reference promotion are separate scripts; promotion requires an explicit confirmation switch.
- **GPU timing units:** D3D12 timestamp ticks are converted with queue frequency to milliseconds; CPU frame time remains separately named.
- **Resource accounting:** committed allocation estimate, canonical retained bytes and logical geometry bytes are emitted as distinct fields.
- **Resize/reload:** HDR/depth are recreated on resize after synchronization; shadow map is scene-owned and renderer replacement occurs after GPU idle on reload.

## Unresolved evidence risks

The implementation has not been compiled with the final C2 MSVC/DXC graph in the delivery environment. Shadow resource states, root-signature/descriptor bindings, capture footprints, timestamp readback, WIC encoding, live objects, visual shadow bias, and analytic IBL appearance therefore remain runtime evidence items rather than source-level PASS claims.
