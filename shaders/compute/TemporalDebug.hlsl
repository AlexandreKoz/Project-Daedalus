// Campaign F diagnostic visualizations of the renderer-wide temporal contract and denoiser state.
// Output is a [0,1] display signal shown with tone-map passthrough (except raw-signal, which is
// linear HDR and tone mapped normally).

#include "compute/ComputeCommon.hlsli"

Texture2D<float4> g_normal_roughness : register(t0);
Texture2D<float4> g_position_depth : register(t1);
Texture2D<uint4> g_identifiers : register(t2);
Texture2D<float4> g_motion : register(t3);
Texture2D<float4> g_denoiser_debug : register(t4);  // x disoccluded, y length/max, z variance, w reactive
Texture2D<float4> g_raw_signal : register(t5);
RWTexture2D<float4> g_output : register(u0);

static const uint VIEW_MOTION = 1u;
static const uint VIEW_DEPTH = 2u;
static const uint VIEW_NORMALS = 3u;
static const uint VIEW_DISOCCLUSION = 4u;
static const uint VIEW_HISTORY_LENGTH = 5u;
static const uint VIEW_VARIANCE = 6u;
static const uint VIEW_REACTIVE = 7u;
static const uint VIEW_RAW_SIGNAL = 8u;

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
    const uint2 pixel = dispatch_id.xy;
    if (outside_render_target(pixel))
        return;
    const uint view = g_pass0.x;
    const float motion_scale = asfloat(g_pass0.y);  // pixels mapped to full saturation
    const float far_depth = asfloat(g_pass0.z);
    const bool has_denoiser = g_pass0.w != 0u;
    const bool background = g_identifiers[pixel].x == 0u;
    float3 color = 0.0;
    if (view == VIEW_MOTION)
    {
        // Signed motion: red = +x (history to the right), green = +y (history below); 0.5 = no motion.
        const float2 motion = g_motion[pixel].xy;
        color = float3(saturate(0.5 + 0.5 * motion / motion_scale), 0.5);
    }
    else if (view == VIEW_DEPTH)
    {
        const float depth = g_position_depth[pixel].w;
        color = background ? float3(0.0, 0.0, 0.2) : saturate(1.0 - log2(1.0 + depth) / log2(1.0 + far_depth)).xxx;
    }
    else if (view == VIEW_NORMALS)
    {
        color = background ? 0.0.xxx : g_normal_roughness[pixel].xyz * 0.5 + 0.5;
    }
    else if (view == VIEW_DISOCCLUSION)
    {
        color = has_denoiser ? (g_denoiser_debug[pixel].x > 0.5 ? float3(1.0, 0.1, 0.1) : float3(0.1, 0.1, 0.1)) : float3(0.3, 0.0, 0.3);
    }
    else if (view == VIEW_HISTORY_LENGTH)
    {
        color = has_denoiser ? g_denoiser_debug[pixel].yyy : float3(0.3, 0.0, 0.3);
    }
    else if (view == VIEW_VARIANCE)
    {
        color = has_denoiser ? saturate(sqrt(max(g_denoiser_debug[pixel].z, 0.0))).xxx : float3(0.3, 0.0, 0.3);
    }
    else if (view == VIEW_REACTIVE)
    {
        color = has_denoiser ? (g_denoiser_debug[pixel].w > 0.5 ? float3(1.0, 0.8, 0.0) : float3(0.05, 0.05, 0.05)) : float3(0.3, 0.0, 0.3);
    }
    else if (view == VIEW_RAW_SIGNAL)
    {
        color = g_raw_signal[pixel].rgb;
    }
    g_output[pixel] = float4(color, 1.0);
}
