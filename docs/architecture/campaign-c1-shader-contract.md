# Campaign C1 CPU/HLSL data contract

Campaign C1 does not rely on implicit C++ packing. `src/rendering/RasterShaderContract.h` is the portable CPU mirror of the C1 HLSL constant data and contains alignment/size/offset `static_assert` guards.

## Frame constants — `b0`

`RasterFrameConstants` is 112 bytes, 16-byte aligned:

- column-major view-projection matrix;
- camera world position;
- provisional ambient/indirect term;
- diagnostic mode;
- punctual-light count;
- explicit padding.

`diagnostic_mode` offset: 96 bytes.

## Draw constants — `b1`

`RasterDrawConstants` is 224 bytes, 16-byte aligned:

- world matrix;
- inverse-transpose normal matrix;
- base-colour factor;
- emissive factor + metallic scalar;
- roughness + normal scale + AO strength + alpha cutoff;
- material/attribute flags;
- stable material diagnostic ID;
- alpha mode;
- independent UV-set index for all five material slots;
- world determinant handedness;
- explicit tail padding.

`flags` offset: 176 bytes. `world_handedness` offset: 208 bytes.

## Light constants — `b2`

`RasterLightGpu` is 64 bytes and 16-byte aligned. The bounded `RasterLightConstants` buffer holds 32 entries. Each entry stores position/range, emitted direction/outer-cone cosine, RGB/intensity, type, inner-cone cosine, and explicit padding.

The portable `Daedalus.Rendering` tests compile these guards on non-Windows toolchains as well as testing the named sizes/offsets. Shader source-health checks require the corresponding HLSL fields/registers, while the final shader bytecode ABI still requires the Windows/DXC build before runtime acceptance.

## Root signature

The forward pass binds three root CBVs followed by five one-SRV descriptor tables and five one-sampler descriptor tables. The tone pass binds the HDR scene-colour SRV plus four 32-bit constants (exposure EV, diagnostic mode, two explicit pads).

The design is intentionally explicit and bounded for C1; it is not presented as a final bindless/material-buffer architecture.
