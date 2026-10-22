#pragma once

// API-independent preparation of every CPU-side table consumed by the DXR passes. The canonical
// scene stays authoritative; this module only derives GPU-shaped records from it and from the
// prepared raster draw list so that instance IDs agree across raster, hybrid, path tracing and
// temporal diagnostics (instance i == prepared raster draw i == TLAS InstanceID() i).

#include "rendering/GpuSceneLayout.h"
#include "rendering/RasterPreparation.h"
#include "rendering/RayTracingContract.h"
#include "scene/Scene.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace daedalus
{
struct RtInstancePolicy
{
    std::uint8_t mask = kRtMaskOpaqueOrMasked;
    std::uint32_t instance_flags = 0;  // kDxrInstanceFlag* values
};

struct RtSceneTables
{
    std::vector<RtInstanceData> instances;
    std::vector<RtInstancePolicy> instance_policies;
    std::vector<RtGeometryData> geometries;          // indexed by canonical primitive
    std::vector<std::uint32_t> geometry_flags;       // kDxrGeometryFlag* per primitive
    std::vector<RtMaterialData> materials;           // 0 = default material
    std::vector<RtEmissiveTriangle> emissive_triangles;
    std::uint64_t emissive_candidate_triangles = 0;
    bool emissive_budget_exceeded = false;
};

// Default emissive-triangle budget: 4 Mi entries * 32 bytes = 128 MiB, bounded for an 8 GB GPU.
inline constexpr std::uint64_t kDefaultEmissiveTriangleBudget = 4ULL * 1024ULL * 1024ULL;

// Row-major 3x4 copy of the affine part of a column-major Mat4, laid out exactly as
// D3D12_RAYTRACING_INSTANCE_DESC::Transform[3][4] expects (Transform[row][column]).
[[nodiscard]] std::array<float, 12> to_row_major_3x4(const Mat4& matrix) noexcept;
[[nodiscard]] std::array<Vec4, 3> to_row_vectors_3x4(const Mat4& matrix) noexcept;
[[nodiscard]] std::array<Vec4, 3> normal_rows(const Mat4& matrix) noexcept;

// Facing policy. DXR determines facing in object space: a triangle is front facing when its
// vertices appear clockwise from the ray origin in a left-handed system, i.e. when
// dot(cross(v1 - v0, v2 - v0), ray_direction) < 0. glTF front faces are counter-clockwise in a
// right-handed system, which is the same algebraic condition, so no FRONT_COUNTERCLOCKWISE flag is
// needed. Because the test is in object space, negative-determinant instances keep their outward
// faces front facing (unlike raster, which must flip the front-face winding for mirrored draws).
[[nodiscard]] bool dxr_default_front_face(Vec3 v0, Vec3 v1, Vec3 v2, Vec3 object_ray_direction) noexcept;

// Transmissive materials are traversed as two-sided interfaces (culling disabled) so rays inside a
// refractive volume can hit and exit through back faces; their raster draw state is unchanged.
[[nodiscard]] RtInstancePolicy instance_policy(AlphaMode alpha_mode, bool double_sided, bool transmissive = false) noexcept;
[[nodiscard]] std::uint32_t geometry_flags_for(AlphaMode alpha_mode) noexcept;

[[nodiscard]] RtSceneTables prepare_ray_tracing_tables(const CanonicalScene& scene,
                                                       std::span<const PreparedRasterDraw> draws,
                                                       const GpuSceneDescriptorLayout& layout,
                                                       std::uint64_t emissive_triangle_budget = kDefaultEmissiveTriangleBudget);

// Rewrites current/previous object transforms for every instance; spans must match instance count.
void update_instance_transforms(std::span<RtInstanceData> instances,
                                std::span<const Mat4> current_world,
                                std::span<const Mat4> previous_world);

// ---- Shader binding table layout --------------------------------------------------------------
// Mirrors of D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT,
// D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT and D3D12_RAYTRACING_MAX_SHADER_RECORD_STRIDE
// (static_asserted against d3d12.h in the graphics layer).
inline constexpr std::uint64_t kShaderIdentifierBytes = 32U;
inline constexpr std::uint64_t kShaderRecordAlignment = 32U;
inline constexpr std::uint64_t kShaderTableAlignment = 64U;
inline constexpr std::uint64_t kMaxShaderRecordStride = 4096U;

struct ShaderTableSection
{
    std::uint64_t offset = 0;
    std::uint64_t stride = 0;
    std::uint64_t count = 0;
    std::uint64_t size = 0;
};

struct ShaderTableLayout
{
    ShaderTableSection ray_generation;
    ShaderTableSection miss;
    ShaderTableSection hit_group;
    std::uint64_t total_bytes = 0;
};

// Throws std::invalid_argument for empty sections, misaligned local argument sizes or strides
// above the D3D12 maximum.
[[nodiscard]] ShaderTableLayout compute_shader_table_layout(std::uint32_t ray_generation_records,
                                                            std::uint32_t miss_records,
                                                            std::uint32_t hit_group_records,
                                                            std::uint32_t local_root_argument_bytes);
}
