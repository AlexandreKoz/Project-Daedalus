#include "rendering/RasterPreparation.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace daedalus
{
namespace
{
[[nodiscard]] PreparedTextureBinding binding_from(const std::optional<TextureReference>& reference)
{
    PreparedTextureBinding binding;
    if (reference.has_value())
    {
        binding.texture = reference->texture;
        binding.texcoord_set = reference->texcoord_set;
    }
    return binding;
}

void validate_binding(const CanonicalScene& scene,
                      const Primitive& primitive,
                      const PreparedTextureBinding& binding,
                      const char* slot)
{
    if (!binding.texture.has_value()) return;
    if (!binding.texture->valid() || binding.texture->value() >= scene.textures.size())
        throw std::runtime_error(std::string("canonical material has invalid ") + slot + " texture reference");
    const bool available = binding.texcoord_set == 0U ? primitive.has_texcoord0 : primitive.has_texcoord1;
    if (binding.texcoord_set > 1U || !available)
        throw std::runtime_error(std::string("canonical material ") + slot + " texture references an unavailable UV set");
}

[[nodiscard]] float distance_squared(Vec3 a, Vec3 b) noexcept
{
    const Vec3 delta = a - b;
    return dot(delta, delta);
}
}

std::uint32_t stable_material_diagnostic_id(MaterialId material)
{
    if (!material.valid()) return kDefaultMaterialDiagnosticId;
    if (material.value() == std::numeric_limits<std::uint32_t>::max() - 1U)
        throw std::overflow_error("source material identifier cannot be encoded for diagnostics");
    return material.value() + 1U;
}

std::vector<PreparedRasterDraw> prepare_raster_draws(const CanonicalScene& scene)
{
    if (!scene.selected_scene.valid() || scene.selected_scene.value() >= scene.scenes.size())
        throw std::runtime_error("canonical scene has no valid selected scene");

    std::vector<PreparedRasterDraw> result;
    std::function<void(NodeId)> visit = [&](NodeId id)
    {
        if (!id.valid() || id.value() >= scene.nodes.size())
            throw std::runtime_error("selected scene references an invalid node");
        const Node& node = scene.nodes[id.value()];
        if (node.mesh.valid())
        {
            if (node.mesh.value() >= scene.meshes.size())
                throw std::runtime_error("canonical node references an invalid mesh");
            for (const PrimitiveId primitive_id : scene.meshes[node.mesh.value()].primitives)
            {
                if (!primitive_id.valid() || primitive_id.value() >= scene.primitives.size())
                    throw std::runtime_error("canonical mesh references an invalid primitive");
                const Primitive& primitive = scene.primitives[primitive_id.value()];
                const bool source_material = primitive.material.valid();
                if (source_material && primitive.material.value() >= scene.materials.size())
                    throw std::runtime_error("canonical primitive references an invalid material");
                const Material& material = source_material ? scene.materials[primitive.material.value()] : scene.default_material;

                PreparedRasterDraw draw;
                draw.primitive_index = primitive_id.value();
                draw.world = node.world_transform;
                draw.world_bounds_center = primitive.bounds.empty()
                    ? transform_point(node.world_transform, {})
                    : transform_point(node.world_transform, center(primitive.bounds));
                draw.base_color_factor = material.base_color_factor;
                draw.emissive_factor = material.emissive_factor;
                draw.metallic_factor = material.metallic_factor;
                draw.roughness_factor = material.roughness_factor;
                draw.normal_scale = material.normal_scale;
                draw.occlusion_strength = material.occlusion_strength;
                draw.alpha_cutoff = material.alpha_cutoff;
                draw.alpha_mode = material.alpha_mode;
                draw.double_sided = material.double_sided;
                draw.negative_determinant = node.negative_determinant;
                draw.has_vertex_colors = primitive.has_colors;
                draw.has_tangents = primitive.has_tangents;
                draw.uses_default_material = !source_material;
                draw.material_diagnostic_id = stable_material_diagnostic_id(primitive.material);
                draw.base_color = binding_from(material.base_color_texture);
                draw.metallic_roughness = binding_from(material.metallic_roughness_texture);
                draw.normal = binding_from(material.normal_texture);
                draw.occlusion = binding_from(material.occlusion_texture);
                draw.emissive = binding_from(material.emissive_texture);

                validate_binding(scene, primitive, draw.base_color, "base-color");
                validate_binding(scene, primitive, draw.metallic_roughness, "metallic-roughness");
                validate_binding(scene, primitive, draw.normal, "normal");
                validate_binding(scene, primitive, draw.occlusion, "occlusion");
                validate_binding(scene, primitive, draw.emissive, "emissive");
                draw.normal_mapping_enabled = draw.normal.texture.has_value() && primitive.has_tangents;
                result.push_back(draw);
            }
        }
        for (const NodeId child : node.children) visit(child);
    };
    for (const NodeId root : scene.scenes[scene.selected_scene.value()].roots) visit(root);
    return result;
}

std::vector<PreparedPunctualLight> prepare_punctual_lights(const CanonicalScene& scene, std::size_t maximum_lights)
{
    if (!scene.selected_scene.valid() || scene.selected_scene.value() >= scene.scenes.size())
        throw std::runtime_error("canonical scene has no valid selected scene");
    if (maximum_lights == 0U)
        throw std::invalid_argument("punctual light limit must be nonzero");

    std::vector<PreparedPunctualLight> result;
    std::function<void(NodeId)> visit = [&](NodeId id)
    {
        if (!id.valid() || id.value() >= scene.nodes.size())
            throw std::runtime_error("selected scene references an invalid node while preparing lights");
        const Node& node = scene.nodes[id.value()];
        if (node.light.valid())
        {
            if (node.light.value() >= scene.lights.size())
                throw std::runtime_error("canonical node references an invalid punctual light");
            if (result.size() >= maximum_lights)
                throw std::runtime_error("selected scene exceeds the configured punctual-light limit of " + std::to_string(maximum_lights));
            const Light& light = scene.lights[node.light.value()];
            PreparedPunctualLight prepared;
            prepared.type = light.type;
            prepared.color = light.color;
            prepared.intensity = light.intensity;
            prepared.position = transform_point(node.world_transform, {});
            const Vec3 transformed_direction = transform_vector(node.world_transform, {0.0F, 0.0F, -1.0F});
            const float direction_length_squared = dot(transformed_direction, transformed_direction);
            if (!(direction_length_squared > 1.0e-20F) || !std::isfinite(direction_length_squared))
                throw std::runtime_error("punctual light node produces a zero or non-finite world direction");
            prepared.direction = transformed_direction / std::sqrt(direction_length_squared);
            prepared.range = light.range;
            prepared.inner_cone_cos = std::cos(light.inner_cone_angle);
            prepared.outer_cone_cos = std::cos(light.outer_cone_angle);
            result.push_back(prepared);
        }
        for (const NodeId child : node.children) visit(child);
    };
    for (const NodeId root : scene.scenes[scene.selected_scene.value()].roots) visit(root);
    return result;
}

std::vector<std::size_t> build_raster_draw_order(const std::vector<PreparedRasterDraw>& draws, Vec3 camera_position)
{
    std::vector<std::size_t> opaque;
    std::vector<std::size_t> transparent;
    opaque.reserve(draws.size());
    transparent.reserve(draws.size());
    for (std::size_t index = 0; index < draws.size(); ++index)
    {
        if (draws[index].alpha_mode == AlphaMode::blend) transparent.push_back(index);
        else opaque.push_back(index);
    }
    std::stable_sort(transparent.begin(), transparent.end(), [&](std::size_t left, std::size_t right)
    {
        const float left_distance = distance_squared(draws[left].world_bounds_center, camera_position);
        const float right_distance = distance_squared(draws[right].world_bounds_center, camera_position);
        if (left_distance != right_distance) return left_distance > right_distance;
        return std::tie(draws[left].material_diagnostic_id, draws[left].primitive_index, left) <
               std::tie(draws[right].material_diagnostic_id, draws[right].primitive_index, right);
    });
    opaque.insert(opaque.end(), transparent.begin(), transparent.end());
    return opaque;
}
}
