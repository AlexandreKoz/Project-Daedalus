#include "assets/SceneSerialization.h"

#include <bit>
#include <cstring>
#include <limits>
#include <initializer_list>
#include <optional>
#include <utility>

namespace daedalus
{
namespace
{
static_assert(std::endian::native == std::endian::little || std::endian::native == std::endian::big);

constexpr std::uint32_t kMagic = 0x4E435344U; // "DSCN"
constexpr std::uint64_t kMaximumStringBytes = 64ULL * 1024ULL * 1024ULL;

class Writer final
{
public:
    void u8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }
    void u32(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8) bytes_.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
    void u64(std::uint64_t value)
    {
        for (int shift = 0; shift < 64; shift += 8) bytes_.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
    void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
    void boolean(bool value) { u8(value ? 1U : 0U); }
    void string(const std::string& value)
    {
        u64(value.size());
        for (const char c : value) bytes_.push_back(static_cast<std::byte>(c));
    }
    void blob(const std::vector<std::byte>& value)
    {
        u64(value.size());
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }
    void vec2(Vec2 v) { f32(v.x); f32(v.y); }
    void vec3(Vec3 v) { f32(v.x); f32(v.y); f32(v.z); }
    void vec4(Vec4 v) { f32(v.x); f32(v.y); f32(v.z); f32(v.w); }
    void quat(Quat q) { f32(q.x); f32(q.y); f32(q.z); f32(q.w); }
    void mat4(const Mat4& m) { for (const float value : m.values) f32(value); }
    void aabb(const Aabb& b) { vec3(b.minimum); vec3(b.maximum); }
    template <typename Tag>
    void handle(Handle<Tag> h) { u32(h.value()); }
    std::vector<std::byte> take() { return std::move(bytes_); }

private:
    std::vector<std::byte> bytes_;
};

class Reader final
{
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    std::uint8_t u8()
    {
        require(1U);
        return static_cast<std::uint8_t>(bytes_[offset_++]);
    }
    std::uint32_t u32()
    {
        require(4U);
        std::uint32_t value = 0;
        for (int shift = 0; shift < 32; shift += 8) value |= static_cast<std::uint32_t>(bytes_[offset_++]) << shift;
        return value;
    }
    std::uint64_t u64()
    {
        require(8U);
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 8) value |= static_cast<std::uint64_t>(bytes_[offset_++]) << shift;
        return value;
    }
    float f32() { return std::bit_cast<float>(u32()); }
    bool boolean()
    {
        const std::uint8_t value = u8();
        if (value > 1U) throw SceneSerializationError("invalid boolean in scene payload");
        return value == 1U;
    }
    std::string string()
    {
        const std::uint64_t size = u64();
        if (size > kMaximumStringBytes) throw SceneSerializationError("string exceeds the scene payload limit");
        require(size);
        std::string value(reinterpret_cast<const char*>(bytes_.data() + offset_), static_cast<std::size_t>(size));
        offset_ += static_cast<std::size_t>(size);
        return value;
    }
    std::vector<std::byte> blob()
    {
        const std::uint64_t size = u64();
        require(size);
        std::vector<std::byte> value(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                                     bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + size));
        offset_ += static_cast<std::size_t>(size);
        return value;
    }
    // Count of elements whose encoded size is at least `minimum_element_bytes`; validated against
    // the remaining payload so corrupt counts cannot trigger huge allocations.
    std::size_t count(std::uint64_t minimum_element_bytes)
    {
        const std::uint64_t value = u64();
        if (minimum_element_bytes != 0U && value > remaining() / minimum_element_bytes)
            throw SceneSerializationError("container count exceeds the remaining scene payload");
        if (value > std::numeric_limits<std::size_t>::max()) throw SceneSerializationError("container count overflow");
        return static_cast<std::size_t>(value);
    }
    Vec2 vec2() { const float x = f32(); const float y = f32(); return {x, y}; }
    Vec3 vec3() { const float x = f32(); const float y = f32(); const float z = f32(); return {x, y, z}; }
    Vec4 vec4() { const float x = f32(); const float y = f32(); const float z = f32(); const float w = f32(); return {x, y, z, w}; }
    Quat quat() { const float x = f32(); const float y = f32(); const float z = f32(); const float w = f32(); return {x, y, z, w}; }
    Mat4 mat4()
    {
        Mat4 m{};
        for (float& value : m.values) value = f32();
        return m;
    }
    Aabb aabb()
    {
        Aabb b;
        b.minimum = vec3();
        b.maximum = vec3();
        return b;
    }
    template <typename HandleType>
    HandleType handle() { return HandleType(u32()); }
    [[nodiscard]] std::uint64_t remaining() const noexcept { return bytes_.size() - offset_; }

private:
    void require(std::uint64_t size) const
    {
        if (size > remaining()) throw SceneSerializationError("scene payload is truncated");
    }

    std::span<const std::byte> bytes_;
    std::size_t offset_ = 0;
};

void write_texture_reference(Writer& writer, const std::optional<TextureReference>& reference)
{
    writer.boolean(reference.has_value());
    if (!reference.has_value()) return;
    writer.handle(reference->texture);
    writer.u32(reference->texcoord_set);
    writer.f32(reference->scale);
}

std::optional<TextureReference> read_texture_reference(Reader& reader)
{
    if (!reader.boolean()) return std::nullopt;
    TextureReference reference;
    reference.texture = reader.handle<TextureId>();
    reference.texcoord_set = reader.u32();
    reference.scale = reader.f32();
    return reference;
}

void write_material(Writer& writer, const Material& material)
{
    writer.string(material.name);
    writer.vec4(material.base_color_factor);
    writer.f32(material.metallic_factor);
    writer.f32(material.roughness_factor);
    writer.vec3(material.emissive_factor);
    write_texture_reference(writer, material.base_color_texture);
    write_texture_reference(writer, material.metallic_roughness_texture);
    write_texture_reference(writer, material.normal_texture);
    write_texture_reference(writer, material.occlusion_texture);
    write_texture_reference(writer, material.emissive_texture);
    writer.f32(material.normal_scale);
    writer.f32(material.occlusion_strength);
    writer.u32(static_cast<std::uint32_t>(material.alpha_mode));
    writer.f32(material.alpha_cutoff);
    writer.boolean(material.double_sided);
    writer.f32(material.emissive_strength);
    writer.f32(material.transmission_factor);
    writer.f32(material.ior);
    writer.f32(material.thickness_factor);
    writer.f32(material.attenuation_distance);
    writer.vec3(material.attenuation_color);
}

Material read_material(Reader& reader)
{
    Material material;
    material.name = reader.string();
    material.base_color_factor = reader.vec4();
    material.metallic_factor = reader.f32();
    material.roughness_factor = reader.f32();
    material.emissive_factor = reader.vec3();
    material.base_color_texture = read_texture_reference(reader);
    material.metallic_roughness_texture = read_texture_reference(reader);
    material.normal_texture = read_texture_reference(reader);
    material.occlusion_texture = read_texture_reference(reader);
    material.emissive_texture = read_texture_reference(reader);
    material.normal_scale = reader.f32();
    material.occlusion_strength = reader.f32();
    const std::uint32_t alpha_mode = reader.u32();
    if (alpha_mode > 2U) throw SceneSerializationError("invalid alpha mode in scene payload");
    material.alpha_mode = static_cast<AlphaMode>(alpha_mode);
    material.alpha_cutoff = reader.f32();
    material.double_sided = reader.boolean();
    material.emissive_strength = reader.f32();
    material.transmission_factor = reader.f32();
    material.ior = reader.f32();
    material.thickness_factor = reader.f32();
    material.attenuation_distance = reader.f32();
    material.attenuation_color = reader.vec3();
    return material;
}

template <typename Enum>
Enum read_enum(Reader& reader, std::uint32_t maximum, const char* what)
{
    const std::uint32_t value = reader.u32();
    if (value > maximum) throw SceneSerializationError(std::string("invalid ") + what + " in scene payload");
    return static_cast<Enum>(value);
}

void validate_references(const CanonicalScene& scene)
{
    auto check = [](bool condition, const char* message)
    {
        if (!condition) throw SceneSerializationError(message);
    };
    for (const Primitive& primitive : scene.primitives)
    {
        check(!primitive.material.valid() || primitive.material.value() < scene.materials.size(), "primitive material out of range");
        for (const std::uint32_t index : primitive.indices) check(index < primitive.vertices.size(), "primitive index out of range");
    }
    for (const Mesh& mesh : scene.meshes)
        for (const PrimitiveId primitive : mesh.primitives)
            check(primitive.valid() && primitive.value() < scene.primitives.size(), "mesh primitive out of range");
    for (const Node& node : scene.nodes)
    {
        check(!node.parent.valid() || node.parent.value() < scene.nodes.size(), "node parent out of range");
        check(!node.mesh.valid() || node.mesh.value() < scene.meshes.size(), "node mesh out of range");
        check(!node.camera.valid() || node.camera.value() < scene.cameras.size(), "node camera out of range");
        check(!node.light.valid() || node.light.value() < scene.lights.size(), "node light out of range");
        for (const NodeId child : node.children) check(child.valid() && child.value() < scene.nodes.size(), "node child out of range");
    }
    for (const Texture& texture : scene.textures)
    {
        check(texture.image.valid() && texture.image.value() < scene.images.size(), "texture image out of range");
        check(!texture.sampler.valid() || texture.sampler.value() < scene.samplers.size(), "texture sampler out of range");
    }
    auto check_material = [&](const Material& material)
    {
        for (const auto* reference : {&material.base_color_texture, &material.metallic_roughness_texture, &material.normal_texture,
                                      &material.occlusion_texture, &material.emissive_texture})
            if (reference->has_value())
                check((*reference)->texture.valid() && (*reference)->texture.value() < scene.textures.size() && (*reference)->texcoord_set <= 1U,
                      "material texture reference out of range");
    };
    check_material(scene.default_material);
    for (const Material& material : scene.materials) check_material(material);
    for (const Image& image : scene.images)
        check(image.decoded_rgba8.size() == static_cast<std::uint64_t>(image.width) * image.height * 4U, "image payload size mismatch");
    for (const SceneDefinition& definition : scene.scenes)
        for (const NodeId root : definition.roots) check(root.valid() && root.value() < scene.nodes.size(), "scene root out of range");
    check(!scene.selected_scene.valid() || scene.selected_scene.value() < scene.scenes.size(), "selected scene out of range");
}
}

std::vector<std::byte> serialize_scene(const CanonicalScene& scene)
{
    Writer writer;
    writer.u32(kMagic);
    writer.u32(kSceneSerializationVersion);
    writer.string(std::string(CanonicalScene::schema_version));

    writer.u64(scene.nodes.size());
    for (const Node& node : scene.nodes)
    {
        writer.string(node.name);
        writer.u32(static_cast<std::uint32_t>(node.transform_source));
        writer.vec3(node.translation);
        writer.quat(node.rotation);
        writer.vec3(node.scale);
        writer.mat4(node.local_transform);
        writer.mat4(node.world_transform);
        writer.handle(node.parent);
        writer.u64(node.children.size());
        for (const NodeId child : node.children) writer.handle(child);
        writer.handle(node.mesh);
        writer.handle(node.camera);
        writer.handle(node.light);
        writer.aabb(node.world_bounds);
        writer.boolean(node.negative_determinant);
    }
    writer.u64(scene.meshes.size());
    for (const Mesh& mesh : scene.meshes)
    {
        writer.string(mesh.name);
        writer.u64(mesh.primitives.size());
        for (const PrimitiveId primitive : mesh.primitives) writer.handle(primitive);
        writer.aabb(mesh.bounds);
    }
    writer.u64(scene.primitives.size());
    for (const Primitive& primitive : scene.primitives)
    {
        writer.string(primitive.name);
        writer.u64(primitive.vertices.size());
        for (const Vertex& vertex : primitive.vertices)
        {
            writer.vec3(vertex.position);
            writer.vec3(vertex.normal);
            writer.vec4(vertex.tangent);
            writer.vec2(vertex.texcoord0);
            writer.vec2(vertex.texcoord1);
            writer.vec4(vertex.color);
        }
        writer.u64(primitive.indices.size());
        for (const std::uint32_t index : primitive.indices) writer.u32(index);
        writer.handle(primitive.material);
        writer.u32(static_cast<std::uint32_t>(primitive.topology));
        writer.boolean(primitive.has_normals);
        writer.boolean(primitive.has_tangents);
        writer.boolean(primitive.has_texcoord0);
        writer.boolean(primitive.has_texcoord1);
        writer.boolean(primitive.has_colors);
        writer.aabb(primitive.bounds);
    }
    writer.u64(scene.materials.size());
    for (const Material& material : scene.materials) write_material(writer, material);
    write_material(writer, scene.default_material);
    writer.u64(scene.textures.size());
    for (const Texture& texture : scene.textures)
    {
        writer.string(texture.name);
        writer.handle(texture.image);
        writer.handle(texture.sampler);
        writer.u32(static_cast<std::uint32_t>(texture.color_space));
    }
    writer.u64(scene.images.size());
    for (const Image& image : scene.images)
    {
        writer.string(image.name);
        writer.string(image.source_identity);
        writer.string(image.mime_type);
        writer.blob(image.encoded_bytes);
        writer.blob(image.decoded_rgba8);
        writer.u32(image.width);
        writer.u32(image.height);
        writer.u32(image.components);
        writer.u64(image.row_stride);
    }
    writer.u64(scene.samplers.size());
    for (const Sampler& sampler : scene.samplers)
    {
        writer.string(sampler.name);
        writer.u32(static_cast<std::uint32_t>(sampler.min_filter));
        writer.u32(static_cast<std::uint32_t>(sampler.mag_filter));
        writer.u32(static_cast<std::uint32_t>(sampler.wrap_s));
        writer.u32(static_cast<std::uint32_t>(sampler.wrap_t));
    }
    writer.u64(scene.cameras.size());
    for (const Camera& camera : scene.cameras)
    {
        writer.string(camera.name);
        writer.u32(static_cast<std::uint32_t>(camera.type));
        writer.f32(camera.aspect_ratio);
        writer.f32(camera.vertical_fov_radians);
        writer.f32(camera.near_plane);
        writer.f32(camera.far_plane);
        writer.f32(camera.x_magnification);
        writer.f32(camera.y_magnification);
    }
    writer.u64(scene.lights.size());
    for (const Light& light : scene.lights)
    {
        writer.string(light.name);
        writer.u32(static_cast<std::uint32_t>(light.type));
        writer.vec3(light.color);
        writer.f32(light.intensity);
        writer.boolean(light.range.has_value());
        writer.f32(light.range.value_or(0.0F));
        writer.f32(light.inner_cone_angle);
        writer.f32(light.outer_cone_angle);
    }
    writer.u64(scene.scenes.size());
    for (const SceneDefinition& definition : scene.scenes)
    {
        writer.string(definition.name);
        writer.u64(definition.roots.size());
        for (const NodeId root : definition.roots) writer.handle(root);
        writer.aabb(definition.bounds);
    }
    writer.handle(scene.selected_scene);

    const SourceMetadata& source = scene.source;
    writer.string(source.display_name);
    writer.string(source.format);
    writer.string(source.version);
    writer.string(source.generator);
    writer.string(source.copyright);
    writer.string(source.source_sha256);
    writer.string(source.deterministic_asset_key);
    writer.u64(source.extensions_used.size());
    for (const std::string& extension : source.extensions_used) writer.string(extension);
    writer.u64(source.extensions_required.size());
    for (const std::string& extension : source.extensions_required) writer.string(extension);
    writer.u64(source.dependencies.size());
    for (const DependencyRecord& dependency : source.dependencies)
    {
        writer.string(dependency.normalized_relative_path);
        writer.string(dependency.sha256);
        writer.u64(dependency.byte_size);
    }
    const ResourceUsage& usage = source.resource_usage;
    writer.u64(usage.source_payload_bytes);
    writer.u64(usage.buffer_payload_bytes);
    writer.u64(usage.encoded_image_bytes);
    writer.u64(usage.canonical_geometry_bytes);
    writer.u64(usage.decoded_image_bytes);
    writer.u64(usage.retained_bytes);
    writer.u64(usage.conservative_peak_bytes);
    return writer.take();
}

CanonicalScene deserialize_scene(std::span<const std::byte> payload)
{
    Reader reader(payload);
    if (reader.u32() != kMagic) throw SceneSerializationError("scene payload has the wrong magic");
    if (reader.u32() != kSceneSerializationVersion) throw SceneSerializationError("scene payload version is not supported");
    if (reader.string() != CanonicalScene::schema_version) throw SceneSerializationError("scene payload canonical schema version differs");

    CanonicalScene scene;
    scene.nodes.resize(reader.count(4U));
    for (Node& node : scene.nodes)
    {
        node.name = reader.string();
        node.transform_source = read_enum<TransformSource>(reader, 2U, "transform source");
        node.translation = reader.vec3();
        node.rotation = reader.quat();
        node.scale = reader.vec3();
        node.local_transform = reader.mat4();
        node.world_transform = reader.mat4();
        node.parent = reader.handle<NodeId>();
        node.children.resize(reader.count(4U));
        for (NodeId& child : node.children) child = reader.handle<NodeId>();
        node.mesh = reader.handle<MeshId>();
        node.camera = reader.handle<CameraId>();
        node.light = reader.handle<LightId>();
        node.world_bounds = reader.aabb();
        node.negative_determinant = reader.boolean();
    }
    scene.meshes.resize(reader.count(4U));
    for (Mesh& mesh : scene.meshes)
    {
        mesh.name = reader.string();
        mesh.primitives.resize(reader.count(4U));
        for (PrimitiveId& primitive : mesh.primitives) primitive = reader.handle<PrimitiveId>();
        mesh.bounds = reader.aabb();
    }
    scene.primitives.resize(reader.count(8U));
    for (Primitive& primitive : scene.primitives)
    {
        primitive.name = reader.string();
        primitive.vertices.resize(reader.count(sizeof(Vertex)));
        for (Vertex& vertex : primitive.vertices)
        {
            vertex.position = reader.vec3();
            vertex.normal = reader.vec3();
            vertex.tangent = reader.vec4();
            vertex.texcoord0 = reader.vec2();
            vertex.texcoord1 = reader.vec2();
            vertex.color = reader.vec4();
        }
        primitive.indices.resize(reader.count(4U));
        for (std::uint32_t& index : primitive.indices) index = reader.u32();
        primitive.material = reader.handle<MaterialId>();
        primitive.topology = read_enum<PrimitiveTopology>(reader, 0U, "primitive topology");
        primitive.has_normals = reader.boolean();
        primitive.has_tangents = reader.boolean();
        primitive.has_texcoord0 = reader.boolean();
        primitive.has_texcoord1 = reader.boolean();
        primitive.has_colors = reader.boolean();
        primitive.bounds = reader.aabb();
    }
    scene.materials.resize(reader.count(8U));
    for (Material& material : scene.materials) material = read_material(reader);
    scene.default_material = read_material(reader);
    scene.textures.resize(reader.count(8U));
    for (Texture& texture : scene.textures)
    {
        texture.name = reader.string();
        texture.image = reader.handle<ImageId>();
        texture.sampler = reader.handle<SamplerId>();
        texture.color_space = read_enum<ColorSpaceIntent>(reader, 1U, "colour space");
    }
    scene.images.resize(reader.count(8U));
    for (Image& image : scene.images)
    {
        image.name = reader.string();
        image.source_identity = reader.string();
        image.mime_type = reader.string();
        image.encoded_bytes = reader.blob();
        image.decoded_rgba8 = reader.blob();
        image.width = reader.u32();
        image.height = reader.u32();
        image.components = reader.u32();
        image.row_stride = reader.u64();
    }
    scene.samplers.resize(reader.count(8U));
    for (Sampler& sampler : scene.samplers)
    {
        sampler.name = reader.string();
        sampler.min_filter = read_enum<FilterMode>(reader, 6U, "min filter");
        sampler.mag_filter = read_enum<FilterMode>(reader, 6U, "mag filter");
        sampler.wrap_s = read_enum<WrapMode>(reader, 2U, "wrap mode");
        sampler.wrap_t = read_enum<WrapMode>(reader, 2U, "wrap mode");
    }
    scene.cameras.resize(reader.count(8U));
    for (Camera& camera : scene.cameras)
    {
        camera.name = reader.string();
        camera.type = read_enum<CameraType>(reader, 1U, "camera type");
        camera.aspect_ratio = reader.f32();
        camera.vertical_fov_radians = reader.f32();
        camera.near_plane = reader.f32();
        camera.far_plane = reader.f32();
        camera.x_magnification = reader.f32();
        camera.y_magnification = reader.f32();
    }
    scene.lights.resize(reader.count(8U));
    for (Light& light : scene.lights)
    {
        light.name = reader.string();
        light.type = read_enum<LightType>(reader, 2U, "light type");
        light.color = reader.vec3();
        light.intensity = reader.f32();
        const bool has_range = reader.boolean();
        const float range = reader.f32();
        if (has_range) light.range = range;
        light.inner_cone_angle = reader.f32();
        light.outer_cone_angle = reader.f32();
    }
    scene.scenes.resize(reader.count(8U));
    for (SceneDefinition& definition : scene.scenes)
    {
        definition.name = reader.string();
        definition.roots.resize(reader.count(4U));
        for (NodeId& root : definition.roots) root = reader.handle<NodeId>();
        definition.bounds = reader.aabb();
    }
    scene.selected_scene = reader.handle<SceneDefinitionId>();

    SourceMetadata& source = scene.source;
    source.display_name = reader.string();
    source.format = reader.string();
    source.version = reader.string();
    source.generator = reader.string();
    source.copyright = reader.string();
    source.source_sha256 = reader.string();
    source.deterministic_asset_key = reader.string();
    source.extensions_used.resize(reader.count(8U));
    for (std::string& extension : source.extensions_used) extension = reader.string();
    source.extensions_required.resize(reader.count(8U));
    for (std::string& extension : source.extensions_required) extension = reader.string();
    source.dependencies.resize(reader.count(24U));
    for (DependencyRecord& dependency : source.dependencies)
    {
        dependency.normalized_relative_path = reader.string();
        dependency.sha256 = reader.string();
        dependency.byte_size = reader.u64();
    }
    ResourceUsage& usage = source.resource_usage;
    usage.source_payload_bytes = reader.u64();
    usage.buffer_payload_bytes = reader.u64();
    usage.encoded_image_bytes = reader.u64();
    usage.canonical_geometry_bytes = reader.u64();
    usage.decoded_image_bytes = reader.u64();
    usage.retained_bytes = reader.u64();
    usage.conservative_peak_bytes = reader.u64();
    if (reader.remaining() != 0U) throw SceneSerializationError("scene payload has trailing bytes");
    validate_references(scene);
    return scene;
}
}
