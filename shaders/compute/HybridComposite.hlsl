// Campaign D hybrid composite: raster base + ray-traced direct + remodulated (optionally denoised)
// ray-traced reflections, all in linear HDR before any exposure/tone mapping.

#include "compute/ComputeCommon.hlsli"
#include "common/Environment.hlsli"

Texture2D<float4> g_raster_base : register(t0);       // raster HDR with deferred terms omitted
Texture2D<float4> g_rt_direct : register(t1);         // rgb direct, a visible fraction
Texture2D<float4> g_rt_reflection : register(t2);     // raw demodulated reflection, a hit distance
Texture2D<float4> g_rt_reflection_filtered : register(t3); // denoised demodulated reflection
Texture2D<float4> g_normal_roughness : register(t4);
Texture2D<float4> g_albedo_metallic : register(t5);
Texture2D<float4> g_position_depth : register(t6);
Texture2D<uint4> g_identifiers : register(t7);
RWTexture2D<float4> g_output : register(u0);

static const uint COMPOSITE_SHADOWS = 1u << 0u;
static const uint COMPOSITE_REFLECTIONS = 1u << 1u;
static const uint COMPOSITE_USE_FILTERED = 1u << 2u;

static const uint DEBUG_NONE = 0u;
static const uint DEBUG_SHADOW_VISIBILITY = 1u;
static const uint DEBUG_REFLECTION = 2u;
static const uint DEBUG_REFLECTION_HIT_DISTANCE = 3u;
static const uint DEBUG_DIRECT = 4u;
static const uint DEBUG_INSTANCE_ID = 5u;

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
    const uint2 pixel = dispatch_id.xy;
    if (outside_render_target(pixel))
        return;
    const uint flags = g_pass0.x;
    const uint debug_view = g_pass0.y;
    const float hit_distance_scale = asfloat(g_pass0.z);
    const float4 base = g_raster_base[pixel];
    const uint4 identifiers = g_identifiers[pixel];
    const bool eligible = identifiers.x != 0u && ((identifiers.w >> 16u) & GBUFFER_FLAG_HYBRID_ELIGIBLE) != 0u;

    float3 direct = 0.0;
    float visibility = 1.0;
    float3 reflection = 0.0;
    float hit_distance = -1.0;
    if (eligible)
    {
        const float4 rt_direct = g_rt_direct[pixel];
        if ((flags & COMPOSITE_SHADOWS) != 0u)
        {
            direct = rt_direct.rgb;
            visibility = rt_direct.a;
        }
        const float4 normal_roughness = g_normal_roughness[pixel];
        if ((flags & COMPOSITE_REFLECTIONS) != 0u && normal_roughness.w <= g_view.reflection_max_roughness)
        {
            const float4 albedo_metallic = g_albedo_metallic[pixel];
            const float3 position = g_position_depth[pixel].xyz;
            const float3 view_direction = normalize(g_view.camera_position_near.xyz - position);
            const float n_dot_v = saturate(dot(normalize(normal_roughness.xyz), view_direction));
            const float4 raw = g_rt_reflection[pixel];
            const float3 demodulated = (flags & COMPOSITE_USE_FILTERED) != 0u ? g_rt_reflection_filtered[pixel].rgb : raw.rgb;
            reflection = demodulated * split_sum_specular_weight(albedo_metallic.rgb, albedo_metallic.a, n_dot_v, normal_roughness.w);
            hit_distance = raw.a;
        }
    }

    float4 result = float4(base.rgb + direct + reflection, base.a);
    if (debug_view == DEBUG_SHADOW_VISIBILITY)
        result = float4(eligible ? visibility.xxx : float3(0.0, 0.0, 0.25), 1.0);
    else if (debug_view == DEBUG_REFLECTION)
        result = float4(reflection, 1.0);
    else if (debug_view == DEBUG_REFLECTION_HIT_DISTANCE)
        result = float4(hit_distance < 0.0 ? float3(0.1, 0.0, 0.2) : saturate(hit_distance * hit_distance_scale).xxx, 1.0);
    else if (debug_view == DEBUG_DIRECT)
        result = float4(direct, 1.0);
    else if (debug_view == DEBUG_INSTANCE_ID)
        result = float4(identifier_color(identifiers.x), 1.0);
    g_output[pixel] = result;
}
