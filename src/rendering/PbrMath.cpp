#include "rendering/PbrMath.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace daedalus
{
namespace
{
constexpr float kDielectricF0 = 0.04F;
constexpr float kMinimumPerceptualRoughness = 0.045F;
constexpr float kDirectionEpsilonSquared = 1.0e-20F;

[[nodiscard]] float clamp_unit(float value) noexcept
{
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] Vec3 add(Vec3 a, Vec3 b) noexcept
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] Vec3 multiply_components(Vec3 a, Vec3 b) noexcept
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

[[nodiscard]] Vec3 lerp(Vec3 a, Vec3 b, float t) noexcept
{
    return add(a * (1.0F - t), b * t);
}

[[nodiscard]] Vec3 one_minus(Vec3 value) noexcept
{
    return {1.0F - value.x, 1.0F - value.y, 1.0F - value.z};
}

[[nodiscard]] Vec3 fresnel_schlick(Vec3 f0, float v_dot_h) noexcept
{
    const float one_minus_cosine = clamp_unit(1.0F - v_dot_h);
    const float factor = one_minus_cosine * one_minus_cosine * one_minus_cosine * one_minus_cosine * one_minus_cosine;
    return add(f0, one_minus(f0) * factor);
}

[[nodiscard]] float ggx_distribution(float n_dot_h, float alpha) noexcept
{
    const float alpha_squared = alpha * alpha;
    const float denominator_term = n_dot_h * n_dot_h * (alpha_squared - 1.0F) + 1.0F;
    const float denominator = std::numbers::pi_v<float> * denominator_term * denominator_term;
    return denominator > 0.0F && std::isfinite(denominator) ? alpha_squared / denominator : 0.0F;
}

[[nodiscard]] float smith_correlated_visibility(float n_dot_v, float n_dot_l, float alpha) noexcept
{
    const float alpha_squared = alpha * alpha;
    const float lambda_v = n_dot_l * std::sqrt(std::max(0.0F, n_dot_v * n_dot_v * (1.0F - alpha_squared) + alpha_squared));
    const float lambda_l = n_dot_v * std::sqrt(std::max(0.0F, n_dot_l * n_dot_l * (1.0F - alpha_squared) + alpha_squared));
    const float denominator = lambda_v + lambda_l;
    return denominator > 1.0e-20F && std::isfinite(denominator) ? 0.5F / denominator : 0.0F;
}

[[nodiscard]] bool finite_nonnegative(Vec3 value) noexcept
{
    return finite(value) && value.x >= 0.0F && value.y >= 0.0F && value.z >= 0.0F;
}
}

float srgb_to_linear(float value) noexcept
{
    const float clamped = clamp_unit(value);
    if (clamped <= 0.04045F) return clamped / 12.92F;
    return std::pow((clamped + 0.055F) / 1.055F, 2.4F);
}

float linear_to_srgb(float value) noexcept
{
    const float clamped = std::max(value, 0.0F);
    if (clamped <= 0.0031308F) return 12.92F * clamped;
    return 1.055F * std::pow(clamped, 1.0F / 2.4F) - 0.055F;
}

Vec3 srgb_to_linear(Vec3 value) noexcept
{
    return {srgb_to_linear(value.x), srgb_to_linear(value.y), srgb_to_linear(value.z)};
}

Vec3 linear_to_srgb(Vec3 value) noexcept
{
    return {linear_to_srgb(value.x), linear_to_srgb(value.y), linear_to_srgb(value.z)};
}

ColorSpaceIntent texture_slot_color_space(MaterialTextureSlot slot) noexcept
{
    switch (slot)
    {
    case MaterialTextureSlot::base_color:
    case MaterialTextureSlot::emissive:
        return ColorSpaceIntent::srgb;
    case MaterialTextureSlot::metallic_roughness:
    case MaterialTextureSlot::normal:
    case MaterialTextureSlot::occlusion:
        return ColorSpaceIntent::linear;
    }
    return ColorSpaceIntent::linear;
}

Vec4 apply_base_color(Vec4 base_color_factor, Vec4 texture_sample, Vec4 vertex_color) noexcept
{
    return {
        std::max(base_color_factor.x * texture_sample.x * vertex_color.x, 0.0F),
        std::max(base_color_factor.y * texture_sample.y * vertex_color.y, 0.0F),
        std::max(base_color_factor.z * texture_sample.z * vertex_color.z, 0.0F),
        material_alpha(base_color_factor.w, texture_sample.w, vertex_color.w)};
}

Vec3 apply_emissive(Vec3 emissive_factor, Vec3 texture_sample) noexcept
{
    return {
        std::max(emissive_factor.x * texture_sample.x, 0.0F),
        std::max(emissive_factor.y * texture_sample.y, 0.0F),
        std::max(emissive_factor.z * texture_sample.z, 0.0F)};
}

float apply_occlusion(float sampled_red, float strength) noexcept
{
    return 1.0F + clamp_unit(strength) * (clamp_unit(sampled_red) - 1.0F);
}

MetallicRoughnessSample apply_metallic_roughness(Vec4 texture_sample,
                                                 float metallic_factor,
                                                 float roughness_factor) noexcept
{
    // glTF 2.0 stores roughness in G and metallic in B.
    return {
        clamp_unit(texture_sample.z * metallic_factor),
        clamp_unit(texture_sample.y * roughness_factor)};
}

float material_alpha(float factor_alpha, float texture_alpha, float vertex_alpha) noexcept
{
    return clamp_unit(factor_alpha * texture_alpha * vertex_alpha);
}

bool alpha_test_passes(AlphaMode mode, float alpha, float alpha_cutoff) noexcept
{
    if (mode != AlphaMode::mask) return true;
    return alpha >= alpha_cutoff;
}

float tangent_handedness(float source_w, bool negative_world_determinant, bool back_facing) noexcept
{
    const float source_sign = source_w < 0.0F ? -1.0F : 1.0F;
    const float world_sign = negative_world_determinant ? -1.0F : 1.0F;
    const float face_sign = back_facing ? -1.0F : 1.0F;
    return source_sign * world_sign * face_sign;
}

std::optional<Vec3> orthonormalize_tangent(Vec3 normal, Vec3 tangent) noexcept
{
    const Vec3 n = normalize(normal, {});
    if (dot(n, n) <= kDirectionEpsilonSquared) return std::nullopt;
    const Vec3 projected = tangent - n * dot(n, tangent);
    const float projected_length_squared = dot(projected, projected);
    if (!(projected_length_squared > kDirectionEpsilonSquared) || !std::isfinite(projected_length_squared))
        return std::nullopt;
    return projected / std::sqrt(projected_length_squared);
}

BrdfEvaluation evaluate_metallic_roughness_brdf(Vec3 base_color,
                                                float metallic,
                                                float perceptual_roughness,
                                                Vec3 normal,
                                                Vec3 view_direction,
                                                Vec3 light_direction) noexcept
{
    BrdfEvaluation result;
    if (!finite(base_color) || !finite(normal) || !finite(view_direction) || !finite(light_direction)) return result;

    const Vec3 n = normalize(normal, {});
    const Vec3 v = normalize(view_direction, {});
    const Vec3 l = normalize(light_direction, {});
    if (dot(n, n) <= kDirectionEpsilonSquared || dot(v, v) <= kDirectionEpsilonSquared || dot(l, l) <= kDirectionEpsilonSquared)
        return result;

    const float n_dot_v = clamp_unit(dot(n, v));
    const float n_dot_l = clamp_unit(dot(n, l));
    if (!(n_dot_v > 0.0F) || !(n_dot_l > 0.0F)) return result;

    const Vec3 half_sum = v + l;
    const float half_length_squared = dot(half_sum, half_sum);
    if (!(half_length_squared > kDirectionEpsilonSquared) || !std::isfinite(half_length_squared)) return result;
    const Vec3 h = half_sum / std::sqrt(half_length_squared);

    const float n_dot_h = clamp_unit(dot(n, h));
    const float v_dot_h = clamp_unit(dot(v, h));
    metallic = clamp_unit(metallic);
    perceptual_roughness = std::clamp(perceptual_roughness, kMinimumPerceptualRoughness, 1.0F);
    const float alpha = perceptual_roughness * perceptual_roughness;

    const Vec3 clamped_base{
        std::max(base_color.x, 0.0F),
        std::max(base_color.y, 0.0F),
        std::max(base_color.z, 0.0F)};
    const Vec3 f0 = lerp({kDielectricF0, kDielectricF0, kDielectricF0}, clamped_base, metallic);
    const Vec3 fresnel = fresnel_schlick(f0, v_dot_h);
    const float distribution = ggx_distribution(n_dot_h, alpha);
    const float visibility = smith_correlated_visibility(n_dot_v, n_dot_l, alpha);

    const Vec3 diffuse_brdf = multiply_components(one_minus(fresnel), clamped_base) *
                              ((1.0F - metallic) / std::numbers::pi_v<float>);
    const Vec3 specular_brdf = fresnel * (distribution * visibility);
    result.diffuse = diffuse_brdf * n_dot_l;
    result.specular = specular_brdf * n_dot_l;
    result.total = add(result.diffuse, result.specular);

    if (!finite_nonnegative(result.total)) return {};
    return result;
}

float point_distance_attenuation(float distance, std::optional<float> range) noexcept
{
    if (!(distance > 0.0F) || !std::isfinite(distance)) return 0.0F;
    const float inverse_square = 1.0F / (distance * distance);
    if (!std::isfinite(inverse_square)) return 0.0F;
    if (!range.has_value()) return inverse_square;
    if (!(*range > 0.0F) || !std::isfinite(*range) || distance >= *range) return 0.0F;
    const float normalized = distance / *range;
    const float normalized_squared = normalized * normalized;
    const float normalized_fourth = normalized_squared * normalized_squared;
    const float window = clamp_unit(1.0F - normalized_fourth);
    const float attenuation = inverse_square * window * window;
    return std::isfinite(attenuation) ? attenuation : 0.0F;
}

float spot_cone_attenuation(float cos_angle, float inner_cone_cos, float outer_cone_cos) noexcept
{
    if (!std::isfinite(cos_angle) || !std::isfinite(inner_cone_cos) || !std::isfinite(outer_cone_cos)) return 0.0F;
    if (cos_angle <= outer_cone_cos) return 0.0F;
    if (cos_angle >= inner_cone_cos) return 1.0F;
    const float width = inner_cone_cos - outer_cone_cos;
    if (!(width > 1.0e-7F)) return cos_angle >= inner_cone_cos ? 1.0F : 0.0F;
    const float t = clamp_unit((cos_angle - outer_cone_cos) / width);
    return t * t * (3.0F - 2.0F * t);
}

float exposure_multiplier(float exposure_ev) noexcept
{
    if (!std::isfinite(exposure_ev)) return 1.0F;
    return std::exp2(std::clamp(exposure_ev, -24.0F, 24.0F));
}

float aces_fitted(float linear_value) noexcept
{
    // Stephen Hill/Krzysztof Narkowicz-style compact ACES fit used as a display operator,
    // not a claim of full ACES color management.
    if (std::isnan(linear_value)) return 0.0F;
    if (std::isinf(linear_value)) return linear_value > 0.0F ? 1.0F : 0.0F;
    // The rational fit is already effectively asymptotic well before this cap. Limiting the
    // algebraic input prevents x*x overflow while preserving monotonic display behavior.
    const float x = std::min(std::max(linear_value, 0.0F), 1.0e10F);
    const float numerator = x * (2.51F * x + 0.03F);
    const float denominator = x * (2.43F * x + 0.59F) + 0.14F;
    if (!(denominator > 0.0F) || !std::isfinite(numerator) || !std::isfinite(denominator)) return 0.0F;
    return clamp_unit(numerator / denominator);
}

Vec3 aces_fitted(Vec3 linear_value) noexcept
{
    return {aces_fitted(linear_value.x), aces_fitted(linear_value.y), aces_fitted(linear_value.z)};
}
}
