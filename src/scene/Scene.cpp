#include "scene/Scene.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <utility>

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

namespace
{
constexpr float kBuiltinPi = 3.14159265358979323846F;

[[nodiscard]] Quat axis_angle(Vec3 axis, float radians) noexcept
{
    const Vec3 unit = normalize(axis, {0.0F, 1.0F, 0.0F});
    const float half = radians * 0.5F;
    const float s = std::sin(half);
    return {unit.x * s, unit.y * s, unit.z * s, std::cos(half)};
}

// Appends a planar quad whose front face (glTF counter-clockwise) faces `normal`. The corner
// order is corrected with an explicit cross-product test so winding never depends on call order.
void append_quad(Primitive& primitive, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 normal, Vec2 uv_scale = {1.0F, 1.0F})
{
    if (dot(cross(b - a, c - a), normal) < 0.0F) std::swap(b, d);
    const Vec3 unit_normal = normalize(normal);
    const Vec3 tangent = normalize(b - a, {1.0F, 0.0F, 0.0F});
    const auto base = static_cast<std::uint32_t>(primitive.vertices.size());
    const std::array<Vec3, 4> corners{a, b, c, d};
    const std::array<Vec2, 4> uvs{Vec2{0.0F, uv_scale.y}, Vec2{uv_scale.x, uv_scale.y}, Vec2{uv_scale.x, 0.0F}, Vec2{0.0F, 0.0F}};
    for (std::size_t index = 0; index < corners.size(); ++index)
    {
        Vertex vertex;
        vertex.position = corners[index];
        vertex.normal = unit_normal;
        vertex.tangent = {tangent.x, tangent.y, tangent.z, 1.0F};
        vertex.texcoord0 = uvs[index];
        primitive.vertices.push_back(vertex);
    }
    for (const std::uint32_t offset : {0U, 1U, 2U, 0U, 2U, 3U}) primitive.indices.push_back(base + offset);
}

void append_unit_cube(Primitive& primitive)
{
    const float h = 0.5F;
    append_quad(primitive, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0.0F, 0.0F, 1.0F});
    append_quad(primitive, {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {0.0F, 0.0F, -1.0F});
    append_quad(primitive, {h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}, {1.0F, 0.0F, 0.0F});
    append_quad(primitive, {-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-1.0F, 0.0F, 0.0F});
    append_quad(primitive, {-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}, {0.0F, 1.0F, 0.0F});
    append_quad(primitive, {-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}, {0.0F, -1.0F, 0.0F});
}

[[nodiscard]] Primitive make_primitive(std::string name, MaterialId material)
{
    Primitive primitive;
    primitive.name = std::move(name);
    primitive.material = material;
    primitive.has_normals = true;
    primitive.has_tangents = true;
    primitive.has_texcoord0 = true;
    return primitive;
}

[[nodiscard]] MaterialId add_material(CanonicalScene& scene, Material material)
{
    scene.materials.push_back(std::move(material));
    return MaterialId(static_cast<std::uint32_t>(scene.materials.size() - 1U));
}

[[nodiscard]] MeshId add_mesh(CanonicalScene& scene, std::string name, std::vector<Primitive> primitives)
{
    Mesh mesh;
    mesh.name = std::move(name);
    for (Primitive& primitive : primitives)
    {
        scene.primitives.push_back(std::move(primitive));
        mesh.primitives.emplace_back(static_cast<std::uint32_t>(scene.primitives.size() - 1U));
    }
    scene.meshes.push_back(std::move(mesh));
    return MeshId(static_cast<std::uint32_t>(scene.meshes.size() - 1U));
}

NodeId add_node(CanonicalScene& scene, std::string name, MeshId mesh, Vec3 translation, Quat rotation, Vec3 scale)
{
    Node node;
    node.name = std::move(name);
    node.mesh = mesh;
    node.transform_source = TransformSource::trs;
    node.translation = translation;
    node.rotation = rotation;
    node.scale = scale;
    node.local_transform = compose_trs(translation, rotation, scale);
    scene.nodes.push_back(std::move(node));
    return NodeId(static_cast<std::uint32_t>(scene.nodes.size() - 1U));
}

void finalize_builtin(CanonicalScene& scene, std::string scene_name, std::string display_name)
{
    SceneDefinition definition;
    definition.name = std::move(scene_name);
    for (std::size_t index = 0; index < scene.nodes.size(); ++index)
        if (!scene.nodes[index].parent.valid()) definition.roots.emplace_back(static_cast<std::uint32_t>(index));
    scene.scenes.push_back(std::move(definition));
    scene.selected_scene = SceneDefinitionId(0);
    scene.default_material.name = "glTF default material";
    scene.source.display_name = std::move(display_name);
    scene.source.format = "internal";
    scene.source.version = "1";
    scene.source.generator = "Project Daedalus built-in scene generator";
    std::string error;
    if (!propagate_world_transforms(scene, &error)) throw std::logic_error("built-in scene hierarchy is invalid: " + error);
    recompute_bounds(scene);
    std::uint64_t geometry_bytes = 0;
    for (const Primitive& primitive : scene.primitives)
        geometry_bytes += primitive.vertices.size() * sizeof(Vertex) + primitive.indices.size() * sizeof(std::uint32_t);
    std::uint64_t image_bytes = 0;
    for (const Image& image : scene.images) image_bytes += image.decoded_rgba8.size();
    scene.source.resource_usage.canonical_geometry_bytes = geometry_bytes;
    scene.source.resource_usage.decoded_image_bytes = image_bytes;
    scene.source.resource_usage.retained_bytes = geometry_bytes + image_bytes;
    scene.source.resource_usage.conservative_peak_bytes = geometry_bytes + image_bytes;
    // Built-in scenes are versioned by name; the key changes whenever the generator version changes.
    scene.source.deterministic_asset_key = "builtin:" + scene.source.display_name + ":v1";
}
}

CanonicalScene make_builtin_cornell_box_scene()
{
    // Self-authored Cornell-box-style validation scene: unit-height room [-1,1]x[0,2]x[-1,1] with the
    // +Z wall open toward the default orbit camera, a downward-facing emissive ceiling quad, and two
    // rotated diffuse boxes. Reflectances follow the commonly published Cornell box approximations.
    CanonicalScene scene;
    Material white;
    white.name = "cornell-white";
    white.base_color_factor = {0.73F, 0.73F, 0.73F, 1.0F};
    white.metallic_factor = 0.0F;
    white.roughness_factor = 1.0F;
    Material red = white;
    red.name = "cornell-red";
    red.base_color_factor = {0.63F, 0.065F, 0.05F, 1.0F};
    Material green = white;
    green.name = "cornell-green";
    green.base_color_factor = {0.14F, 0.45F, 0.091F, 1.0F};
    Material light = white;
    light.name = "cornell-light";
    light.base_color_factor = {0.78F, 0.78F, 0.78F, 1.0F};
    light.emissive_factor = {1.0F, 0.71F, 0.24F};
    light.emissive_strength = 17.0F;
    const MaterialId white_id = add_material(scene, white);
    const MaterialId red_id = add_material(scene, red);
    const MaterialId green_id = add_material(scene, green);
    const MaterialId light_id = add_material(scene, light);

    Primitive white_walls = make_primitive("floor-ceiling-back", white_id);
    append_quad(white_walls, {-1.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 1.0F}, {1.0F, 0.0F, -1.0F}, {-1.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F});
    append_quad(white_walls, {-1.0F, 2.0F, -1.0F}, {1.0F, 2.0F, -1.0F}, {1.0F, 2.0F, 1.0F}, {-1.0F, 2.0F, 1.0F}, {0.0F, -1.0F, 0.0F});
    append_quad(white_walls, {-1.0F, 0.0F, -1.0F}, {1.0F, 0.0F, -1.0F}, {1.0F, 2.0F, -1.0F}, {-1.0F, 2.0F, -1.0F}, {0.0F, 0.0F, 1.0F});
    Primitive red_wall = make_primitive("left-wall", red_id);
    append_quad(red_wall, {-1.0F, 0.0F, 1.0F}, {-1.0F, 0.0F, -1.0F}, {-1.0F, 2.0F, -1.0F}, {-1.0F, 2.0F, 1.0F}, {1.0F, 0.0F, 0.0F});
    Primitive green_wall = make_primitive("right-wall", green_id);
    append_quad(green_wall, {1.0F, 0.0F, -1.0F}, {1.0F, 0.0F, 1.0F}, {1.0F, 2.0F, 1.0F}, {1.0F, 2.0F, -1.0F}, {-1.0F, 0.0F, 0.0F});
    Primitive light_quad = make_primitive("ceiling-light", light_id);
    append_quad(light_quad, {-0.25F, 1.98F, -0.25F}, {0.25F, 1.98F, -0.25F}, {0.25F, 1.98F, 0.25F}, {-0.25F, 1.98F, 0.25F}, {0.0F, -1.0F, 0.0F});
    std::vector<Primitive> room;
    room.push_back(std::move(white_walls));
    room.push_back(std::move(red_wall));
    room.push_back(std::move(green_wall));
    room.push_back(std::move(light_quad));
    const MeshId room_mesh = add_mesh(scene, "cornell-room", std::move(room));

    Primitive cube = make_primitive("unit-box", white_id);
    append_unit_cube(cube);
    std::vector<Primitive> cube_primitives;
    cube_primitives.push_back(std::move(cube));
    const MeshId cube_mesh = add_mesh(scene, "cornell-box", std::move(cube_primitives));

    add_node(scene, "room", room_mesh, {}, {}, {1.0F, 1.0F, 1.0F});
    add_node(scene, "short-box", cube_mesh, {0.33F, 0.3F, 0.37F}, axis_angle({0.0F, 1.0F, 0.0F}, -17.0F * kBuiltinPi / 180.0F), {0.6F, 0.6F, 0.6F});
    add_node(scene, "tall-box", cube_mesh, {-0.35F, 0.6F, -0.3F}, axis_angle({0.0F, 1.0F, 0.0F}, 17.0F * kBuiltinPi / 180.0F), {0.6F, 1.2F, 0.6F});
    finalize_builtin(scene, "Cornell box", "builtin-cornell-box");
    return scene;
}

CanonicalScene make_builtin_alpha_instancing_scene()
{
    // Instancing + alpha-mask + negative-scale validation: one masked leaf card mesh instanced five
    // times (one mirrored with a negative X scale, one double-sided), one BLEND card, an opaque ground,
    // and a directional light. The mask texture is generated procedurally (64x64 RGBA8 discs).
    CanonicalScene scene;
    constexpr std::uint32_t size = 64;
    Image image;
    image.name = "procedural-disc-mask";
    image.source_identity = "builtin:procedural-disc-mask:v1";
    image.mime_type = "image/x-daedalus-generated";
    image.width = size;
    image.height = size;
    image.components = 4;
    image.row_stride = static_cast<std::uint64_t>(size) * 4U;
    image.decoded_rgba8.resize(static_cast<std::size_t>(size) * size * 4U);
    for (std::uint32_t y = 0; y < size; ++y)
    {
        for (std::uint32_t x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(size);
            const float v = (static_cast<float>(y) + 0.5F) / static_cast<float>(size);
            const float cell_u = std::fmod(u * 4.0F, 1.0F) - 0.5F;
            const float cell_v = std::fmod(v * 4.0F, 1.0F) - 0.5F;
            const bool inside = cell_u * cell_u + cell_v * cell_v < 0.16F;
            std::byte* pixel = image.decoded_rgba8.data() + (static_cast<std::size_t>(y) * size + x) * 4U;
            pixel[0] = std::byte{60};
            pixel[1] = std::byte{static_cast<unsigned char>(140U + (x * 2U) % 100U)};
            pixel[2] = std::byte{50};
            pixel[3] = std::byte{static_cast<unsigned char>(inside ? 255U : 0U)};
        }
    }
    scene.images.push_back(std::move(image));
    Sampler sampler;
    sampler.name = "nearest-repeat";
    sampler.min_filter = FilterMode::nearest;
    sampler.mag_filter = FilterMode::nearest;
    scene.samplers.push_back(sampler);
    Texture texture;
    texture.name = "disc-mask";
    texture.image = ImageId(0);
    texture.sampler = SamplerId(0);
    texture.color_space = ColorSpaceIntent::srgb;
    scene.textures.push_back(texture);

    Material ground;
    ground.name = "ground";
    ground.base_color_factor = {0.55F, 0.55F, 0.52F, 1.0F};
    ground.metallic_factor = 0.0F;
    ground.roughness_factor = 0.8F;
    Material masked = ground;
    masked.name = "masked-leaf";
    masked.base_color_factor = {1.0F, 1.0F, 1.0F, 1.0F};
    masked.base_color_texture = TextureReference{TextureId(0), 0U, 1.0F};
    masked.alpha_mode = AlphaMode::mask;
    masked.alpha_cutoff = 0.5F;
    masked.roughness_factor = 0.6F;
    Material masked_double = masked;
    masked_double.name = "masked-leaf-double-sided";
    masked_double.double_sided = true;
    Material blended = ground;
    blended.name = "blend-card";
    blended.base_color_factor = {0.2F, 0.4F, 0.9F, 0.45F};
    blended.alpha_mode = AlphaMode::blend;
    blended.roughness_factor = 0.3F;
    Material mirror = ground;
    mirror.name = "polished-metal";
    mirror.base_color_factor = {0.95F, 0.93F, 0.88F, 1.0F};
    mirror.metallic_factor = 1.0F;
    mirror.roughness_factor = 0.08F;
    const MaterialId ground_id = add_material(scene, ground);
    const MaterialId masked_id = add_material(scene, masked);
    const MaterialId masked_double_id = add_material(scene, masked_double);
    const MaterialId blend_id = add_material(scene, blended);
    const MaterialId mirror_id = add_material(scene, mirror);

    Primitive ground_quad = make_primitive("ground", ground_id);
    append_quad(ground_quad, {-3.0F, 0.0F, 3.0F}, {3.0F, 0.0F, 3.0F}, {3.0F, 0.0F, -3.0F}, {-3.0F, 0.0F, -3.0F}, {0.0F, 1.0F, 0.0F}, {6.0F, 6.0F});
    std::vector<Primitive> ground_primitives;
    ground_primitives.push_back(std::move(ground_quad));
    const MeshId ground_mesh = add_mesh(scene, "ground", std::move(ground_primitives));

    auto card = [&](MaterialId material, const char* name)
    {
        Primitive primitive = make_primitive(name, material);
        append_quad(primitive, {-0.5F, 0.0F, 0.0F}, {0.5F, 0.0F, 0.0F}, {0.5F, 1.0F, 0.0F}, {-0.5F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
        std::vector<Primitive> primitives;
        primitives.push_back(std::move(primitive));
        return add_mesh(scene, name, std::move(primitives));
    };
    const MeshId leaf = card(masked_id, "masked-card");
    const MeshId leaf_double = card(masked_double_id, "masked-card-double-sided");
    const MeshId blend_card = card(blend_id, "blend-card");

    Primitive cube = make_primitive("metal-box", mirror_id);
    append_unit_cube(cube);
    std::vector<Primitive> cube_primitives;
    cube_primitives.push_back(std::move(cube));
    const MeshId cube_mesh = add_mesh(scene, "metal-box", std::move(cube_primitives));

    const Quat identity{};
    add_node(scene, "ground", ground_mesh, {}, identity, {1.0F, 1.0F, 1.0F});
    add_node(scene, "leaf-0", leaf, {-1.6F, 0.0F, 0.2F}, identity, {1.0F, 1.0F, 1.0F});
    add_node(scene, "leaf-1", leaf, {-0.5F, 0.0F, -0.4F}, axis_angle({0.0F, 1.0F, 0.0F}, 0.6F), {1.2F, 1.2F, 1.2F});
    add_node(scene, "leaf-mirrored", leaf, {0.6F, 0.0F, -0.2F}, axis_angle({0.0F, 1.0F, 0.0F}, -0.4F), {-1.0F, 1.0F, 1.0F});
    add_node(scene, "leaf-double-sided", leaf_double, {1.7F, 0.0F, 0.1F}, axis_angle({0.0F, 1.0F, 0.0F}, 2.6F), {1.0F, 1.0F, 1.0F});
    // Negative Y scale flips the card downward, so it is translated up by its height to stand on the ground.
    add_node(scene, "leaf-mirrored-double-sided", leaf_double, {-0.2F, 1.0F, 1.1F}, identity, {1.0F, -1.0F, 1.0F});
    add_node(scene, "blend-card", blend_card, {0.0F, 0.0F, 1.6F}, axis_angle({0.0F, 1.0F, 0.0F}, 0.3F), {1.4F, 1.4F, 1.4F});
    add_node(scene, "metal-box", cube_mesh, {0.0F, 0.35F, -1.6F}, axis_angle({0.0F, 1.0F, 0.0F}, 0.5F), {0.7F, 0.7F, 0.7F});

    Light sun;
    sun.name = "sun";
    sun.type = LightType::directional;
    sun.color = {1.0F, 0.95F, 0.86F};
    sun.intensity = 3.0F;
    scene.lights.push_back(sun);
    Node light_node;
    light_node.name = "sun";
    light_node.transform_source = TransformSource::trs;
    light_node.rotation = axis_angle({1.0F, 0.0F, 0.0F}, -1.0F);
    light_node.local_transform = compose_trs({}, light_node.rotation, {1.0F, 1.0F, 1.0F});
    light_node.light = LightId(0);
    scene.nodes.push_back(std::move(light_node));
    finalize_builtin(scene, "Alpha instancing", "builtin-alpha-instancing");
    return scene;
}

CanonicalScene make_builtin_scene(std::string_view name)
{
    if (name == "triangle") return make_builtin_triangle_scene();
    if (name == "cornell-box") return make_builtin_cornell_box_scene();
    if (name == "alpha-instancing") return make_builtin_alpha_instancing_scene();
    throw std::invalid_argument("unknown built-in scene: " + std::string(name));
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
