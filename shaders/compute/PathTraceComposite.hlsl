// Campaign E/F: remodulates the denoised path-traced signal. The denoiser filtered
// (L - Le_primary) / albedo, so the displayed linear radiance is filtered * albedo + Le_primary.
// The progressive reference accumulation is untouched by this pass.

#include "compute/ComputeCommon.hlsli"

Texture2D<float4> g_filtered : register(t0);
Texture2D<float4> g_albedo : register(t1);
Texture2D<float4> g_primary_emission : register(t2);
RWTexture2D<float4> g_output : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
    const uint2 pixel = dispatch_id.xy;
    if (outside_render_target(pixel))
        return;
    const float3 radiance = g_filtered[pixel].rgb * g_albedo[pixel].rgb + g_primary_emission[pixel].rgb;
    g_output[pixel] = float4(radiance, 1.0);
}
