#pragma once

#include "scene/Scene.h"

#include <optional>

namespace daedalus
{
enum class MaterialTextureSlot
{
    base_color,
    metallic_roughness,
    normal,
    occlusion,
    emissive
};

struct MetallicRoughnessSample
{
    float metallic = 1.0F;
    float roughness = 1.0F;
};

struct BrdfEvaluation
{
    Vec3 diffuse{};
    Vec3 specular{};
    Vec3 total{};
};

[[nodiscard]] float srgb_to_linear(float value) noexcept;
[[nodiscard]] float linear_to_srgb(float value) noexcept;
[[nodiscard]] Vec3 srgb_to_linear(Vec3 value) noexcept;
[[nodiscard]] Vec3 linear_to_srgb(Vec3 value) noexcept;
[[nodiscard]] ColorSpaceIntent texture_slot_color_space(MaterialTextureSlot slot) noexcept;
[[nodiscard]] Vec4 apply_base_color(Vec4 base_color_factor, Vec4 texture_sample, Vec4 vertex_color) noexcept;
[[nodiscard]] Vec3 apply_emissive(Vec3 emissive_factor, Vec3 texture_sample) noexcept;
[[nodiscard]] float apply_occlusion(float sampled_red, float strength) noexcept;
[[nodiscard]] MetallicRoughnessSample apply_metallic_roughness(Vec4 texture_sample,
                                                               float metallic_factor,
                                                               float roughness_factor) noexcept;
[[nodiscard]] float material_alpha(float factor_alpha, float texture_alpha, float vertex_alpha) noexcept;
[[nodiscard]] bool alpha_test_passes(AlphaMode mode, float alpha, float alpha_cutoff) noexcept;
[[nodiscard]] float tangent_handedness(float source_w, bool negative_world_determinant, bool back_facing) noexcept;
[[nodiscard]] std::optional<Vec3> orthonormalize_tangent(Vec3 normal, Vec3 tangent) noexcept;

// Returns BRDF * max(N.L, 0), before multiplication by incident irradiance/radiance.
[[nodiscard]] BrdfEvaluation evaluate_metallic_roughness_brdf(Vec3 base_color,
                                                              float metallic,
                                                              float perceptual_roughness,
                                                              Vec3 normal,
                                                              Vec3 view_direction,
                                                              Vec3 light_direction) noexcept;

// glTF punctual range uses a smooth quartic window chosen by Daedalus; the KHR extension
// leaves the exact soft cutoff implementation to the renderer. A non-positive optional range
// is treated as absent by callers after canonical validation.
[[nodiscard]] float point_distance_attenuation(float distance, std::optional<float> range) noexcept;
[[nodiscard]] float spot_cone_attenuation(float cos_angle, float inner_cone_cos, float outer_cone_cos) noexcept;

[[nodiscard]] float exposure_multiplier(float exposure_ev) noexcept;
[[nodiscard]] float aces_fitted(float linear_value) noexcept;
[[nodiscard]] Vec3 aces_fitted(Vec3 linear_value) noexcept;
}
