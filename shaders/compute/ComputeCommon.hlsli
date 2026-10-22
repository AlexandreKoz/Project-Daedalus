#ifndef DAEDALUS_COMPUTE_COMMON_HLSLI
#define DAEDALUS_COMPUTE_COMMON_HLSLI

// Shared compute root signature (src/graphics/ComputePass.h):
//   b0 GpuViewConstants, b1 16 x 32-bit pass constants, t0..t15 inputs, u0..u7 outputs,
//   s0 linear clamp, s1 point clamp. All passes use 8x8 thread groups.

#include "common/FrameContract.hlsli"

ConstantBuffer<ViewConstantsData> g_view : register(b0);
cbuffer PassConstants : register(b1)
{
    uint4 g_pass0;
    uint4 g_pass1;
    uint4 g_pass2;
    uint4 g_pass3;
};
SamplerState g_linear_clamp : register(s0);
SamplerState g_point_clamp : register(s1);

bool outside_render_target(uint2 pixel)
{
    return pixel.x >= uint(g_view.render_size.x) || pixel.y >= uint(g_view.render_size.y);
}

float compute_luminance(float3 value)
{
    return dot(value, float3(0.2126, 0.7152, 0.0722));
}

float3 identifier_color(uint identifier)
{
    if (identifier == 0u)
        return float3(0.0, 0.0, 0.0);
    uint hash = identifier * 747796405u + 2891336453u;
    hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
    hash = (hash >> 22u) ^ hash;
    return float3(0.2 + 0.8 * float((hash >> 0u) & 255u) / 255.0,
                  0.2 + 0.8 * float((hash >> 8u) & 255u) / 255.0,
                  0.2 + 0.8 * float((hash >> 16u) & 255u) / 255.0);
}

bool finite3(float3 value)
{
    return !any(isnan(value)) && !any(isinf(value));
}

#endif
