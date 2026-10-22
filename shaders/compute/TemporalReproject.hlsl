// Campaign F temporal accumulation with explicit history rejection (SVGF-style).
//
// Signal: a linear-radiance signal (demodulated path-traced radiance or demodulated hybrid
// reflections) - never tone-mapped values. History is reprojected with the renderer-wide motion
// contract (motion = previous_pixel - current_pixel, jitter-free; z = previous linear depth) using
// a bilinear footprint whose taps are individually rejected by:
//   * out-of-bounds previous position,
//   * stable instance ID mismatch (disocclusion / different object),
//   * relative previous-depth mismatch (rigid-motion exact: expected depth comes from the motion pass),
//   * normal mismatch (cos < normal_threshold),
//   * global invalidation (g_view.history_valid == 0: cut, resize, reload, mode/setting change).
// Accepted history is optionally clipped to the current 3x3 neighbourhood mean +/- gamma*sigma in
// YCoCg (reduces ghosting at the cost of extra noise/bias), then blended with weight 1/length
// (length capped at max_history). Moments give temporal variance; short histories fall back to a
// 3x3 edge-aware spatial variance estimate.

#include "compute/ComputeCommon.hlsli"

Texture2D<float4> g_noisy : register(t0);
Texture2D<float4> g_normal_roughness : register(t1);
Texture2D<float4> g_position_depth : register(t2);
Texture2D<uint4> g_identifiers : register(t3);
Texture2D<float4> g_motion : register(t4);
Texture2D<float4> g_previous_normal_roughness : register(t5);
Texture2D<float4> g_previous_position_depth : register(t6);
Texture2D<uint4> g_previous_identifiers : register(t7);
Texture2D<float4> g_history_color : register(t8);
Texture2D<float4> g_history_moments : register(t9);
RWTexture2D<float4> g_out_history_color : register(u0);
RWTexture2D<float4> g_out_history_moments : register(u1);  // x m1, y m2, z history length, w variance
RWTexture2D<float4> g_out_filter_input : register(u2);     // rgb colour, a variance
RWTexture2D<float4> g_out_debug : register(u3);            // x disoccluded, y length/max, z variance, w reactive

float3 rgb_to_ycocg(float3 c)
{
    return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

float3 ycocg_to_rgb(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
    const uint2 pixel = dispatch_id.xy;
    if (outside_render_target(pixel))
        return;
    const uint max_history = max(g_pass0.x, 1u);
    const float clip_gamma = asfloat(g_pass0.y);
    const float normal_threshold = asfloat(g_pass0.z);
    const float depth_tolerance = asfloat(g_pass0.w);
    const int2 size = int2(g_view.render_size.xy);

    const float3 noisy = g_noisy[pixel].rgb;
    const uint4 identifiers = g_identifiers[pixel];
    const float noisy_luminance = compute_luminance(noisy);
    if (identifiers.x == 0u || !finite3(noisy))
    {
        const float3 safe = finite3(noisy) ? noisy : 0.0.xxx;
        g_out_history_color[pixel] = float4(safe, 1.0);
        g_out_history_moments[pixel] = float4(noisy_luminance, noisy_luminance * noisy_luminance, 0.0, 0.0);
        g_out_filter_input[pixel] = float4(safe, 0.0);
        g_out_debug[pixel] = float4(1.0, 0.0, 0.0, 0.0);
        return;
    }
    const float3 normal = g_normal_roughness[pixel].xyz;
    const float4 motion = g_motion[pixel];
    const float expected_previous_depth = motion.z;

    // 3x3 neighbourhood statistics of the current signal (clipping + short-history variance).
    float3 mean = 0.0;
    float3 mean_squared = 0.0;
    float spatial_weight = 0.0;
    float spatial_m1 = 0.0;
    float spatial_m2 = 0.0;
    const float center_depth = g_position_depth[pixel].w;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int2 q = clamp(int2(pixel) + int2(x, y), int2(0, 0), size - 1);
            const float3 sample_color = g_noisy[q].rgb;
            const float3 ycocg = rgb_to_ycocg(sample_color);
            mean += ycocg;
            mean_squared += ycocg * ycocg;
            const bool same_surface = g_identifiers[q].x == identifiers.x &&
                                      abs(g_position_depth[q].w - center_depth) <= 0.05 * center_depth;
            if (same_surface && finite3(sample_color))
            {
                const float l = compute_luminance(sample_color);
                spatial_m1 += l;
                spatial_m2 += l * l;
                spatial_weight += 1.0;
            }
        }
    }
    mean /= 9.0;
    const float3 sigma = sqrt(max(mean_squared / 9.0 - mean * mean, 0.0));

    // Bilinear history footprint with per-tap rejection.
    const float2 previous_position = float2(pixel) + 0.5 + motion.xy - 0.5;
    const int2 base = int2(floor(previous_position));
    const float2 fraction = previous_position - float2(base);
    float3 history = 0.0;
    float2 history_moments = 0.0;
    float history_length = 0.0;
    float weight_sum = 0.0;
    // g_pass1.x: this denoiser's own history validity (false on its first frame after creation/resize).
    const bool global_valid = g_view.history_valid != 0u && g_pass1.x != 0u && motion.w > 0.0;
    if (global_valid)
    {
        [unroll]
        for (int tap = 0; tap < 4; ++tap)
        {
            const int2 offset = int2(tap & 1, tap >> 1);
            const int2 q = base + offset;
            if (any(q < int2(0, 0)) || any(q >= size))
                continue;
            const float weight = (offset.x == 0 ? 1.0 - fraction.x : fraction.x) * (offset.y == 0 ? 1.0 - fraction.y : fraction.y);
            if (weight <= 0.0)
                continue;
            if (g_previous_identifiers[q].x != identifiers.x)
                continue;
            const float previous_depth = g_previous_position_depth[q].w;
            if (abs(previous_depth - expected_previous_depth) > depth_tolerance * max(expected_previous_depth, 1.0e-4))
                continue;
            if (dot(g_previous_normal_roughness[q].xyz, normal) < normal_threshold)
                continue;
            const float4 previous_color = g_history_color[q];
            const float4 previous_moments = g_history_moments[q];
            if (!finite3(previous_color.rgb))
                continue;
            history += previous_color.rgb * weight;
            history_moments += previous_moments.xy * weight;
            history_length += previous_moments.z * weight;
            weight_sum += weight;
        }
    }

    const bool disoccluded = weight_sum <= 1.0e-4;
    float history_count = 1.0;
    float3 color = noisy;
    float2 moments = float2(noisy_luminance, noisy_luminance * noisy_luminance);
    float reactive = 0.0;
    if (!disoccluded)
    {
        history /= weight_sum;
        history_moments /= weight_sum;
        history_length /= weight_sum;
        const float history_luminance = compute_luminance(history);
        reactive = abs(history_luminance - noisy_luminance) > 4.0 * max(sigma.x, 1.0e-4) ? 1.0 : 0.0;
        if (clip_gamma > 0.0)
        {
            const float3 box_min = mean - clip_gamma * sigma;
            const float3 box_max = mean + clip_gamma * sigma;
            history = ycocg_to_rgb(clamp(rgb_to_ycocg(history), box_min, box_max));
        }
        history_count = min(history_length + 1.0, float(max_history));
        const float alpha = 1.0 / history_count;
        const float moments_alpha = max(alpha, 0.2);
        color = lerp(history, noisy, alpha);
        moments = lerp(history_moments, moments, moments_alpha);
    }
    float variance = max(moments.y - moments.x * moments.x, 0.0);
    if (history_count < 4.0 && spatial_weight > 0.0)
    {
        const float m1 = spatial_m1 / spatial_weight;
        variance = max(max(spatial_m2 / spatial_weight - m1 * m1, 0.0), variance) * (4.0 / max(history_count, 1.0));
    }
    g_out_history_color[pixel] = float4(color, 1.0);
    g_out_history_moments[pixel] = float4(moments, history_count, variance);
    g_out_filter_input[pixel] = float4(color, variance);
    g_out_debug[pixel] = float4(disoccluded ? 1.0 : 0.0, history_count / float(max_history), variance, reactive);
}
