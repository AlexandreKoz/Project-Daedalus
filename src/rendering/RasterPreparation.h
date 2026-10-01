#pragma once

#include "scene/Scene.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace daedalus
{
inline constexpr std::uint32_t kDefaultMaterialDiagnosticId = 0U;
inline constexpr std::size_t kMaximumPunctualLights = 32U;

struct PreparedTextureBinding
{
    std::optional<TextureId> texture;
    std::uint32_t texcoord_set = 0;
};

struct PreparedRasterDraw
{
    std::uint32_t primitive_index = 0;
    Mat4 world = identity_matrix();
    Vec3 world_bounds_center{};
    Vec4 base_color_factor{1.0F, 1.0F, 1.0F, 1.0F};
    Vec3 emissive_factor{};
    float metallic_factor = 1.0F;
    float roughness_factor = 1.0F;
    float normal_scale = 1.0F;
    float occlusion_strength = 1.0F;
    float alpha_cutoff = 0.5F;
    AlphaMode alpha_mode = AlphaMode::opaque;
    bool double_sided = false;
    bool negative_determinant = false;
    bool has_vertex_colors = false;
    bool has_tangents = false;
    bool normal_mapping_enabled = false;
    bool uses_default_material = true;
    std::uint32_t material_diagnostic_id = kDefaultMaterialDiagnosticId;
    PreparedTextureBinding base_color;
    PreparedTextureBinding metallic_roughness;
    PreparedTextureBinding normal;
    PreparedTextureBinding occlusion;
    PreparedTextureBinding emissive;
};

struct PreparedPunctualLight
{
    LightType type = LightType::point;
    Vec3 color{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;
    Vec3 position{};
    Vec3 direction{0.0F, 0.0F, -1.0F}; // world-space direction in which the light emits
    std::optional<float> range;
    float inner_cone_cos = 1.0F;
    float outer_cone_cos = 0.0F;
};

[[nodiscard]] std::uint32_t stable_material_diagnostic_id(MaterialId material);
[[nodiscard]] std::vector<PreparedRasterDraw> prepare_raster_draws(const CanonicalScene& scene);
[[nodiscard]] std::vector<PreparedPunctualLight> prepare_punctual_lights(const CanonicalScene& scene,
                                                                         std::size_t maximum_lights = kMaximumPunctualLights);
// OPAQUE and MASK remain in traversal order. BLEND follows, sorted far-to-near by transformed
// primitive-bounds center with deterministic tie breakers.
[[nodiscard]] std::vector<std::size_t> build_raster_draw_order(const std::vector<PreparedRasterDraw>& draws,
                                                                Vec3 camera_position);
}
