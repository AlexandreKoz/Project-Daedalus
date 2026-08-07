#include "rendering/DiagnosticPreparation.h"

#include <functional>
#include <stdexcept>

namespace daedalus
{
std::vector<PreparedDiagnosticDraw> prepare_diagnostic_draws(const CanonicalScene& scene)
{
    if (!scene.selected_scene.valid() || scene.selected_scene.value() >= scene.scenes.size())
        throw std::runtime_error("canonical scene has no valid selected scene");

    std::vector<PreparedDiagnosticDraw> result;
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

                PreparedDiagnosticDraw draw;
                draw.primitive_index = primitive_id.value();
                draw.world = node.world_transform;
                draw.base_color_factor = material.base_color_factor;
                draw.uses_default_material = !source_material;
                draw.negative_determinant = node.negative_determinant;
                if (material.base_color_texture.has_value())
                {
                    draw.base_color_texture = material.base_color_texture->texture;
                    draw.texture_coord_set = material.base_color_texture->texcoord_set;
                    draw.selected_texcoord_available = draw.texture_coord_set == 0U ? primitive.has_texcoord0 : primitive.has_texcoord1;
                }
                result.push_back(draw);
            }
        }
        for (const NodeId child : node.children) visit(child);
    };
    for (const NodeId root : scene.scenes[scene.selected_scene.value()].roots) visit(root);
    return result;
}
}
