# Campaign C1 PBR material, lighting, and output contract

This document defines the supported Campaign C1 glTF 2.0 metallic-roughness raster model. It is intentionally narrower than full Campaign C acceptance: shadows, image-based lighting, screenshot regression, and final reference/performance evidence remain Campaign C2 work.

## Material sampling and colour spaces

| Slot | SRV interpretation | Channels used | Combination |
|---|---|---|---|
| Base colour | sRGB RGB; alpha remains linear/unmodified by sRGB decode | RGBA | `baseColorFactor * texture * vertexColor` |
| Metallic-roughness | Linear | roughness = **G**, metallic = **B** | texture channels times scalar factors |
| Normal | Linear | XYZ | `[0,1] -> [-1,1]`, scale tangent X/Y, renormalize |
| Occlusion | Linear | **R** | `lerp(1, R, strength)` |
| Emissive | sRGB RGB | RGB | `emissiveFactor * texture` |

The renderer does not infer colour space from names and does not re-decode images. Each canonical RGBA8 texture receives a linear and an sRGB view; the material slot selects the required interpretation. DirectX sRGB SRV decoding applies to RGB while the alpha channel retains its stored value, which preserves glTF base-colour alpha semantics.

The canonical default-material sentinel is unchanged: an invalid primitive `MaterialId` resolves to `CanonicalScene::default_material`. Source material 0 remains source material 0. For diagnostics the default material has stable ID 0 and source material `i` has ID `i + 1`.

## Normal and tangent convention

Canonical space retains glTF right-handed coordinates and counter-clockwise front faces. Source tangent XYZ and tangent `w` are preserved by Campaign B.

For shading:

1. geometric normal is transformed by the inverse-transpose world matrix and normalized;
2. tangent XYZ is transformed by the world matrix;
3. tangent is Gram-Schmidt orthogonalized against the shading-side geometric normal;
4. tangent handedness is `source_w * sign(world determinant) * face_sign`;
5. bitangent is `cross(N, T) * handedness`;
6. tangent-space normal is decoded, `normalTexture.scale` multiplies X and Y, and the vector is normalized before TBN transformation;
7. the final world shading normal is normalized.

For a double-sided back face, `SV_IsFrontFace` reverses the shading normal and the tangent handedness sign while leaving the authored UV tangent direction unchanged. This preserves the UV derivative directions (`T`/`B`) while flipping the normal seen by the lighting equation; merely disabling culling would be incorrect.

**Missing tangent policy:** normal-map evaluation is disabled for a primitive whose canonical source lacks tangents. The renderer logs the limitation and the normal diagnostic continues to show the geometric/interpolated normal. C1 does not invent arbitrary tangent vectors and does not implement a derivative fallback. Although Campaign B supplies default vertex storage values for a missing tangent attribute, `Primitive::has_tangents` remains false and is the production decision bit.

A singular world transform cannot provide a meaningful inverse-transpose. The importer already reports singular transforms; the GPU boundary uses identity as a finite last-resort normal matrix rather than generating NaN/Inf. Such source geometry remains diagnostically degraded rather than physically trustworthy.

## Metallic-roughness BRDF

C1 uses an isotropic Cook-Torrance-style microfacet model aligned with the glTF metallic-roughness workflow.

Let perceptual roughness be `r` and microfacet roughness be:

```text
r_safe = clamp(r, 0.045, 1)
alpha  = r_safe^2
```

The lower bound is a numerical regularization for a finite real-time raster representation of the delta-like zero-roughness limit; it is not hidden tone-map clipping.

### Fresnel

Dielectric base reflectance is:

```text
F_dielectric = 0.04
F0 = lerp(0.04, baseColor, metallic)
F  = F0 + (1 - F0) * (1 - V·H)^5
```

### GGX / Trowbridge-Reitz distribution

```text
D_GGX = alpha^2 /
        [pi * ((N·H)^2 * (alpha^2 - 1) + 1)^2]
```

### Correlated Smith visibility

The implementation uses the correlated Smith GGX visibility form:

```text
lambdaV = (N·L) * sqrt((N·V)^2 * (1 - alpha^2) + alpha^2)
lambdaL = (N·V) * sqrt((N·L)^2 * (1 - alpha^2) + alpha^2)
V_GGX   = 0.5 / (lambdaV + lambdaL)
```

Zero/invalid denominators contribute zero rather than NaN/Inf.

### Diffuse/specular separation

```text
diffuseBRDF  = (1 - F) * baseColor * (1 - metallic) / pi
specularBRDF = F * D_GGX * V_GGX
Lo_direct   += (diffuseBRDF + specularBRDF) * max(N·L, 0) * incidentLight
```

The Fresnel factor removes reflected energy from the diffuse lobe, and pure metallic material has no diffuse term. CPU reference tests cover normal incidence, grazing angles, metallic 0/1/mixed values, roughness extremes, negative `N·L`, and finite/non-negative output.

## Punctual lights

Campaign C1 consumes canonical `KHR_lights_punctual` data. A light node emits along local **-Z**, transformed into world space by the node world matrix and normalized.

- **Directional:** `incidentLight = color * intensity`; the surface-to-light direction is the negative of emitted direction.
- **Point:** `incidentLight = color * intensity * attenuation(distance)`.
- **Spot:** point-light distance attenuation multiplied by cone attenuation.

For finite positive range `R`, Daedalus uses the smooth KHR-compatible quartic window:

```text
x = d / R
attenuation = (1 / d^2) * max(1 - x^4, 0)^2
```

At or beyond range, contribution is zero. Without a range, attenuation is `1/d^2`. Zero distance is guarded and produces no undefined inverse.

Spot falloff uses cosine-space smooth interpolation from the outer cone to the inner cone:

```text
0                              cos(theta) <= cos(outer)
1                              cos(theta) >= cos(inner)
smoothstep(cos(outer),
           cos(inner),
           cos(theta))          otherwise
```

The canonical importer validates cone ordering. Preparation rejects zero/non-finite transformed directions and rejects scenes exceeding the 32-light C1 limit instead of silently truncating.

## Ambient occlusion before C2 IBL

C1 has no environment/IBL solution yet. To keep material diagnostics and scenes readable while preserving replacement boundaries, it uses a deliberately small provisional indirect diffuse term:

```text
indirect = provisionalAmbient * baseColor * (1 - metallic) * AO
```

AO affects **only** this provisional indirect term. It never darkens punctual direct illumination or emissive output. Campaign C2 should replace this term with image-based diffuse/specular environment lighting and apply AO according to that documented indirect-light model.

## Emissive

Emissive RGB is decoded from sRGB, multiplied by `emissiveFactor`, and added independently of punctual lighting:

```text
sceneColor = direct + provisionalIndirect + emissive
```

It is not multiplied by `N·L`, light intensity, or AO.

## HDR, exposure, tone mapping, and display encoding

PBR lighting writes linear scene colour to `R16G16B16A16_FLOAT`. Exposure is specified in EV stops:

```text
exposed = sceneColor * 2^exposureEV
```

CLI range is `[-24, +24]`, default `0`.

Shaded output uses the compact Narkowicz/Hill-style ACES fit, documented here as a display operator rather than full ACES colour management:

```text
y = clamp( x * (2.51*x + 0.03) /
           (x * (2.43*x + 0.59) + 0.14), 0, 1 )
```

Portable tests require finite and monotonic output across the positive validation range. Diagnostic values bypass exposure and the ACES fit so the views represent actual material intermediates rather than artistic display compression.

At the `R16G16B16A16_FLOAT` storage boundary, finite radiance above 65504 is clamped to the largest finite half-float value. A NaN/Inf produced by shading is converted to a conspicuous finite magenta sentinel rather than written as NaN/Inf. This is an explicit render-target representability/diagnostic guard, not a tone-mapping substitute; C2 image validation should treat the sentinel as a failure signature.

The swap chain is `R8G8B8A8_UNORM`, not an sRGB RTV, so the tone/output pixel shader performs one explicit linear-to-sRGB transfer before writing. There is no second hardware sRGB encode step.

## Alpha modes

- `OPAQUE`: depth test/write enabled, no blending.
- `MASK`: the base-colour alpha product is compared against `alphaCutoff` and discarded before metallic/normal/direct-light work; depth writes remain enabled.
- `BLEND`: straight-alpha output and `SRC_ALPHA / INV_SRC_ALPHA` colour blending; depth test enabled, depth writes disabled; opaque/mask first, then deterministic object-level back-to-front sorting.

No weighted OIT is implemented. Intersecting transparent geometry can remain order-dependent.
