#include "scene/Scene.h"

#include <functional>
#include <sstream>

namespace daedalus
{
bool propagate_world_transforms(CanonicalScene& scene, std::string* error_message)
{
    enum class State : std::uint8_t { unvisited, visiting, complete };
    std::vector<State> states(scene.nodes.size(), State::unvisited);
    std::function<bool(NodeId, const Mat4&)> visit = [&](NodeId id, const Mat4& parent_world)
    {
        if (!id.valid() || id.value() >= scene.nodes.size())
        {
            if (error_message != nullptr) *error_message = "node hierarchy references an invalid node identifier";
            return false;
        }
        State& state = states[id.value()];
        if (state == State::visiting)
        {
            if (error_message != nullptr) *error_message = "node hierarchy contains a cycle";
            return false;
        }
        if (state == State::complete) return true;
        state = State::visiting;
        Node& node = scene.nodes[id.value()];
        node.world_transform = multiply(parent_world, node.local_transform);
        node.negative_determinant = determinant3x3(node.world_transform) < 0.0F;
        for (const NodeId child : node.children)
        {
            if (!visit(child, node.world_transform)) return false;
        }
        state = State::complete;
        return true;
    };

    for (std::size_t index = 0; index < scene.nodes.size(); ++index)
    {
        if (!scene.nodes[index].parent.valid() && !visit(NodeId(static_cast<std::uint32_t>(index)), identity_matrix()))
            return false;
    }
    for (const State state : states)
    {
        if (state != State::complete)
        {
            if (error_message != nullptr) *error_message = "node hierarchy contains unreachable or cyclic nodes";
            return false;
        }
    }
    return true;
}

void recompute_bounds(CanonicalScene& scene)
{
    for (Primitive& primitive : scene.primitives)
    {
        primitive.bounds = Aabb{};
        for (const Vertex& vertex : primitive.vertices) expand(primitive.bounds, vertex.position);
    }
    for (Mesh& mesh : scene.meshes)
    {
        mesh.bounds = Aabb{};
        for (const PrimitiveId primitive : mesh.primitives)
            if (primitive.valid() && primitive.value() < scene.primitives.size()) expand(mesh.bounds, scene.primitives[primitive.value()].bounds);
    }
    for (Node& node : scene.nodes)
    {
        node.world_bounds = Aabb{};
        if (node.mesh.valid() && node.mesh.value() < scene.meshes.size())
            node.world_bounds = transform_bounds(scene.meshes[node.mesh.value()].bounds, node.world_transform);
    }
    for (SceneDefinition& definition : scene.scenes)
    {
        definition.bounds = Aabb{};
        std::function<void(NodeId)> visit = [&](NodeId id)
        {
            if (!id.valid() || id.value() >= scene.nodes.size()) return;
            const Node& node = scene.nodes[id.value()];
            expand(definition.bounds, node.world_bounds);
            for (const NodeId child : node.children) visit(child);
        };
        for (const NodeId root : definition.roots) visit(root);
    }
}

CanonicalScene make_builtin_triangle_scene()
{
    CanonicalScene scene;
    Primitive primitive;
    primitive.name = "Campaign A diagnostic triangle";
    primitive.vertices = {
        {{0.0F, 0.65F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F,0.0F,0.0F,1.0F}, {0.5F,0.0F}, {}, {1.0F,0.0F,0.0F,1.0F}},
        {{0.65F,-0.65F,0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F,0.0F,0.0F,1.0F}, {1.0F,1.0F}, {}, {0.0F,1.0F,0.0F,1.0F}},
        {{-0.65F,-0.65F,0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F,0.0F,0.0F,1.0F}, {0.0F,1.0F}, {}, {0.0F,0.0F,1.0F,1.0F}}};
    primitive.indices = {0, 1, 2};
    primitive.has_normals = true;
    primitive.has_texcoord0 = true;
    primitive.has_colors = true;
    scene.primitives.push_back(std::move(primitive));

    Material material;
    material.name = "Built-in vertex colour material";
    scene.materials.push_back(material);
    scene.primitives[0].material = MaterialId(0);

    Mesh mesh;
    mesh.name = "Built-in triangle mesh";
    mesh.primitives.push_back(PrimitiveId(0));
    scene.meshes.push_back(std::move(mesh));

    Node node;
    node.name = "Built-in triangle node";
    node.mesh = MeshId(0);
    scene.nodes.push_back(std::move(node));

    SceneDefinition definition;
    definition.name = "Built-in fallback scene";
    definition.roots.push_back(NodeId(0));
    scene.scenes.push_back(std::move(definition));
    scene.selected_scene = SceneDefinitionId(0);
    scene.source.display_name = "built-in-triangle";
    scene.source.format = "internal";
    scene.source.version = "1";
    static_cast<void>(propagate_world_transforms(scene));
    recompute_bounds(scene);
    return scene;
}

std::string dump_scene_hierarchy(const CanonicalScene& scene)
{
    std::ostringstream output;
    output << "Canonical scene '" << scene.source.display_name << "'\n";
    output << "nodes=" << scene.nodes.size() << " meshes=" << scene.meshes.size()
           << " primitives=" << scene.primitives.size() << " materials=" << scene.materials.size()
           << " textures=" << scene.textures.size() << " images=" << scene.images.size() << '\n';
    if (!scene.selected_scene.valid() || scene.selected_scene.value() >= scene.scenes.size())
    {
        output << "selected scene: <invalid>\n";
        return output.str();
    }
    const SceneDefinition& definition = scene.scenes[scene.selected_scene.value()];
    output << "selected scene: [" << scene.selected_scene.value() << "] " << definition.name << '\n';
    std::function<void(NodeId, int)> visit = [&](NodeId id, int depth)
    {
        if (!id.valid() || id.value() >= scene.nodes.size()) return;
        const Node& node = scene.nodes[id.value()];
        output << std::string(static_cast<std::size_t>(depth) * 2U, ' ') << "- node[" << id.value() << "] "
               << (node.name.empty() ? "<unnamed>" : node.name);
        if (node.mesh.valid()) output << " mesh=" << node.mesh.value();
        if (node.camera.valid()) output << " camera=" << node.camera.value();
        if (node.light.valid()) output << " light=" << node.light.value();
        if (node.negative_determinant) output << " negative-scale";
        output << '\n';
        for (const NodeId child : node.children) visit(child, depth + 1);
    };
    for (const NodeId root : definition.roots) visit(root, 0);
    return output.str();
}
}
