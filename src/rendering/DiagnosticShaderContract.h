#pragma once

#include "scene/Math.h"

#include <cstddef>
#include <cstdint>

namespace daedalus
{
// CPU mirror of shaders/Diagnostic.hlsl DrawConstants.
// Keep this renderer-neutral so portable tests catch CPU-side ABI drift before Windows/D3D12 builds.
struct alignas(16) DiagnosticDrawConstants
{
    Mat4 world_view_projection{};
    Mat4 world{};
    Mat4 normal_matrix{};
    Vec4 base_color_factor{};
    std::uint32_t diagnostic_mode = 0;
    std::uint32_t has_texture = 0;
    std::uint32_t use_vertex_color = 0;
    std::uint32_t texture_coord_set = 0;
    float world_handedness = 1.0F;
    std::uint32_t padding0 = 0;
    std::uint32_t padding1 = 0;
    std::uint32_t padding2 = 0;
};

static_assert(alignof(DiagnosticDrawConstants) == 16, "DiagnosticDrawConstants alignment changed");
static_assert(sizeof(DiagnosticDrawConstants) == 240, "CPU/HLSL DiagnosticDrawConstants packing changed");
static_assert(offsetof(DiagnosticDrawConstants, diagnostic_mode) == 208, "diagnostic-mode offset changed");
static_assert(offsetof(DiagnosticDrawConstants, texture_coord_set) == 220, "UV-set offset changed");
static_assert(offsetof(DiagnosticDrawConstants, world_handedness) == 224, "handedness offset changed");
static_assert(offsetof(DiagnosticDrawConstants, padding2) == 236, "explicit tail padding changed");
}
