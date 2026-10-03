#include "rendering/CampaignC2Math.h"

#include "rendering/PbrMath.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace daedalus
{
namespace
{
constexpr float kPi = 3.14159265358979323846F;

[[nodiscard]] Vec3 mix(Vec3 a, Vec3 b, float t) noexcept
{
    return a * (1.0F - t) + b * t;
}

[[nodiscard]] Vec3 safe_up(Vec3 direction) noexcept
{
    return std::abs(direction.y) > 0.95F ? Vec3{1.0F, 0.0F, 0.0F} : Vec3{0.0F, 1.0F, 0.0F};
}

[[nodiscard]] Mat4 orthographic_off_center_rh(float left, float right, float bottom, float top,
                                               float near_plane, float far_plane) noexcept
{
    Mat4 result = identity_matrix();
    const float width = std::max(right - left, 1.0e-5F);
    const float height = std::max(top - bottom, 1.0e-5F);
    const float depth = std::max(far_plane - near_plane, 1.0e-5F);
    result.at(0, 0) = 2.0F / width;
    result.at(1, 1) = 2.0F / height;
    result.at(2, 2) = -1.0F / depth;
    result.at(0, 3) = -(right + left) / width;
    result.at(1, 3) = -(top + bottom) / height;
    result.at(2, 3) = -near_plane / depth;
    return result;
}

[[nodiscard]] Aabb bounds_in_space(const Aabb& bounds, const Mat4& transform) noexcept
{
    return transform_bounds(bounds, transform);
}
}

float roughness_to_environment_lod(float perceptual_roughness, std::uint32_t mip_count) noexcept
{
    if (mip_count <= 1U) return 0.0F;
    const float r = std::clamp(perceptual_roughness, 0.0F, 1.0F);
    return r * static_cast<float>(mip_count - 1U);
}

Vec3 procedural_environment_radiance(Vec3 direction) noexcept
{
    const Vec3 d = normalize(direction, {0.0F, 1.0F, 0.0F});
    const float sky_t = std::clamp(d.y * 0.5F + 0.5F, 0.0F, 1.0F);
    const Vec3 ground{0.035F, 0.028F, 0.022F};
    const Vec3 horizon{0.26F, 0.31F, 0.38F};
    const Vec3 zenith{0.08F, 0.18F, 0.42F};
    Vec3 environment = d.y >= 0.0F ? mix(horizon, zenith, std::pow(sky_t, 0.65F))
                                    : mix(ground, horizon * 0.24F, sky_t * 2.0F);
    const Vec3 sun_direction = normalize(Vec3{-0.35F, 0.82F, 0.45F});
    const float sun = std::pow(std::max(dot(d, sun_direction), 0.0F), 512.0F) * 18.0F;
    environment = environment + Vec3{1.0F, 0.82F, 0.58F} * sun;
    return environment;
}

Vec3 procedural_diffuse_irradiance(Vec3 normal) noexcept
{
    // Deterministic low-order hemispherical approximation for the self-authored sky.
    // It intentionally excludes the narrow sun lobe; punctual/direct light remains separate.
    const Vec3 n = normalize(normal, {0.0F, 1.0F, 0.0F});
    const float hemi = std::clamp(n.y * 0.5F + 0.5F, 0.0F, 1.0F);
    const Vec3 ground{0.075F, 0.060F, 0.050F};
    const Vec3 sky{0.34F, 0.47F, 0.72F};
    return mix(ground, sky, hemi) * kPi;
}

Vec3 procedural_prefiltered_specular(Vec3 reflection, float perceptual_roughness) noexcept
{
    const Vec3 sharp = procedural_environment_radiance(reflection);
    const Vec3 average{0.15F, 0.19F, 0.27F};
    const float r = std::clamp(perceptual_roughness, 0.0F, 1.0F);
    // The polynomial approximates the energy-preserving broadening of the generated environment.
    const float blur = r * r * (3.0F - 2.0F * r);
    return mix(sharp, average, blur);
}

Vec2 environment_brdf_approximation(float n_dot_v, float perceptual_roughness) noexcept
{
    // Split-sum approximation from the well-known real-time environment BRDF fit.
    // The returned x/y terms are used as F0*x + y.
    const float ndv = std::clamp(n_dot_v, 0.0F, 1.0F);
    const float roughness = std::clamp(perceptual_roughness, 0.0F, 1.0F);
    const float c0x = -1.0F;
    const float c0y = -0.0275F;
    const float c0z = -0.572F;
    const float c0w = 0.022F;
    const float c1x = 1.0F;
    const float c1y = 0.0425F;
    const float c1z = 1.04F;
    const float c1w = -0.04F;
    const float rx = roughness * c0x + c1x;
    const float ry = roughness * c0y + c1y;
    const float rz = roughness * c0z + c1z;
    const float rw = roughness * c0w + c1w;
    const float a004 = std::min(rx * rx, std::exp2(-9.28F * ndv)) * rx + ry;
    return {-1.04F * a004 + rz, 1.04F * a004 + rw};
}

ShadowProjection build_shadow_projection(const Aabb& scene_bounds,
                                         std::span<const PreparedPunctualLight> lights) noexcept
{
    ShadowProjection result;
    if (scene_bounds.empty()) return result;
    std::size_t selected = lights.size();
    for (std::size_t i = 0; i < lights.size(); ++i)
    {
        if (lights[i].type == LightType::directional || lights[i].type == LightType::spot)
        {
            selected = i;
            break;
        }
    }
    if (selected == lights.size()) return result;

    const PreparedPunctualLight& light = lights[selected];
    result.enabled = true;
    result.light_index = selected;
    result.light_type = light.type;

    if (light.type == LightType::directional)
    {
        const Vec3 scene_center = center(scene_bounds);
        const float diagonal = std::max(length(extent(scene_bounds)), 0.5F);
        const Vec3 direction = normalize(light.direction, {0.0F, -1.0F, 0.0F});
        const Vec3 eye = scene_center - direction * (diagonal * 1.5F + 1.0F);
        const Mat4 view = look_at_rh(eye, scene_center, safe_up(direction));
        Aabb light_bounds = bounds_in_space(scene_bounds, view);
        const float margin = std::max(diagonal * 0.05F, 0.02F);
        const float left = light_bounds.minimum.x - margin;
        const float right = light_bounds.maximum.x + margin;
        const float bottom = light_bounds.minimum.y - margin;
        const float top = light_bounds.maximum.y + margin;
        const float near_plane = std::max(0.01F, -light_bounds.maximum.z - margin);
        const float far_plane = std::max(near_plane + 0.05F, -light_bounds.minimum.z + margin);
        result.view_projection = multiply(orthographic_off_center_rh(left, right, bottom, top, near_plane, far_plane), view);
    }
    else
    {
        const Vec3 direction = normalize(light.direction, {0.0F, 0.0F, -1.0F});
        const Mat4 view = look_at_rh(light.position, light.position + direction, safe_up(direction));
        const Vec3 dimensions = extent(scene_bounds);
        const float diagonal = std::max(length(dimensions), 0.5F);
        const float near_plane = 0.02F;
        const float range = light.range.has_value() ? *light.range : diagonal * 4.0F;
        const float far_plane = std::max(range, near_plane + 0.05F);
        const float outer_cos = std::clamp(light.outer_cone_cos, -1.0F, 1.0F);
        const float outer_angle = std::acos(outer_cos);
        const float fov = std::clamp(outer_angle * 2.0F, 0.05F, kPi - 0.05F);
        result.view_projection = multiply(perspective_rh(fov, 1.0F, near_plane, far_plane), view);
    }
    return result;
}

ImageComparisonMetrics compare_linear_rgb(std::span<const Vec3> reference,
                                          std::span<const Vec3> candidate,
                                          const ImageComparisonThresholds& thresholds)
{
    if (reference.size() != candidate.size())
        throw std::invalid_argument("image comparison requires equal pixel counts");
    ImageComparisonMetrics metrics;
    if (reference.empty()) return metrics;
    double absolute_sum = 0.0;
    double squared_sum = 0.0;
    double maximum = 0.0;
    std::uint64_t above = 0;
    for (std::size_t i = 0; i < reference.size(); ++i)
    {
        if (!finite(reference[i]) || !finite(candidate[i]))
            throw std::invalid_argument("image comparison input contains NaN or infinity");
        const float ref[3]{reference[i].x, reference[i].y, reference[i].z};
        const float got[3]{candidate[i].x, candidate[i].y, candidate[i].z};
        for (std::size_t channel = 0; channel < 3U; ++channel)
        {
            const double difference = std::abs(static_cast<double>(ref[channel]) - static_cast<double>(got[channel]));
            absolute_sum += difference;
            squared_sum += difference * difference;
            maximum = std::max(maximum, difference);
            if (difference > thresholds.per_channel_threshold) ++above;
        }
    }
    metrics.compared_channels = static_cast<std::uint64_t>(reference.size()) * 3ULL;
    metrics.channels_over_threshold = above;
    const double denominator = static_cast<double>(metrics.compared_channels);
    metrics.mean_absolute_error = static_cast<float>(absolute_sum / denominator);
    metrics.root_mean_square_error = static_cast<float>(std::sqrt(squared_sum / denominator));
    metrics.maximum_absolute_error = static_cast<float>(maximum);
    metrics.fraction_over_threshold = static_cast<float>(static_cast<double>(above) / denominator);
    metrics.pass = metrics.mean_absolute_error <= thresholds.mean_absolute_error &&
                   metrics.root_mean_square_error <= thresholds.root_mean_square_error &&
                   metrics.maximum_absolute_error <= thresholds.maximum_absolute_error &&
                   metrics.fraction_over_threshold <= thresholds.maximum_fraction_over_threshold;
    return metrics;
}

std::vector<Vec3> rgba8_srgb_to_linear_rgb(std::span<const std::byte> rgba8)
{
    if (rgba8.size() % 4U != 0U) throw std::invalid_argument("RGBA8 input byte count must be divisible by four");
    std::vector<Vec3> result;
    result.reserve(rgba8.size() / 4U);
    for (std::size_t i = 0; i < rgba8.size(); i += 4U)
    {
        const float r = static_cast<float>(std::to_integer<unsigned int>(rgba8[i + 0U])) / 255.0F;
        const float g = static_cast<float>(std::to_integer<unsigned int>(rgba8[i + 1U])) / 255.0F;
        const float b = static_cast<float>(std::to_integer<unsigned int>(rgba8[i + 2U])) / 255.0F;
        result.push_back(srgb_to_linear(Vec3{r, g, b}));
    }
    return result;
}

std::string image_comparison_metrics_json(const ImageComparisonMetrics& metrics,
                                          const ImageComparisonThresholds& thresholds)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(8)
           << "{\n"
           << "  \"status\": \"" << (metrics.pass ? "PASS" : "FAIL") << "\",\n"
           << "  \"domain\": \"linear-srgb-rgb\",\n"
           << "  \"mean_absolute_error\": " << metrics.mean_absolute_error << ",\n"
           << "  \"root_mean_square_error\": " << metrics.root_mean_square_error << ",\n"
           << "  \"maximum_absolute_error\": " << metrics.maximum_absolute_error << ",\n"
           << "  \"fraction_over_threshold\": " << metrics.fraction_over_threshold << ",\n"
           << "  \"compared_channels\": " << metrics.compared_channels << ",\n"
           << "  \"channels_over_threshold\": " << metrics.channels_over_threshold << ",\n"
           << "  \"thresholds\": {\n"
           << "    \"mean_absolute_error\": " << thresholds.mean_absolute_error << ",\n"
           << "    \"root_mean_square_error\": " << thresholds.root_mean_square_error << ",\n"
           << "    \"maximum_absolute_error\": " << thresholds.maximum_absolute_error << ",\n"
           << "    \"per_channel_threshold\": " << thresholds.per_channel_threshold << ",\n"
           << "    \"maximum_fraction_over_threshold\": " << thresholds.maximum_fraction_over_threshold << "\n"
           << "  }\n"
           << "}\n";
    return stream.str();
}
}
