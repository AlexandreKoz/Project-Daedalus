#pragma once

#include "rendering/RasterPreparation.h"
#include "scene/Math.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace daedalus
{
struct ShadowProjection
{
    bool enabled = false;
    std::size_t light_index = 0;
    LightType light_type = LightType::point;
    Mat4 view_projection = identity_matrix();
    float constant_bias = 0.0010F;
    float normal_bias = 0.0020F;
};

struct ImageComparisonThresholds
{
    float mean_absolute_error = 0.0025F;
    float root_mean_square_error = 0.0050F;
    float maximum_absolute_error = 0.05F;
    float per_channel_threshold = 0.01F;
    float maximum_fraction_over_threshold = 0.01F;
};

struct ImageComparisonMetrics
{
    float mean_absolute_error = 0.0F;
    float root_mean_square_error = 0.0F;
    float maximum_absolute_error = 0.0F;
    float fraction_over_threshold = 0.0F;
    std::uint64_t compared_channels = 0;
    std::uint64_t channels_over_threshold = 0;
    bool pass = true;
};

[[nodiscard]] float roughness_to_environment_lod(float perceptual_roughness, std::uint32_t mip_count) noexcept;
[[nodiscard]] Vec3 procedural_environment_radiance(Vec3 direction) noexcept;
[[nodiscard]] Vec3 procedural_diffuse_irradiance(Vec3 normal) noexcept;
[[nodiscard]] Vec3 procedural_prefiltered_specular(Vec3 reflection, float perceptual_roughness) noexcept;
[[nodiscard]] Vec2 environment_brdf_approximation(float n_dot_v, float perceptual_roughness) noexcept;
[[nodiscard]] ShadowProjection build_shadow_projection(const Aabb& scene_bounds,
                                                        std::span<const PreparedPunctualLight> lights) noexcept;
[[nodiscard]] ImageComparisonMetrics compare_linear_rgb(std::span<const Vec3> reference,
                                                        std::span<const Vec3> candidate,
                                                        const ImageComparisonThresholds& thresholds);
[[nodiscard]] std::vector<Vec3> rgba8_srgb_to_linear_rgb(std::span<const std::byte> rgba8);
[[nodiscard]] std::string image_comparison_metrics_json(const ImageComparisonMetrics& metrics,
                                                        const ImageComparisonThresholds& thresholds);
}
