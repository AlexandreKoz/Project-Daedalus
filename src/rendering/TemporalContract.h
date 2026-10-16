#pragma once

// C++ side of shaders/common/FrameContract.hlsli. Every field offset is frozen by static_assert;
// the HLSL struct must keep identical order and packing (16-byte register rules, column-major
// float4x4 matching Mat4's column-major storage).

#include "scene/Math.h"

#include <cstddef>
#include <cstdint>

namespace daedalus
{
struct alignas(16) GpuViewConstants
{
    Mat4 view{};                                  // world -> view (right-handed, camera looks down -Z)
    Mat4 projection{};                            // jittered
    Mat4 view_projection{};                       // jittered: used for rasterization / primary rays
    Mat4 view_projection_unjittered{};            // used for motion vectors
    Mat4 inverse_view_projection_unjittered{};
    Mat4 previous_view_projection_unjittered{};
    Mat4 previous_view_projection{};              // jittered
    Mat4 inverse_view{};
    Mat4 inverse_projection{};                    // inverse of the jittered projection
    Vec4 camera_position_near{};                  // xyz world camera position, w near plane
    Vec4 previous_camera_position_far{};          // xyz previous camera position, w far plane
    Vec4 jitter_pixels{};                         // xy current jitter, zw previous jitter (render pixels)
    Vec4 render_size{};                           // width, height, 1/width, 1/height
    Vec4 display_size{};                          // width, height, 1/width, 1/height
    std::uint32_t frame_index = 0;
    std::uint32_t jitter_index = 0;
    std::uint32_t scene_revision = 0;
    std::uint32_t reset_flags = 0;                // ResetReason bits observed for this frame
    float exposure_ev = 0.0F;
    float environment_intensity = 1.0F;
    float emissive_scale = 1.0F;                  // scripted emissive animation multiplier
    std::uint32_t history_valid = 0;
    std::uint32_t hybrid_flags = 0;
    float reflection_max_roughness = 0.5F;
    std::uint32_t padding0 = 0;
    std::uint32_t padding1 = 0;
};

// Per-draw temporal/hybrid data bound at raster register b3 next to the frozen C2 b1 draw block.
struct alignas(16) RasterTemporalDrawConstants
{
    Mat4 previous_world{};
    std::uint32_t instance_id = 0;          // stable instance index (prepared draw order); G-buffer stores +1
    std::uint32_t material_table_index = 0; // 0 = default material, i + 1 = source material i
    std::uint32_t flags = 0;
    std::uint32_t padding0 = 0;
};

inline constexpr std::uint32_t kHybridFlagRayTracedShadows = 1U << 0U;     // opaque direct light deferred to DXR
inline constexpr std::uint32_t kHybridFlagRayTracedReflections = 1U << 1U; // low-roughness IBL specular deferred to DXR
inline constexpr std::uint32_t kHybridFlagGeometryBuffer = 1U << 2U;      // raster writes the G-buffer MRT set

inline constexpr std::uint32_t kTemporalDrawFlagHybridEligible = 1U << 0U; // OPAQUE/MASK draw composited by DXR

// G-buffer formats (render resolution). Values are DXGI enum numbers so portable code can test
// the contract without including d3d12.h; graphics code static_asserts them against DXGI.
inline constexpr std::uint32_t kGBufferCount = 5U;
inline constexpr std::uint32_t kGBufferNormalRoughness = 0U;   // R16G16B16A16_FLOAT: shading normal xyz, perceptual roughness
inline constexpr std::uint32_t kGBufferAlbedoMetallic = 1U;    // R16G16B16A16_FLOAT: linear base colour rgb, metallic
inline constexpr std::uint32_t kGBufferPositionDepth = 2U;     // R32G32B32A32_FLOAT: world position xyz, linear view depth (0 = sky)
inline constexpr std::uint32_t kGBufferMotion = 3U;            // R16G16B16A16_FLOAT: xy previous - current pixel (render pixels),
                                                               // z = linear view depth of the same point in the previous frame, w = 1
inline constexpr std::uint32_t kGBufferIdentifiers = 4U;       // R32G32B32A32_UINT: instance+1, material+1, octahedral geometric normal, AO|flags

static_assert(alignof(GpuViewConstants) == 16);
static_assert(offsetof(GpuViewConstants, projection) == 64);
static_assert(offsetof(GpuViewConstants, view_projection) == 128);
static_assert(offsetof(GpuViewConstants, view_projection_unjittered) == 192);
static_assert(offsetof(GpuViewConstants, inverse_view_projection_unjittered) == 256);
static_assert(offsetof(GpuViewConstants, previous_view_projection_unjittered) == 320);
static_assert(offsetof(GpuViewConstants, previous_view_projection) == 384);
static_assert(offsetof(GpuViewConstants, inverse_view) == 448);
static_assert(offsetof(GpuViewConstants, inverse_projection) == 512);
static_assert(offsetof(GpuViewConstants, camera_position_near) == 576);
static_assert(offsetof(GpuViewConstants, jitter_pixels) == 608);
static_assert(offsetof(GpuViewConstants, render_size) == 624);
static_assert(offsetof(GpuViewConstants, display_size) == 640);
static_assert(offsetof(GpuViewConstants, frame_index) == 656);
static_assert(offsetof(GpuViewConstants, exposure_ev) == 672);
static_assert(offsetof(GpuViewConstants, history_valid) == 684);
static_assert(offsetof(GpuViewConstants, hybrid_flags) == 688);
static_assert(offsetof(GpuViewConstants, reflection_max_roughness) == 692);
static_assert(sizeof(GpuViewConstants) == 704);
static_assert(alignof(RasterTemporalDrawConstants) == 16);
static_assert(offsetof(RasterTemporalDrawConstants, instance_id) == 64);
static_assert(offsetof(RasterTemporalDrawConstants, flags) == 72);
static_assert(sizeof(RasterTemporalDrawConstants) == 80);
}
