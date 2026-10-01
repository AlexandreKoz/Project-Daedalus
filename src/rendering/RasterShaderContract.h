#pragma once

#include "rendering/RasterPreparation.h"
#include "scene/Math.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace daedalus
{
struct alignas(16) RasterFrameConstants
{
    Mat4 view_projection{};
    Vec4 camera_position{}; // xyz camera position; w reserved
    Vec4 provisional_ambient{0.03F, 0.03F, 0.03F, 0.0F};
    std::uint32_t diagnostic_mode = 0;
    std::uint32_t light_count = 0;
    std::uint32_t padding0 = 0;
    std::uint32_t padding1 = 0;
};

struct alignas(16) RasterDrawConstants
{
    Mat4 world{};
    Mat4 normal_matrix{};
    Vec4 base_color_factor{};
    Vec4 emissive_metallic{}; // emissive xyz, metallic factor
    Vec4 roughness_normal_ao_cutoff{}; // roughness, normal scale, AO strength, alpha cutoff
    std::uint32_t flags = 0;
    std::uint32_t material_diagnostic_id = 0;
    std::uint32_t alpha_mode = 0;
    std::uint32_t base_color_texcoord = 0;
    std::uint32_t metallic_roughness_texcoord = 0;
    std::uint32_t normal_texcoord = 0;
    std::uint32_t occlusion_texcoord = 0;
    std::uint32_t emissive_texcoord = 0;
    float world_handedness = 1.0F;
    std::uint32_t padding0 = 0;
    std::uint32_t padding1 = 0;
};

struct alignas(16) RasterLightGpu
{
    Vec4 position_range{};       // xyz position; w range (0 = unbounded)
    Vec4 direction_outer_cos{};  // xyz emitted direction; w cos(outer)
    Vec4 color_intensity{};      // rgb; w intensity
    std::uint32_t type = 0;
    float inner_cone_cos = 1.0F;
    std::uint32_t padding0 = 0;
    std::uint32_t padding1 = 0;
};

struct alignas(16) RasterLightConstants
{
    std::array<RasterLightGpu, kMaximumPunctualLights> lights{};
};

inline constexpr std::uint32_t kRasterFlagVertexColor = 1U << 0U;
inline constexpr std::uint32_t kRasterFlagBaseColorTexture = 1U << 1U;
inline constexpr std::uint32_t kRasterFlagMetallicRoughnessTexture = 1U << 2U;
inline constexpr std::uint32_t kRasterFlagNormalTexture = 1U << 3U;
inline constexpr std::uint32_t kRasterFlagOcclusionTexture = 1U << 4U;
inline constexpr std::uint32_t kRasterFlagEmissiveTexture = 1U << 5U;
inline constexpr std::uint32_t kRasterFlagDoubleSided = 1U << 6U;
inline constexpr std::uint32_t kRasterFlagHasTangents = 1U << 7U;

static_assert(alignof(RasterFrameConstants) == 16);
static_assert(sizeof(RasterFrameConstants) == 112);
static_assert(offsetof(RasterFrameConstants, diagnostic_mode) == 96);
static_assert(alignof(RasterDrawConstants) == 16);
static_assert(sizeof(RasterDrawConstants) == 224);
static_assert(offsetof(RasterDrawConstants, flags) == 176);
static_assert(offsetof(RasterDrawConstants, world_handedness) == 208);
static_assert(alignof(RasterLightGpu) == 16);
static_assert(sizeof(RasterLightGpu) == 64);
static_assert(offsetof(RasterLightGpu, type) == 48);
static_assert(sizeof(RasterLightConstants) == 64U * kMaximumPunctualLights);
}
