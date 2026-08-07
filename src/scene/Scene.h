#pragma once

#include "scene/Math.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace daedalus
{
template <typename Tag>
class Handle final
{
public:
    static constexpr std::uint32_t invalid_value = std::numeric_limits<std::uint32_t>::max();

    constexpr Handle() noexcept = default;
    explicit constexpr Handle(std::uint32_t value) noexcept : value_(value) {}

    [[nodiscard]] constexpr bool valid() const noexcept { return value_ != invalid_value; }
    [[nodiscard]] constexpr std::uint32_t value() const noexcept { return value_; }
    friend constexpr bool operator==(Handle, Handle) noexcept = default;

private:
    std::uint32_t value_ = invalid_value;
};

struct NodeTag; struct MeshTag; struct PrimitiveTag; struct MaterialTag; struct TextureTag;
struct ImageTag; struct SamplerTag; struct CameraTag; struct LightTag; struct SceneDefinitionTag;
using NodeId = Handle<NodeTag>;
using MeshId = Handle<MeshTag>;
using PrimitiveId = Handle<PrimitiveTag>;
using MaterialId = Handle<MaterialTag>;
using TextureId = Handle<TextureTag>;
using ImageId = Handle<ImageTag>;
using SamplerId = Handle<SamplerTag>;
using CameraId = Handle<CameraTag>;
using LightId = Handle<LightTag>;
using SceneDefinitionId = Handle<SceneDefinitionTag>;

struct Vertex
{
    Vec3 position{};
    Vec3 normal{0.0F, 0.0F, 1.0F};
    Vec4 tangent{1.0F, 0.0F, 0.0F, 1.0F};
    Vec2 texcoord0{};
    Vec2 texcoord1{};
    Vec4 color{1.0F, 1.0F, 1.0F, 1.0F};
};

enum class PrimitiveTopology
{
    triangles
};

struct Primitive
{
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    MaterialId material;
    PrimitiveTopology topology = PrimitiveTopology::triangles;
    bool has_normals = false;
    bool has_tangents = false;
    bool has_texcoord0 = false;
    bool has_texcoord1 = false;
    bool has_colors = false;
    Aabb bounds;
};

struct Mesh
{
    std::string name;
    std::vector<PrimitiveId> primitives;
    Aabb bounds;
};

enum class TransformSource
{
    identity,
    matrix,
    trs
};

struct Node
{
    std::string name;
    TransformSource transform_source = TransformSource::identity;
    Vec3 translation{};
    Quat rotation{};
    Vec3 scale{1.0F, 1.0F, 1.0F};
    Mat4 local_transform = identity_matrix();
    Mat4 world_transform = identity_matrix();
    NodeId parent;
    std::vector<NodeId> children;
    MeshId mesh;
    CameraId camera;
    LightId light;
    Aabb world_bounds;
    bool negative_determinant = false;
};

enum class AlphaMode
{
    opaque,
    mask,
    blend
};

enum class ColorSpaceIntent
{
    linear,
    srgb
};

struct TextureReference
{
    TextureId texture;
    std::uint32_t texcoord_set = 0;
    float scale = 1.0F;
};

struct Material
{
    std::string name;
    Vec4 base_color_factor{1.0F, 1.0F, 1.0F, 1.0F};
    float metallic_factor = 1.0F;
    float roughness_factor = 1.0F;
    Vec3 emissive_factor{};
    std::optional<TextureReference> base_color_texture;
    std::optional<TextureReference> metallic_roughness_texture;
    std::optional<TextureReference> normal_texture;
    std::optional<TextureReference> occlusion_texture;
    std::optional<TextureReference> emissive_texture;
    float normal_scale = 1.0F;
    float occlusion_strength = 1.0F;
    AlphaMode alpha_mode = AlphaMode::opaque;
    float alpha_cutoff = 0.5F;
    bool double_sided = false;
};

enum class FilterMode
{
    unspecified,
    nearest,
    linear,
    nearest_mipmap_nearest,
    linear_mipmap_nearest,
    nearest_mipmap_linear,
    linear_mipmap_linear
};

enum class WrapMode
{
    clamp_to_edge,
    mirrored_repeat,
    repeat
};

struct Sampler
{
    std::string name;
    FilterMode min_filter = FilterMode::unspecified;
    FilterMode mag_filter = FilterMode::unspecified;
    WrapMode wrap_s = WrapMode::repeat;
    WrapMode wrap_t = WrapMode::repeat;
};

struct Image
{
    std::string name;
    std::string source_identity;
    std::string mime_type;
    std::vector<std::byte> encoded_bytes;
    std::vector<std::byte> decoded_rgba8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t components = 4;
    std::uint64_t row_stride = 0;
};

struct Texture
{
    std::string name;
    ImageId image;
    SamplerId sampler;
    ColorSpaceIntent color_space = ColorSpaceIntent::linear;
};

enum class CameraType
{
    perspective,
    orthographic
};

struct Camera
{
    std::string name;
    CameraType type = CameraType::perspective;
    float aspect_ratio = 0.0F;
    float vertical_fov_radians = 0.785398163F;
    float near_plane = 0.01F;
    float far_plane = 1000.0F;
    float x_magnification = 1.0F;
    float y_magnification = 1.0F;
};

enum class LightType
{
    directional,
    point,
    spot
};

struct Light
{
    std::string name;
    LightType type = LightType::point;
    Vec3 color{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;
    std::optional<float> range;
    float inner_cone_angle = 0.0F;
    float outer_cone_angle = 0.785398163F;
};

struct SceneDefinition
{
    std::string name;
    std::vector<NodeId> roots;
    Aabb bounds;
};

struct DependencyRecord
{
    std::string normalized_relative_path;
    std::string sha256;
    std::uint64_t byte_size = 0;
};

struct ResourceUsage
{
    std::uint64_t source_payload_bytes = 0;
    std::uint64_t buffer_payload_bytes = 0;
    std::uint64_t encoded_image_bytes = 0;
    std::uint64_t canonical_geometry_bytes = 0;
    std::uint64_t decoded_image_bytes = 0;
    std::uint64_t retained_bytes = 0;
    std::uint64_t conservative_peak_bytes = 0;
};

struct SourceMetadata
{
    std::string display_name;
    std::string format;
    std::string version;
    std::string generator;
    std::string copyright;
    std::string source_sha256;
    std::string deterministic_asset_key;
    std::vector<std::string> extensions_used;
    std::vector<std::string> extensions_required;
    std::vector<DependencyRecord> dependencies;
    ResourceUsage resource_usage;
};

struct CanonicalScene
{
    static constexpr std::string_view schema_version = "daedalus.canonical-scene/1";

    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<Primitive> primitives;
    std::vector<Material> materials;
    Material default_material;
    std::vector<Texture> textures;
    std::vector<Image> images;
    std::vector<Sampler> samplers;
    std::vector<Camera> cameras;
    std::vector<Light> lights;
    std::vector<SceneDefinition> scenes;
    SceneDefinitionId selected_scene;
    SourceMetadata source;
};

[[nodiscard]] bool propagate_world_transforms(CanonicalScene& scene, std::string* error_message = nullptr);
void recompute_bounds(CanonicalScene& scene);
[[nodiscard]] CanonicalScene make_builtin_triangle_scene();
[[nodiscard]] std::string dump_scene_hierarchy(const CanonicalScene& scene);
}
