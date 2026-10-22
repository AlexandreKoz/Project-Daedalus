#pragma once

// C++ side of shaders/raytracing/RayTracingCommon.hlsli. These are DXR-specific structures and
// deliberately do not reuse the packed raster constant blocks. Field order/offsets are frozen by
// static_assert and mirrored field-for-field in HLSL.

#include "scene/Scene.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace daedalus
{
// StructuredBuffer<RtInstanceData> (t1): one entry per TLAS instance; TLAS InstanceID() == index.
struct alignas(16) RtInstanceData
{
    std::array<Vec4, 3> object_to_world{};           // rows of the 3x4 affine object->world matrix
    std::array<Vec4, 3> previous_object_to_world{};  // previous frame (equal to current when static)
    std::array<Vec4, 3> normal_to_world{};           // rows of inverse-transpose(object->world) 3x3 (w = 0)
    std::uint32_t geometry_index = 0;
    std::uint32_t material_index = 0;                // 0 = default material, i + 1 = source material i
    std::uint32_t flags = 0;
    std::uint32_t emissive_triangle_base = 0xFFFFFFFFU; // first RtEmissiveTriangle entry for this instance
};

// StructuredBuffer<RtGeometryData> (t2): one entry per canonical primitive (one BLAS each).
struct alignas(16) RtGeometryData
{
    std::uint32_t vertex_buffer_descriptor = 0;  // index into g_buffers[] (t0 space2), RELATIVE to GpuSceneDescriptorLayout::raw_base()
    std::uint32_t index_buffer_descriptor = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    std::uint32_t vertex_stride = 0;             // bytes; must equal sizeof(Vertex) == 72
    std::uint32_t flags = 0;
    std::uint32_t primitive_index = 0;
    std::uint32_t padding0 = 0;
};

// StructuredBuffer<RtMaterialData> (t3): index 0 is the canonical default material.
struct alignas(16) RtMaterialData
{
    Vec4 base_color_factor{1.0F, 1.0F, 1.0F, 1.0F};
    Vec4 emissive_metallic{};           // rgb emissive factor * emissive strength, w metallic
    Vec4 roughness_normal_ao_cutoff{};  // roughness, normal scale, occlusion strength, alpha cutoff
    Vec4 transmission_ior{};            // x transmission factor, y IOR, z thickness factor, w attenuation distance (0 = none)
    Vec4 attenuation_color{1.0F, 1.0F, 1.0F, 0.0F};
    std::uint32_t base_color_texture = 1;          // sRGB view (white sRGB if absent)
    std::uint32_t metallic_roughness_texture = 0;  // linear view (white linear if absent)
    std::uint32_t normal_texture = 0;
    std::uint32_t occlusion_texture = 0;
    std::uint32_t emissive_texture = 1;            // sRGB view
    std::uint32_t base_color_sampler = 0;
    std::uint32_t metallic_roughness_sampler = 0;
    std::uint32_t normal_sampler = 0;
    std::uint32_t occlusion_sampler = 0;
    std::uint32_t emissive_sampler = 0;
    std::uint32_t texcoord_sets = 0;               // bit i set => slot i (base, mr, normal, occlusion, emissive) uses TEXCOORD_1
    std::uint32_t flags = 0;
    std::uint32_t diagnostic_id = 0;
    std::uint32_t padding0 = 0;
    std::uint32_t padding1 = 0;
    std::uint32_t padding2 = 0;
};

// StructuredBuffer<RtEmissiveTriangle> (t4): power-proportional Vose alias table over emissive triangles.
struct alignas(16) RtEmissiveTriangle
{
    std::uint32_t instance_index = 0;
    std::uint32_t triangle_index = 0;
    float alias_threshold = 1.0F;   // accept bucket i if u < threshold, else take alias
    std::uint32_t alias = 0;
    float pmf = 0.0F;               // selection probability of this triangle
    float padding0 = 0.0F;
    float padding1 = 0.0F;
    float padding2 = 0.0F;
};

// ConstantBuffer<RtPassConstants> (b2): per-dispatch integrator/hybrid settings.
struct alignas(16) RtPassConstants
{
    std::uint32_t light_count = 0;
    std::uint32_t instance_count = 0;
    std::uint32_t emissive_triangle_count = 0;
    std::uint32_t flags = 0;
    std::uint32_t sample_base = 0;          // accumulated samples per pixel before this dispatch
    std::uint32_t samples_per_dispatch = 1;
    std::uint32_t max_depth = 8;
    std::uint32_t min_russian_roulette_depth = 3;
    std::uint32_t seed = 0;
    std::uint32_t debug_view = 0;
    std::uint32_t frame_index = 0;
    std::uint32_t accumulation_reset = 0;
    float firefly_clamp = 0.0F;
    float reflection_max_roughness = 0.5F;
    float ray_extent = 1.0e30F;             // finite ray length used for directional/environment rays
    float environment_sun_probability = 0.5F;
    float probability_punctual = 0.0F;
    float probability_emissive = 0.0F;
    float probability_environment = 0.0F;
    std::uint32_t display_sample_target = 0; // normalization for the sample-count debug view
};

// Instance flags (RtInstanceData::flags).
inline constexpr std::uint32_t kRtInstanceNegativeDeterminant = 1U << 0U;
inline constexpr std::uint32_t kRtInstanceDoubleSided = 1U << 1U;
inline constexpr std::uint32_t kRtInstanceAlphaMask = 1U << 2U;
inline constexpr std::uint32_t kRtInstanceAlphaBlend = 1U << 3U;
inline constexpr std::uint32_t kRtInstanceEmissive = 1U << 4U;

// Geometry flags (RtGeometryData::flags).
inline constexpr std::uint32_t kRtGeometryHasNormals = 1U << 0U;
inline constexpr std::uint32_t kRtGeometryHasTangents = 1U << 1U;
inline constexpr std::uint32_t kRtGeometryHasTexcoord1 = 1U << 2U;
inline constexpr std::uint32_t kRtGeometryHasColors = 1U << 3U;

// Material flags (RtMaterialData::flags). Bits 0-1: alpha mode (0 opaque, 1 mask, 2 blend).
inline constexpr std::uint32_t kRtMaterialAlphaModeMask = 0x3U;
inline constexpr std::uint32_t kRtMaterialDoubleSided = 1U << 2U;
inline constexpr std::uint32_t kRtMaterialBaseColorTexture = 1U << 3U;
inline constexpr std::uint32_t kRtMaterialMetallicRoughnessTexture = 1U << 4U;
inline constexpr std::uint32_t kRtMaterialNormalTexture = 1U << 5U;
inline constexpr std::uint32_t kRtMaterialOcclusionTexture = 1U << 6U;
inline constexpr std::uint32_t kRtMaterialEmissiveTexture = 1U << 7U;
inline constexpr std::uint32_t kRtMaterialTransmission = 1U << 8U;
inline constexpr std::uint32_t kRtMaterialVolume = 1U << 9U;       // solid refractive interface (thickness > 0)

// RtPassConstants::flags.
inline constexpr std::uint32_t kRtPassShadows = 1U << 0U;
inline constexpr std::uint32_t kRtPassReflections = 1U << 1U;
inline constexpr std::uint32_t kRtPassPunctualNee = 1U << 2U;
inline constexpr std::uint32_t kRtPassEmissiveNee = 1U << 3U;
inline constexpr std::uint32_t kRtPassEnvironmentNee = 1U << 4U;
inline constexpr std::uint32_t kRtPassDenoiserOutputs = 1U << 5U;  // write demodulated signal + G-buffer, centre primary rays
inline constexpr std::uint32_t kRtPassAccumulate = 1U << 6U;       // progressive accumulation enabled (target not reached)

// Instance masks used in TraceRay InstanceInclusionMask.
inline constexpr std::uint8_t kRtMaskOpaqueOrMasked = 0x01U;
inline constexpr std::uint8_t kRtMaskBlend = 0x02U;
inline constexpr std::uint8_t kRtMaskAll = 0xFFU;

// Mirrors of D3D12 enum values (static_asserted against d3d12.h in graphics code) so the instance
// policy can be tested portably.
inline constexpr std::uint32_t kDxrInstanceFlagTriangleCullDisable = 0x1U;
inline constexpr std::uint32_t kDxrInstanceFlagTriangleFrontCounterClockwise = 0x2U;
inline constexpr std::uint32_t kDxrGeometryFlagOpaque = 0x1U;
inline constexpr std::uint32_t kDxrGeometryFlagNoDuplicateAnyHit = 0x2U;

// Ray types / shader-table indices. Each BLAS contains exactly one geometry, so
// GeometryContributionToHitGroupIndex is always 0 and the hit-record index is the ray type.
inline constexpr std::uint32_t kRayTypeRadiance = 0U;
inline constexpr std::uint32_t kRayTypeVisibility = 1U;
inline constexpr std::uint32_t kRayTypeCount = 2U;

// Payload/attribute contract (D3D12_RAYTRACING_SHADER_CONFIG). Radiance payload carries hit
// information only (barycentrics, t, instance, primitive, facing, any-hit RNG seed); shading is
// performed in the ray-generation shader, so MaxTraceRecursionDepth is 1.
inline constexpr std::uint32_t kRadiancePayloadBytes = 32U;
inline constexpr std::uint32_t kVisibilityPayloadBytes = 8U;
inline constexpr std::uint32_t kMaxPayloadBytes = 32U;
inline constexpr std::uint32_t kMaxAttributeBytes = 8U;
inline constexpr std::uint32_t kMaxTraceRecursionDepth = 1U;

// Ray-tracing counters (RWByteAddressBuffer, uint32 each).
enum class RtCounter : std::uint32_t
{
    primary_samples = 0,
    path_segments,
    nonfinite_radiance,
    nonfinite_throughput,
    invalid_pdf,
    negative_radiance,
    clamped_samples,
    terminated_russian_roulette,
    terminated_max_depth,
    terminated_miss,
    terminated_absorbed,
    visibility_rays,
    total_internal_reflections,
    light_samples,
    reflection_rays,
    reserved0,
    count
};
inline constexpr std::uint32_t kRtCounterCount = static_cast<std::uint32_t>(RtCounter::count);
inline constexpr std::uint32_t kRtCounterBytes = kRtCounterCount * 4U;

static_assert(sizeof(Vertex) == 72, "DXR vertex fetch assumes the canonical 72-byte vertex");
static_assert(alignof(RtInstanceData) == 16);
static_assert(offsetof(RtInstanceData, previous_object_to_world) == 48);
static_assert(offsetof(RtInstanceData, normal_to_world) == 96);
static_assert(offsetof(RtInstanceData, geometry_index) == 144);
static_assert(offsetof(RtInstanceData, emissive_triangle_base) == 156);
static_assert(sizeof(RtInstanceData) == 160);
static_assert(sizeof(RtGeometryData) == 32);
static_assert(offsetof(RtMaterialData, transmission_ior) == 48);
static_assert(offsetof(RtMaterialData, base_color_texture) == 80);
static_assert(offsetof(RtMaterialData, texcoord_sets) == 120);
static_assert(offsetof(RtMaterialData, flags) == 124);
static_assert(offsetof(RtMaterialData, diagnostic_id) == 128);
static_assert(sizeof(RtMaterialData) == 144);
static_assert(sizeof(RtEmissiveTriangle) == 32);
static_assert(offsetof(RtPassConstants, firefly_clamp) == 48);
static_assert(offsetof(RtPassConstants, probability_punctual) == 64);
static_assert(sizeof(RtPassConstants) == 80);
static_assert(kMaxPayloadBytes >= kRadiancePayloadBytes && kMaxPayloadBytes >= kVisibilityPayloadBytes);
}
