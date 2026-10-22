// Campaign D/E portable contract tests (source only in the D-H drop; not executed).
#include "TestHarness.h"
#include "rendering/GpuSceneLayout.h"
#include "rendering/RasterPreparation.h"
#include "rendering/RayTracingContract.h"
#include "rendering/RayTracingPreparation.h"
#include "scene/Scene.h"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace
{
using namespace daedalus;
using namespace daedalus::tests;

void test_contract_sizes()
{
    require(sizeof(RtInstanceData) == 160U, "RtInstanceData must stay 160 bytes (HLSL mirror)");
    require(sizeof(RtGeometryData) == 32U, "RtGeometryData must stay 32 bytes");
    require(sizeof(RtMaterialData) == 144U, "RtMaterialData must stay 144 bytes");
    require(sizeof(RtEmissiveTriangle) == 32U, "RtEmissiveTriangle must stay 32 bytes");
    require(sizeof(RtPassConstants) == 80U, "RtPassConstants must stay 80 bytes");
    require(kMaxPayloadBytes == 32U && kMaxAttributeBytes == 8U, "state-object payload/attribute limits");
    require(kMaxTraceRecursionDepth == 1U, "raygen-only tracing keeps recursion depth 1");
    require(kRtCounterBytes == kRtCounterCount * 4U, "counter buffer size");
}

void test_row_major_instance_transform()
{
    Mat4 matrix = identity_matrix();
    matrix.at(0, 3) = 5.0F;
    matrix.at(1, 3) = -2.0F;
    matrix.at(2, 3) = 3.0F;
    matrix.at(0, 1) = 0.25F;
    const std::array<float, 12> rows = to_row_major_3x4(matrix);
    require_near(rows[3], 5.0F, 0.0F, "Transform[0][3] is x translation");
    require_near(rows[7], -2.0F, 0.0F, "Transform[1][3] is y translation");
    require_near(rows[11], 3.0F, 0.0F, "Transform[2][3] is z translation");
    require_near(rows[1], 0.25F, 0.0F, "Transform[0][1] is row 0 column 1");
}

void test_front_face_policy()
{
    // Counter-clockwise triangle seen from +Z (glTF front face); ray travelling towards -Z hits its front.
    const Vec3 v0{0.0F, 0.0F, 0.0F};
    const Vec3 v1{1.0F, 0.0F, 0.0F};
    const Vec3 v2{0.0F, 1.0F, 0.0F};
    require(dxr_default_front_face(v0, v1, v2, {0.0F, 0.0F, -1.0F}), "CCW glTF face must be DXR front facing from +Z");
    require(!dxr_default_front_face(v0, v1, v2, {0.0F, 0.0F, 1.0F}), "same face seen from -Z is back facing");
}

void test_instance_policies()
{
    const RtInstancePolicy opaque = instance_policy(AlphaMode::opaque, false);
    require(opaque.mask == kRtMaskOpaqueOrMasked, "opaque instances use the opaque/mask ray mask");
    require((opaque.instance_flags & kDxrInstanceFlagTriangleCullDisable) == 0U, "single-sided opaque keeps culling");
    require((opaque.instance_flags & kDxrInstanceFlagTriangleFrontCounterClockwise) == 0U, "no FRONT_CCW flag (object-space facing)");
    const RtInstancePolicy double_sided = instance_policy(AlphaMode::mask, true);
    require((double_sided.instance_flags & kDxrInstanceFlagTriangleCullDisable) != 0U, "double-sided disables culling");
    const RtInstancePolicy blend = instance_policy(AlphaMode::blend, false);
    require(blend.mask == kRtMaskBlend, "blend instances are isolated by mask");
    const RtInstancePolicy transmissive = instance_policy(AlphaMode::opaque, false, true);
    require((transmissive.instance_flags & kDxrInstanceFlagTriangleCullDisable) != 0U, "transmissive interfaces are two-sided");
    require((geometry_flags_for(AlphaMode::opaque) & kDxrGeometryFlagOpaque) != 0U, "opaque geometry skips any-hit");
    require((geometry_flags_for(AlphaMode::mask) & kDxrGeometryFlagOpaque) == 0U, "masked geometry runs any-hit");
    require((geometry_flags_for(AlphaMode::mask) & kDxrGeometryFlagNoDuplicateAnyHit) != 0U, "masked any-hit must not duplicate");
}

void test_shader_table_layout()
{
    const ShaderTableLayout layout = compute_shader_table_layout(1U, 2U, 2U, 0U);
    require(layout.ray_generation.offset % kShaderTableAlignment == 0U, "raygen section aligned");
    require(layout.miss.offset % kShaderTableAlignment == 0U, "miss section aligned");
    require(layout.hit_group.offset % kShaderTableAlignment == 0U, "hit section aligned");
    require(layout.miss.stride % kShaderRecordAlignment == 0U && layout.miss.stride >= kShaderIdentifierBytes, "record stride");
    require(layout.miss.count == 2U && layout.hit_group.count == 2U, "record counts");
    require(layout.total_bytes >= layout.hit_group.offset + layout.hit_group.size, "total covers all sections");
    require(layout.miss.offset >= layout.ray_generation.offset + layout.ray_generation.size, "sections do not overlap");
    require_throws<std::invalid_argument>([] { (void)compute_shader_table_layout(0U, 1U, 1U, 0U); }, "empty raygen section rejected");
    require_throws<std::invalid_argument>([] { (void)compute_shader_table_layout(1U, 1U, 1U, 5U); }, "misaligned local arguments rejected");
}

void test_cornell_tables()
{
    const CanonicalScene scene = make_builtin_cornell_box_scene();
    const std::vector<PreparedRasterDraw> draws = prepare_raster_draws(scene);
    const GpuSceneDescriptorLayout layout = GpuSceneDescriptorLayout::create(scene.textures.size(), scene.primitives.size(), scene.samplers.size());
    const RtSceneTables tables = prepare_ray_tracing_tables(scene, draws, layout);
    require(tables.instances.size() == draws.size(), "one TLAS instance per raster draw");
    require(tables.instance_policies.size() == tables.instances.size(), "one policy per instance");
    require(tables.geometries.size() == scene.primitives.size(), "geometry table indexed by primitive");
    require(tables.materials.size() == scene.materials.size() + 1U, "material table has the default material at index 0");
    require(!tables.emissive_triangles.empty(), "Cornell box light must produce emissive triangles");
    require(!tables.emissive_budget_exceeded, "Cornell box stays within the emissive budget");
    float pmf_sum = 0.0F;
    for (const RtEmissiveTriangle& triangle : tables.emissive_triangles) pmf_sum += triangle.pmf;
    require_near(pmf_sum, 1.0F, 1.0e-4F, "emissive triangle PMF sums to one");

    const RtSceneTables limited = prepare_ray_tracing_tables(scene, draws, layout, 1U);
    require(limited.emissive_budget_exceeded || limited.emissive_triangles.size() <= 1U, "emissive budget is enforced");
}

void test_raw_buffer_descriptors_are_table_relative()
{
    // Audit D-H-STATIC-001: g_buffers[] is bound at srv_gpu(layout.raw_base()), so RtGeometryData
    // must carry table-relative indices. The alpha scene has textures, making raw_base() > 2.
    const CanonicalScene scene = make_builtin_alpha_instancing_scene();
    require(!scene.textures.empty(), "fixture scene must have textures so raw_base() is non-trivial");
    const std::vector<PreparedRasterDraw> draws = prepare_raster_draws(scene);
    const GpuSceneDescriptorLayout layout = GpuSceneDescriptorLayout::create(scene.textures.size(), scene.primitives.size(), scene.samplers.size());
    require(layout.raw_base() > GpuSceneDescriptorLayout::kTextureBase, "raw region starts after the texture region");
    const RtSceneTables tables = prepare_ray_tracing_tables(scene, draws, layout);
    for (std::size_t index = 0; index < tables.geometries.size(); ++index)
    {
        const RtGeometryData& geometry = tables.geometries[index];
        const auto primitive = static_cast<std::uint32_t>(index);
        require(geometry.vertex_buffer_descriptor == primitive * 2U, "vertex buffer index is relative to the g_buffers table base");
        require(geometry.index_buffer_descriptor == primitive * 2U + 1U, "index buffer index is relative to the g_buffers table base");
        require(layout.raw_base() + geometry.vertex_buffer_descriptor == layout.vertex_buffer(primitive), "relative + base == absolute (vertex)");
        require(layout.raw_base() + geometry.index_buffer_descriptor == layout.index_buffer(primitive), "relative + base == absolute (index)");
        require(geometry.index_buffer_descriptor < layout.raw_buffer_table_size(), "index stays inside the bound table");
    }
}

void test_instance_transform_update()
{
    const CanonicalScene scene = make_builtin_alpha_instancing_scene();
    const std::vector<PreparedRasterDraw> draws = prepare_raster_draws(scene);
    const GpuSceneDescriptorLayout layout = GpuSceneDescriptorLayout::create(scene.textures.size(), scene.primitives.size(), scene.samplers.size());
    RtSceneTables tables = prepare_ray_tracing_tables(scene, draws, layout);
    std::vector<Mat4> current(tables.instances.size(), translation_matrix({1.0F, 2.0F, 3.0F}));
    std::vector<Mat4> previous(tables.instances.size(), identity_matrix());
    update_instance_transforms(tables.instances, current, previous);
    require(!tables.instances.empty(), "alpha scene has instances");
    require_near(tables.instances.front().object_to_world[0].w, 1.0F, 0.0F, "current transform x translation written");
    require_near(tables.instances.front().previous_object_to_world[0].w, 0.0F, 0.0F, "previous transform kept separately");
}
}

int main()
{
    return daedalus::tests::run({
        {"ray-tracing contract sizes", test_contract_sizes},
        {"row-major instance transform", test_row_major_instance_transform},
        {"DXR front-face policy", test_front_face_policy},
        {"instance and geometry policies", test_instance_policies},
        {"shader table layout", test_shader_table_layout},
        {"Cornell box ray-tracing tables", test_cornell_tables},
        {"raw buffer descriptors are table-relative", test_raw_buffer_descriptors_are_table_relative},
        {"instance transform update", test_instance_transform_update}});
}
