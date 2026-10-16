#ifndef DAEDALUS_ENVIRONMENT_HLSLI
#define DAEDALUS_ENVIRONMENT_HLSLI

#include "common/PbrLighting.hlsli"

// daedalus-procedural-sky-v1. Moved verbatim from RasterPbr.hlsl so raster IBL, hybrid misses
// and the path tracer evaluate the identical analytic environment. CPU mirror:
// src/rendering/CampaignC2Math.cpp. The sun lobe constants are also used by the path tracer's
// importance sampler (src/rendering/PathTracingMath.h): direction (-0.35, 0.82, 0.45), exponent 512.
static const float3 DAEDALUS_SUN_DIRECTION_UNNORMALIZED = float3(-0.35, 0.82, 0.45);
static const float DAEDALUS_SUN_EXPONENT = 512.0;

float3 procedural_environment_radiance(float3 direction)
{
    const float3 d = normalize(direction);
    const float sky_t = saturate(d.y * 0.5 + 0.5);
    const float3 ground = float3(0.035, 0.028, 0.022);
    const float3 horizon = float3(0.26, 0.31, 0.38);
    const float3 zenith = float3(0.08, 0.18, 0.42);
    float3 environment = d.y >= 0.0 ? lerp(horizon, zenith, pow(sky_t, 0.65))
                                     : lerp(ground, horizon * 0.24, sky_t * 2.0);
    const float3 sun_direction = normalize(float3(-0.35, 0.82, 0.45));
    const float sun = pow(max(dot(d, sun_direction), 0.0), 512.0) * 18.0;
    return environment + float3(1.0, 0.82, 0.58) * sun;
}

float3 procedural_diffuse_irradiance(float3 normal)
{
    const float hemi = saturate(normalize(normal).y * 0.5 + 0.5);
    return lerp(float3(0.075, 0.060, 0.050), float3(0.34, 0.47, 0.72), hemi) * DAEDALUS_PI;
}

float3 procedural_prefiltered_specular(float3 reflection, float roughness)
{
    const float3 sharp = procedural_environment_radiance(reflection);
    const float3 average = float3(0.15, 0.19, 0.27);
    const float r = saturate(roughness);
    const float blur = r * r * (3.0 - 2.0 * r);
    return lerp(sharp, average, blur);
}

float2 environment_brdf_approximation(float n_dot_v, float roughness)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = saturate(roughness) * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * saturate(n_dot_v))) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

float3 fresnel_schlick_roughness(float3 f0, float n_dot_v, float roughness)
{
    return f0 + (max((1.0 - roughness).xxx, f0) - f0) * pow(saturate(1.0 - n_dot_v), 5.0);
}

// Split-sum specular weight (F0 * A + B) used to demodulate/remodulate ray-traced reflections so
// the denoiser filters radiance rather than BRDF-weighted values.
float3 split_sum_specular_weight(float3 base_color, float metallic, float n_dot_v, float roughness)
{
    const float3 f0 = lerp(DAEDALUS_DIELECTRIC_F0.xxx, max(base_color, 0.0), saturate(metallic));
    const float2 brdf = environment_brdf_approximation(n_dot_v, roughness);
    return f0 * brdf.x + brdf.y;
}

#endif
