#include "common/Color.hlsli"

Texture2D<float4> scene_color : register(t0);

cbuffer ToneMapConstants : register(b0)
{
    float exposure_ev;
    uint diagnostic_mode;
    uint padding0;
    uint padding1;
};

struct FullscreenVertexOutput
{
    float4 position : SV_Position;
};

FullscreenVertexOutput VSMain(uint vertex_id : SV_VertexID)
{
    FullscreenVertexOutput output;
    const float2 position = vertex_id == 0u ? float2(-1.0, -1.0) :
                            vertex_id == 1u ? float2(-1.0, 3.0) : float2(3.0, -1.0);
    output.position = float4(position, 0.0, 1.0);
    return output;
}

float4 PSMain(FullscreenVertexOutput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    const float4 source = scene_color.Load(int3(pixel, 0));
    float3 linear_display;
    if (diagnostic_mode == 0u)
    {
        const float3 exposed = max(source.rgb, 0.0) * exp2(clamp(exposure_ev, -24.0, 24.0));
        linear_display = aces_fitted(exposed);
    }
    else
    {
        linear_display = saturate(source.rgb);
    }
    // Swap-chain format is UNORM rather than *_SRGB, so encode exactly once here.
    return float4(saturate(linear_to_srgb(linear_display)), 1.0);
}
