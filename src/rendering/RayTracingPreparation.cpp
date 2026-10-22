#include "rendering/RayTracingPreparation.h"

#include "rendering/PathTracingMath.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace daedalus
{
namespace
{
[[nodiscard]] std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment)
{
    if (value > std::numeric_limits<std::uint64_t>::max() - (alignment - 1U))
        throw std::overflow_error("shader table size overflow");
    return (value + alignment - 1U) / alignment * alignment;
}

[[nodiscard]] float luminance(Vec3 value) noexcept
{
    return 0.2126F * value.x + 0.7152F * value.y + 0.0722F * value.z;
}

void bind_texture(const CanonicalScene& scene,
                  const GpuSceneDescriptorLayout& layout,
                  const std::optional<TextureReference>& reference,
                  bool srgb,
                  std::uint32_t slot,
                  std::uint32_t flag,
                  std::uint32_t& texture_descriptor,
                  std::uint32_t& sampler_descriptor,
                  RtMaterialData& material)
{
    texture_descriptor = srgb ? GpuSceneDescriptorLayout::kWhiteSrgb : GpuSceneDescriptorLayout::kWhiteLinear;
    sampler_descriptor = layout.default_sampler();
    if (!reference.has_value()) return;
    if (!reference->texture.valid() || reference->texture.value() >= scene.textures.size())
        throw std::runtime_error("canonical material references an invalid texture for DXR preparation");
    if (reference->texcoord_set > 1U)
        throw std::runtime_error("canonical material references an unsupported TEXCOORD set for DXR preparation");
    const std::uint32_t texture = reference->texture.value();
    texture_descriptor = srgb ? layout.texture_srgb(texture) : layout.texture_linear(texture);
    const Texture& source = scene.textures[texture];
    sampler_descriptor = source.sampler.valid() ? layout.scene_sampler(source.sampler.value()) : layout.default_sampler();
    if (reference->texcoord_set == 1U) material.texcoord_sets |= 1U << slot;
    material.flags |= flag;
}

[[nodiscard]] RtMaterialData make_material(const CanonicalScene& scene,
                                           const GpuSceneDescriptorLayout& layout,
                                           const Material& source,
                                           std::uint32_t diagnostic_id)
{
    if (!std::isfinite(source.emissive_strength) || source.emissive_strength < 0.0F || !std::isfinite(source.ior) ||
        source.ior < 1.0F || !std::isfinite(source.transmission_factor) || source.transmission_factor < 0.0F ||
        source.transmission_factor > 1.0F || !std::isfinite(source.thickness_factor) || source.thickness_factor < 0.0F ||
        !std::isfinite(source.attenuation_distance) || source.attenuation_distance < 0.0F || !finite(source.attenuation_color))
        throw std::runtime_error("canonical material extension factors are invalid for DXR preparation: " + source.name);
    RtMaterialData material;
    material.base_color_factor = source.base_color_factor;
    const Vec3 emissive = source.emissive_factor * source.emissive_strength;
    material.emissive_metallic = {emissive.x, emissive.y, emissive.z, source.metallic_factor};
    material.roughness_normal_ao_cutoff = {source.roughness_factor, source.normal_scale, source.occlusion_strength, source.alpha_cutoff};
    material.transmission_ior = {source.transmission_factor, source.ior, source.thickness_factor, source.attenuation_distance};
    material.attenuation_color = {source.attenuation_color.x, source.attenuation_color.y, source.attenuation_color.z, 0.0F};
    switch (source.alpha_mode)
    {
    case AlphaMode::opaque: break;
    case AlphaMode::mask: material.flags |= 1U; break;
    case AlphaMode::blend: material.flags |= 2U; break;
    }
    if (source.double_sided) material.flags |= kRtMaterialDoubleSided;
    if (source.transmission_factor > 0.0F) material.flags |= kRtMaterialTransmission;
    if (source.transmission_factor > 0.0F && source.thickness_factor > 0.0F) material.flags |= kRtMaterialVolume;
    bind_texture(scene, layout, source.base_color_texture, true, 0U, kRtMaterialBaseColorTexture,
                 material.base_color_texture, material.base_color_sampler, material);
    bind_texture(scene, layout, source.metallic_roughness_texture, false, 1U, kRtMaterialMetallicRoughnessTexture,
                 material.metallic_roughness_texture, material.metallic_roughness_sampler, material);
    bind_texture(scene, layout, source.normal_texture, false, 2U, kRtMaterialNormalTexture,
                 material.normal_texture, material.normal_sampler, material);
    bind_texture(scene, layout, source.occlusion_texture, false, 3U, kRtMaterialOcclusionTexture,
                 material.occlusion_texture, material.occlusion_sampler, material);
    bind_texture(scene, layout, source.emissive_texture, true, 4U, kRtMaterialEmissiveTexture,
                 material.emissive_texture, material.emissive_sampler, material);
    material.diagnostic_id = diagnostic_id;
    return material;
}
}

std::array<float, 12> to_row_major_3x4(const Mat4& matrix) noexcept
{
    std::array<float, 12> result{};
    for (std::size_t row = 0; row < 3U; ++row)
        for (std::size_t column = 0; column < 4U; ++column)
            result[row * 4U + column] = matrix.at(row, column);
    return result;
}

std::array<Vec4, 3> to_row_vectors_3x4(const Mat4& matrix) noexcept
{
    std::array<Vec4, 3> rows{};
    for (std::size_t row = 0; row < 3U; ++row)
        rows[row] = {matrix.at(row, 0), matrix.at(row, 1), matrix.at(row, 2), matrix.at(row, 3)};
    return rows;
}

std::array<Vec4, 3> normal_rows(const Mat4& matrix) noexcept
{
    bool invertible = false;
    const Mat4 normal_matrix = inverse_transpose(matrix, &invertible);
    const Mat4 source = invertible ? normal_matrix : identity_matrix();
    std::array<Vec4, 3> rows{};
    for (std::size_t row = 0; row < 3U; ++row)
        rows[row] = {source.at(row, 0), source.at(row, 1), source.at(row, 2), 0.0F};
    return rows;
}

bool dxr_default_front_face(Vec3 v0, Vec3 v1, Vec3 v2, Vec3 object_ray_direction) noexcept
{
    return dot(cross(v1 - v0, v2 - v0), object_ray_direction) < 0.0F;
}

RtInstancePolicy instance_policy(AlphaMode alpha_mode, bool double_sided, bool transmissive) noexcept
{
    RtInstancePolicy policy;
    // BLEND geometry never casts raster shadows (Campaign C contract); hybrid visibility rays use
    // kRtMaskOpaqueOrMasked to stay consistent, while radiance/path rays include all instances.
    policy.mask = alpha_mode == AlphaMode::blend ? kRtMaskBlend : kRtMaskOpaqueOrMasked;
    policy.instance_flags = (double_sided || transmissive) ? kDxrInstanceFlagTriangleCullDisable : 0U;
    return policy;
}

std::uint32_t geometry_flags_for(AlphaMode alpha_mode) noexcept
{
    // OPAQUE skips any-hit invocation entirely. MASK/BLEND need any-hit alpha evaluation and must not
    // be invoked twice for the same triangle (stochastic coverage/alpha tests are not idempotent
    // with respect to the counters and must be evaluated exactly once).
    return alpha_mode == AlphaMode::opaque ? kDxrGeometryFlagOpaque : kDxrGeometryFlagNoDuplicateAnyHit;
}

RtSceneTables prepare_ray_tracing_tables(const CanonicalScene& scene,
                                         std::span<const PreparedRasterDraw> draws,
                                         const GpuSceneDescriptorLayout& layout,
                                         std::uint64_t emissive_triangle_budget)
{
    if (layout.primitive_count != scene.primitives.size() || layout.texture_count != scene.textures.size() ||
        layout.sampler_count != scene.samplers.size())
        throw std::runtime_error("DXR preparation descriptor layout does not match the canonical scene");
    if (draws.size() > 0x00FFFFFFU)
        throw std::runtime_error("scene exceeds the 24-bit DXR InstanceID range");

    RtSceneTables tables;
    tables.materials.reserve(scene.materials.size() + 1U);
    tables.materials.push_back(make_material(scene, layout, scene.default_material, kDefaultMaterialDiagnosticId));
    for (std::size_t index = 0; index < scene.materials.size(); ++index)
        tables.materials.push_back(make_material(scene, layout, scene.materials[index],
                                                 stable_material_diagnostic_id(MaterialId(static_cast<std::uint32_t>(index)))));

    tables.geometries.reserve(scene.primitives.size());
    tables.geometry_flags.reserve(scene.primitives.size());
    for (std::size_t index = 0; index < scene.primitives.size(); ++index)
    {
        const Primitive& primitive = scene.primitives[index];
        if (primitive.vertices.empty() || primitive.indices.empty() || primitive.indices.size() % 3U != 0U)
            throw std::runtime_error("canonical primitive is not a non-empty indexed triangle list");
        if (primitive.vertices.size() > 0xFFFFFFFFULL || primitive.indices.size() > 0xFFFFFFFFULL)
            throw std::runtime_error("canonical primitive exceeds 32-bit vertex/index counts");
        if (primitive.material.valid() && primitive.material.value() >= scene.materials.size())
            throw std::runtime_error("canonical primitive references an invalid material");
        RtGeometryData geometry;
        const auto primitive_index = static_cast<std::uint32_t>(index);
        // Table-relative: g_buffers[] (t0, space2) is bound with its base at layout.raw_base().
        geometry.vertex_buffer_descriptor = layout.vertex_buffer_table_index(primitive_index);
        geometry.index_buffer_descriptor = layout.index_buffer_table_index(primitive_index);
        if (geometry.index_buffer_descriptor >= layout.raw_buffer_table_size())
            throw std::logic_error("DXR raw-buffer descriptor index exceeds the bound g_buffers table");
        geometry.vertex_count = static_cast<std::uint32_t>(primitive.vertices.size());
        geometry.index_count = static_cast<std::uint32_t>(primitive.indices.size());
        geometry.vertex_stride = static_cast<std::uint32_t>(sizeof(Vertex));
        geometry.primitive_index = primitive_index;
        if (primitive.has_normals) geometry.flags |= kRtGeometryHasNormals;
        if (primitive.has_tangents) geometry.flags |= kRtGeometryHasTangents;
        if (primitive.has_texcoord1) geometry.flags |= kRtGeometryHasTexcoord1;
        if (primitive.has_colors) geometry.flags |= kRtGeometryHasColors;
        tables.geometries.push_back(geometry);
        const Material& material = primitive.material.valid() ? scene.materials[primitive.material.value()] : scene.default_material;
        tables.geometry_flags.push_back(geometry_flags_for(material.alpha_mode));
    }

    std::vector<double> emissive_weights;
    tables.instances.reserve(draws.size());
    tables.instance_policies.reserve(draws.size());
    for (const PreparedRasterDraw& draw : draws)
    {
        if (draw.primitive_index >= scene.primitives.size())
            throw std::runtime_error("prepared draw references an invalid primitive for DXR preparation");
        if (draw.material_table_index >= tables.materials.size())
            throw std::runtime_error("prepared draw references an invalid material table entry");
        RtInstanceData instance;
        instance.object_to_world = to_row_vectors_3x4(draw.world);
        instance.previous_object_to_world = instance.object_to_world;
        instance.normal_to_world = normal_rows(draw.world);
        instance.geometry_index = draw.primitive_index;
        instance.material_index = draw.material_table_index;
        if (draw.negative_determinant) instance.flags |= kRtInstanceNegativeDeterminant;
        if (draw.double_sided) instance.flags |= kRtInstanceDoubleSided;
        if (draw.alpha_mode == AlphaMode::mask) instance.flags |= kRtInstanceAlphaMask;
        if (draw.alpha_mode == AlphaMode::blend) instance.flags |= kRtInstanceAlphaBlend;

        const float emitted = luminance(draw.emissive_factor);
        if (emitted > 0.0F && std::isfinite(emitted))
        {
            instance.flags |= kRtInstanceEmissive;
            const Primitive& primitive = scene.primitives[draw.primitive_index];
            const std::uint64_t triangles = primitive.indices.size() / 3U;
            tables.emissive_candidate_triangles += triangles;
            if (!tables.emissive_budget_exceeded && emissive_weights.size() + triangles <= emissive_triangle_budget)
            {
                instance.emissive_triangle_base = static_cast<std::uint32_t>(emissive_weights.size());
                const auto instance_index = static_cast<std::uint32_t>(tables.instances.size());
                for (std::uint64_t triangle = 0; triangle < triangles; ++triangle)
                {
                    const Vec3 a = transform_point(draw.world, primitive.vertices[primitive.indices[triangle * 3U]].position);
                    const Vec3 b = transform_point(draw.world, primitive.vertices[primitive.indices[triangle * 3U + 1U]].position);
                    const Vec3 c = transform_point(draw.world, primitive.vertices[primitive.indices[triangle * 3U + 2U]].position);
                    const float area = 0.5F * length(cross(b - a, c - a));
                    emissive_weights.push_back(std::isfinite(area) ? static_cast<double>(area) * emitted : 0.0);
                    RtEmissiveTriangle entry;
                    entry.instance_index = instance_index;
                    entry.triangle_index = static_cast<std::uint32_t>(triangle);
                    tables.emissive_triangles.push_back(entry);
                }
            }
            else
            {
                tables.emissive_budget_exceeded = true;
            }
        }
        tables.instances.push_back(instance);
        const bool transmissive = (tables.materials[draw.material_table_index].flags & kRtMaterialTransmission) != 0U;
        tables.instance_policies.push_back(instance_policy(draw.alpha_mode, draw.double_sided, transmissive));
    }

    if (tables.emissive_budget_exceeded)
    {
        // Partial light tables would silently bias light selection toward early instances; emissive
        // NEE is disabled entirely and BSDF-sampled emission remains (unbiased, higher variance).
        tables.emissive_triangles.clear();
        for (RtInstanceData& instance : tables.instances) instance.emissive_triangle_base = 0xFFFFFFFFU;
    }
    else if (!tables.emissive_triangles.empty())
    {
        double total = 0.0;
        for (const double weight : emissive_weights) total += weight;
        if (total > 0.0)
        {
            const std::vector<AliasEntry> alias = build_alias_table(emissive_weights);
            for (std::size_t index = 0; index < alias.size(); ++index)
            {
                tables.emissive_triangles[index].alias_threshold = alias[index].threshold;
                tables.emissive_triangles[index].alias = alias[index].alias;
                tables.emissive_triangles[index].pmf = alias[index].pmf;
            }
        }
        else
        {
            tables.emissive_triangles.clear();
            for (RtInstanceData& instance : tables.instances) instance.emissive_triangle_base = 0xFFFFFFFFU;
        }
    }
    return tables;
}

void update_instance_transforms(std::span<RtInstanceData> instances,
                                std::span<const Mat4> current_world,
                                std::span<const Mat4> previous_world)
{
    if (instances.size() != current_world.size() || instances.size() != previous_world.size())
        throw std::invalid_argument("instance transform update spans must match the instance count");
    for (std::size_t index = 0; index < instances.size(); ++index)
    {
        instances[index].object_to_world = to_row_vectors_3x4(current_world[index]);
        instances[index].previous_object_to_world = to_row_vectors_3x4(previous_world[index]);
        instances[index].normal_to_world = normal_rows(current_world[index]);
    }
}

ShaderTableLayout compute_shader_table_layout(std::uint32_t ray_generation_records,
                                              std::uint32_t miss_records,
                                              std::uint32_t hit_group_records,
                                              std::uint32_t local_root_argument_bytes)
{
    if (ray_generation_records == 0U || miss_records == 0U || hit_group_records == 0U)
        throw std::invalid_argument("shader tables require at least one ray-generation, miss and hit-group record");
    if (local_root_argument_bytes % 4U != 0U)
        throw std::invalid_argument("local root arguments must be a multiple of four bytes");
    const std::uint64_t stride = align_up(kShaderIdentifierBytes + local_root_argument_bytes, kShaderRecordAlignment);
    if (stride > kMaxShaderRecordStride)
        throw std::invalid_argument("shader record stride exceeds the D3D12 maximum of 4096 bytes");

    ShaderTableLayout layout;
    std::uint64_t cursor = 0;
    auto section = [&](std::uint32_t count)
    {
        ShaderTableSection result;
        result.offset = align_up(cursor, kShaderTableAlignment);
        result.stride = stride;
        result.count = count;
        result.size = stride * count;
        cursor = result.offset + result.size;
        return result;
    };
    layout.ray_generation = section(ray_generation_records);
    layout.miss = section(miss_records);
    layout.hit_group = section(hit_group_records);
    layout.total_bytes = align_up(cursor, kShaderTableAlignment);
    return layout;
}
}
