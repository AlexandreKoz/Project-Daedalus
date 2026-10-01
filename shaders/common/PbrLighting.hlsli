#ifndef DAEDALUS_PBR_LIGHTING_HLSLI
#define DAEDALUS_PBR_LIGHTING_HLSLI

static const float DAEDALUS_PI = 3.14159265358979323846;
static const float DAEDALUS_DIELECTRIC_F0 = 0.04;
static const float DAEDALUS_MIN_PERCEPTUAL_ROUGHNESS = 0.045;

float3 fresnel_schlick(float3 f0, float v_dot_h)
{
    const float factor = pow(saturate(1.0 - v_dot_h), 5.0);
    return f0 + (1.0 - f0) * factor;
}

float ggx_distribution(float n_dot_h, float alpha)
{
    const float alpha_squared = alpha * alpha;
    const float term = n_dot_h * n_dot_h * (alpha_squared - 1.0) + 1.0;
    return alpha_squared / max(DAEDALUS_PI * term * term, 1.0e-20);
}

float smith_correlated_visibility(float n_dot_v, float n_dot_l, float alpha)
{
    const float alpha_squared = alpha * alpha;
    const float lambda_v = n_dot_l * sqrt(max(n_dot_v * n_dot_v * (1.0 - alpha_squared) + alpha_squared, 0.0));
    const float lambda_l = n_dot_v * sqrt(max(n_dot_l * n_dot_l * (1.0 - alpha_squared) + alpha_squared, 0.0));
    return 0.5 / max(lambda_v + lambda_l, 1.0e-20);
}

float3 evaluate_brdf_times_n_dot_l(float3 base_color,
                                   float metallic,
                                   float perceptual_roughness,
                                   float3 normal,
                                   float3 view_direction,
                                   float3 light_direction)
{
    const float n_dot_v = saturate(dot(normal, view_direction));
    const float n_dot_l = saturate(dot(normal, light_direction));
    if (n_dot_v <= 0.0 || n_dot_l <= 0.0)
        return 0.0;

    const float3 half_sum = view_direction + light_direction;
    const float half_length_squared = dot(half_sum, half_sum);
    if (half_length_squared <= 1.0e-20)
        return 0.0;
    const float3 half_vector = half_sum * rsqrt(half_length_squared);
    const float n_dot_h = saturate(dot(normal, half_vector));
    const float v_dot_h = saturate(dot(view_direction, half_vector));

    metallic = saturate(metallic);
    perceptual_roughness = clamp(perceptual_roughness, DAEDALUS_MIN_PERCEPTUAL_ROUGHNESS, 1.0);
    const float alpha = perceptual_roughness * perceptual_roughness;
    const float3 f0 = lerp(DAEDALUS_DIELECTRIC_F0.xxx, max(base_color, 0.0), metallic);
    const float3 fresnel = fresnel_schlick(f0, v_dot_h);
    const float distribution = ggx_distribution(n_dot_h, alpha);
    const float visibility = smith_correlated_visibility(n_dot_v, n_dot_l, alpha);
    const float3 diffuse = (1.0 - fresnel) * max(base_color, 0.0) * ((1.0 - metallic) / DAEDALUS_PI);
    const float3 specular = fresnel * distribution * visibility;
    return (diffuse + specular) * n_dot_l;
}

float distance_attenuation(float distance_to_light, float range)
{
    if (distance_to_light <= 0.0)
        return 0.0;
    const float inverse_square = 1.0 / max(distance_to_light * distance_to_light, 1.0e-20);
    if (range <= 0.0)
        return inverse_square;
    if (distance_to_light >= range)
        return 0.0;
    const float normalized = distance_to_light / range;
    const float normalized_squared = normalized * normalized;
    const float window = saturate(1.0 - normalized_squared * normalized_squared);
    return inverse_square * window * window;
}

float spot_attenuation(float cos_angle, float inner_cos, float outer_cos)
{
    if (cos_angle <= outer_cos)
        return 0.0;
    if (cos_angle >= inner_cos)
        return 1.0;
    const float width = inner_cos - outer_cos;
    if (width <= 1.0e-7)
        return cos_angle >= inner_cos ? 1.0 : 0.0;
    const float t = saturate((cos_angle - outer_cos) / width);
    return t * t * (3.0 - 2.0 * t);
}

#endif
