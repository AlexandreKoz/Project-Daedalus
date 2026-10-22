// Campaign F edge-aware a-trous wavelet filter (Dammertz et al. 2010 with SVGF guidance,
// Schied et al. 2017). One iteration per dispatch with step 2^i; 5x5 B3-spline kernel; edge stops:
//   depth     exp(-|z_q - z_p| / (sigma_z * |grad z| * |offset| + eps))
//   normal    max(0, n_p . n_q)^sigma_n
//   luminance exp(-|l_q - l_p| / (sigma_l * sqrt(filtered variance) + eps))
//   identity  stable instance IDs must match (no cross-object leaking).
// Variance is propagated with squared weights. Known trade-offs: over-blurring of contact detail
// and glossy reflections, residual ghosting on fast-changing lighting (see TemporalReproject).

#include "compute/ComputeCommon.hlsli"

Texture2D<float4> g_source : register(t0);          // rgb colour, a variance
Texture2D<float4> g_normal_roughness : register(t1);
Texture2D<float4> g_position_depth : register(t2);
Texture2D<uint4> g_identifiers : register(t3);
RWTexture2D<float4> g_destination : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
    const uint2 pixel = dispatch_id.xy;
    if (outside_render_target(pixel))
        return;
    const int step_size = int(max(g_pass0.x, 1u));
    const float sigma_luminance = asfloat(g_pass0.y);
    const float sigma_normal = asfloat(g_pass0.z);
    const float sigma_depth = asfloat(g_pass0.w);
    const int2 size = int2(g_view.render_size.xy);

    const float4 center = g_source[pixel];
    const uint center_id = g_identifiers[pixel].x;
    if (center_id == 0u)
    {
        g_destination[pixel] = center;
        return;
    }
    const float3 center_normal = g_normal_roughness[pixel].xyz;
    const float center_depth = g_position_depth[pixel].w;
    const float center_luminance = compute_luminance(center.rgb);

    // Gaussian 3x3 pre-filter of the variance for a stable luminance edge-stop.
    float filtered_variance = 0.0;
    float variance_weight = 0.0;
    [unroll]
    for (int vy = -1; vy <= 1; ++vy)
    {
        [unroll]
        for (int vx = -1; vx <= 1; ++vx)
        {
            const int2 q = clamp(int2(pixel) + int2(vx, vy), int2(0, 0), size - 1);
            const float w = (vx == 0 ? 0.5 : 0.25) * (vy == 0 ? 0.5 : 0.25);
            filtered_variance += g_source[q].a * w;
            variance_weight += w;
        }
    }
    filtered_variance /= variance_weight;

    const int2 right = clamp(int2(pixel) + int2(1, 0), int2(0, 0), size - 1);
    const int2 down = clamp(int2(pixel) + int2(0, 1), int2(0, 0), size - 1);
    const float depth_gradient = max(max(abs(g_position_depth[right].w - center_depth), abs(g_position_depth[down].w - center_depth)), 1.0e-6);
    const float luminance_scale = sigma_luminance * sqrt(max(filtered_variance, 0.0)) + 1.0e-6;
    const float kernel[3] = {3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0};

    float3 color_sum = center.rgb;
    float variance_sum = center.a;
    float weight_sum = 1.0;
    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            if (x == 0 && y == 0)
                continue;
            const int2 q = int2(pixel) + int2(x, y) * step_size;
            if (any(q < int2(0, 0)) || any(q >= size))
                continue;
            if (g_identifiers[q].x != center_id)
                continue;
            const float4 sample_value = g_source[q];
            const float sample_depth = g_position_depth[q].w;
            const float tap_distance = length(float2(x, y) * float(step_size));
            const float w_depth = exp(-abs(sample_depth - center_depth) / (sigma_depth * depth_gradient * tap_distance + 1.0e-6));
            const float w_normal = pow(saturate(dot(center_normal, g_normal_roughness[q].xyz)), sigma_normal);
            const float w_luminance = exp(-abs(compute_luminance(sample_value.rgb) - center_luminance) / luminance_scale);
            const float w = kernel[abs(x)] * kernel[abs(y)] * w_depth * w_normal * w_luminance;
            if (!(w > 0.0) || !finite3(sample_value.rgb))
                continue;
            color_sum += sample_value.rgb * w;
            variance_sum += sample_value.a * w * w;
            weight_sum += w;
        }
    }
    g_destination[pixel] = float4(color_sum / weight_sum, variance_sum / (weight_sum * weight_sum));
}
