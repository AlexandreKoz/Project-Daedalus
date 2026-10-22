// Campaign D hybrid ray-tracing library (DXIL lib_6_3).
//
// Inputs are the raster primary surface (G-buffer) written by RasterPbr.hlsl PSMainGBuffer with
// the SAME material evaluation as the forward path. For every eligible OPAQUE/MASK pixel:
//  * direct lighting from every punctual light is re-evaluated with the raster BRDF and a real
//    DXR visibility ray (alpha-tested any-hit, BLEND excluded as in raster shadow casting);
//  * for roughness <= reflection_max_roughness one GGX VNDF reflection ray is traced; the hit is
//    shaded with direct lighting (shadow rays), emission and the procedural IBL (declared limit:
//    exactly ONE ray-traced specular bounce; deeper bounces use the analytic environment).
// Outputs are linear HDR: u0 = RT direct lighting (a = fraction of visible light samples),
// u1 = reflection radiance demodulated by the split-sum weight (a = hit distance, -1 = miss/none),
// u2 = counters.

#include "raytracing/RayTracingCommon.hlsli"

Texture2D<float4> g_normal_roughness : register(t5);
Texture2D<float4> g_albedo_metallic : register(t6);
Texture2D<float4> g_position_depth : register(t7);
Texture2D<uint4> g_identifiers : register(t8);
RWTexture2D<float4> g_out_direct : register(u0);
RWTexture2D<float4> g_out_reflection : register(u1);
RWByteAddressBuffer g_counters : register(u2);

static const uint HYBRID_RNG_SEED = 0x48594252u; // "HYBR"

void count_wave(uint counter, uint value)
{
    const uint total = WaveActiveSum(value);
    if (WaveIsFirstLane() && total != 0u)
        g_counters.InterlockedAdd(counter * 4u, total);
}

// Direct lighting at a world position using the raster BRDF and DXR visibility.
float3 shade_direct(float3 position, float3 geometric_normal, float3 shading_normal, float3 view_direction,
                    float3 base_color, float metallic, float roughness, uint rng_seed, inout uint visible_count,
                    inout uint tested_count)
{
    float3 direct = 0.0;
    const float3 origin = offset_ray_origin(position, geometric_normal);
    [loop]
    for (uint index = 0u; index < min(g_pass.light_count, 32u); ++index)
    {
        float3 light_direction;
        float light_distance;
        float3 radiance;
        if (!evaluate_punctual_light(index, position, light_direction, light_distance, radiance))
            continue;
        const float3 brdf = evaluate_brdf_times_n_dot_l(base_color, metallic, roughness, shading_normal, view_direction, light_direction);
        if (!any(brdf > 0.0) || dot(geometric_normal, light_direction) <= 0.0)
            continue;
        ++tested_count;
        const float max_distance = g_lights[index].type == 0u ? g_pass.ray_extent : light_distance * (1.0 - 1.0e-4);
        if (trace_visibility(origin, light_direction, max_distance, MASK_OPAQUE_OR_MASKED, rng_seed ^ index))
        {
            ++visible_count;
            direct += brdf * radiance;
        }
    }
    return direct;
}

// Radiance leaving a reflection hit toward the reflecting surface. Single ray-traced bounce: the
// hit is shaded with RT direct light, emission, and the analytic IBL approximation (AO applied to
// indirect only, as in raster). Transmission at the hit is not traced further (declared).
float3 shade_reflection_hit(RadiancePayload payload, float3 incoming_direction, uint rng_seed)
{
    SurfaceHit hit = reconstruct_surface(payload.instance_index, payload.primitive_index, payload.barycentrics,
                                         (payload.flags & PAYLOAD_FRONT_FACE) != 0u);
    const MaterialSample material = evaluate_material(hit);
    const float3 view_direction = -incoming_direction;
    uint visible = 0u;
    uint tested = 0u;
    float3 radiance = material.emissive;
    // Reflection hits always use ray-traced visibility: the shadow toggle only selects whether the
    // PRIMARY surface's direct light is ray traced or left to the raster shadow-map path.
    radiance += shade_direct(hit.position, hit.geometric_normal, hit.shading_normal, view_direction, material.base_color,
                                 material.metallic, material.roughness, rng_seed, visible, tested);
    const float n_dot_v = saturate(dot(hit.shading_normal, view_direction));
    const float3 f0 = lerp(DAEDALUS_DIELECTRIC_F0.xxx, material.base_color, material.metallic);
    const float3 fresnel_ibl = fresnel_schlick_roughness(f0, n_dot_v, material.roughness);
    const float3 diffuse_ibl = procedural_diffuse_irradiance(hit.shading_normal) * material.base_color * (1.0 - material.metallic) * (1.0 - fresnel_ibl);
    const float3 prefiltered = procedural_prefiltered_specular(reflect(-view_direction, hit.shading_normal), material.roughness);
    const float2 brdf = environment_brdf_approximation(n_dot_v, material.roughness);
    radiance += (diffuse_ibl + prefiltered * (f0 * brdf.x + brdf.y)) * material.occlusion * max(g_view.environment_intensity, 0.0);
    return radiance;
}

[shader("raygeneration")]
void HybridRayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint4 identifiers = g_identifiers[pixel];
    uint shadow_rays = 0u;
    uint reflection_rays = 0u;
    uint nonfinite = 0u;
    if (identifiers.x == 0u)
    {
        g_out_direct[pixel] = 0.0;
        g_out_reflection[pixel] = float4(0.0, 0.0, 0.0, -1.0);
        count_wave(COUNTER_VISIBILITY_RAYS, 0u);
        count_wave(COUNTER_REFLECTION_RAYS, 0u);
        count_wave(COUNTER_NONFINITE_RADIANCE, 0u);
        return;
    }
    const float4 normal_roughness = g_normal_roughness[pixel];
    const float4 albedo_metallic = g_albedo_metallic[pixel];
    const float3 position = g_position_depth[pixel].xyz;
    const float3 shading_normal = normalize(normal_roughness.xyz);
    const float roughness = normal_roughness.w;
    const float3 base_color = albedo_metallic.rgb;
    const float metallic = albedo_metallic.a;
    const float3 geometric_normal = decode_octahedral_normal(identifiers.z);
    const bool eligible = ((identifiers.w >> 16u) & GBUFFER_FLAG_HYBRID_ELIGIBLE) != 0u;
    const float3 view_direction = normalize(g_view.camera_position_near.xyz - position);
    const uint pixel_seed = pcg_hash(pixel.x ^ pcg_hash(pixel.y ^ pcg_hash(g_view.frame_index ^ HYBRID_RNG_SEED)));

    float4 direct_output = 0.0;
    if (eligible && (g_pass.flags & PASS_SHADOWS) != 0u)
    {
        uint visible = 0u;
        uint tested = 0u;
        const float3 direct = shade_direct(position, geometric_normal, shading_normal, view_direction, base_color, metallic,
                                           roughness, pixel_seed, visible, tested);
        shadow_rays += tested;
        direct_output = float4(direct, tested == 0u ? 1.0 : float(visible) / float(tested));
    }

    float4 reflection_output = float4(0.0, 0.0, 0.0, -1.0);
    if (eligible && (g_pass.flags & PASS_REFLECTIONS) != 0u && roughness <= g_pass.reflection_max_roughness)
    {
        PathRng rng = make_path_rng(pixel, g_view.frame_index, HYBRID_RNG_SEED);
        const float alpha = max(roughness, DAEDALUS_MIN_PERCEPTUAL_ROUGHNESS) * max(roughness, DAEDALUS_MIN_PERCEPTUAL_ROUGHNESS);
        float3 tangent;
        float3 bitangent;
        orthonormal_basis(shading_normal, tangent, bitangent);
        const float3 view_local = to_local(view_direction, tangent, bitangent, shading_normal);
        float3 reflection = 0.0;
        float hit_distance = -1.0;
        if (view_local.z > 0.0)
        {
            const float3 half_local = sample_ggx_vndf(view_local, alpha, rng_next_float2(rng));
            const float3 half_vector = to_world(half_local, tangent, bitangent, shading_normal);
            const float3 light_direction = reflect(-view_direction, half_vector);
            const float n_dot_l = dot(shading_normal, light_direction);
            if (n_dot_l > 0.0 && dot(geometric_normal, light_direction) > 0.0)
            {
                ++reflection_rays;
                const RadiancePayload payload = trace_radiance(offset_ray_origin(position, geometric_normal), light_direction,
                                                               g_pass.ray_extent, MASK_ALL, rng_next_uint(rng));
                float3 incoming;
                if ((payload.flags & PAYLOAD_HIT) != 0u)
                {
                    incoming = shade_reflection_hit(payload, light_direction, rng_next_uint(rng));
                    hit_distance = payload.hit_t;
                }
                else
                {
                    incoming = environment_radiance(light_direction);
                }
                // VNDF estimator of the specular lobe: f * cos / pdf = F(v.h) * G2 / G1(v).
                const float n_dot_v = view_local.z;
                const float v_dot_h = saturate(dot(view_direction, half_vector));
                const float3 f0 = lerp(DAEDALUS_DIELECTRIC_F0.xxx, base_color, metallic);
                const float3 fresnel = fresnel_schlick(f0, v_dot_h);
                const float g2 = smith_correlated_visibility(n_dot_v, n_dot_l, alpha) * 4.0 * n_dot_v * n_dot_l;
                const float g1 = smith_g1_ggx(n_dot_v, alpha);
                const float3 weighted = incoming * fresnel * (g1 > 0.0 ? g2 / g1 : 0.0);
                const float3 demodulation = max(split_sum_specular_weight(base_color, metallic, n_dot_v, roughness), 1.0e-4);
                reflection = weighted / demodulation;
            }
        }
        if (!all_finite(reflection))
        {
            reflection = 0.0;
            ++nonfinite;
        }
        reflection_output = float4(reflection, hit_distance);
    }
    if (!all_finite(direct_output.rgb))
    {
        direct_output = 0.0;
        ++nonfinite;
    }
    g_out_direct[pixel] = direct_output;
    g_out_reflection[pixel] = reflection_output;
    count_wave(COUNTER_VISIBILITY_RAYS, shadow_rays);
    count_wave(COUNTER_REFLECTION_RAYS, reflection_rays);
    count_wave(COUNTER_NONFINITE_RADIANCE, nonfinite);
}
