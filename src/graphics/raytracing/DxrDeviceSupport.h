#pragma once

#ifndef _WIN32
#error DxrDeviceSupport is available only on Windows.
#endif

#include "graphics/D3D12Context.h"

#include <string>

namespace daedalus
{
enum class RayTracingSupportStatus
{
    supported,
    unsupported
};

// Result of a real CheckFeatureSupport query. Daedalus never emulates rays: when any requirement
// is missing, the DXR modes report UNSUPPORTED with the precise missing capability and the raster
// path remains active.
struct DxrCapabilities
{
    RayTracingSupportStatus status = RayTracingSupportStatus::unsupported;
    D3D12_RAYTRACING_TIER raytracing_tier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    D3D12_RESOURCE_BINDING_TIER resource_binding_tier = D3D12_RESOURCE_BINDING_TIER_1;
    D3D_SHADER_MODEL highest_shader_model = D3D_SHADER_MODEL_5_1;
    bool device5 = false;
    bool command_list4 = false;
    bool typed_uav_load_rgba32f = false;
    bool typed_uav_load_rgba16f = false;
    bool warp = false;
    std::string reason; // empty when supported

    [[nodiscard]] bool supported() const noexcept { return status == RayTracingSupportStatus::supported; }
    [[nodiscard]] std::string tier_name() const;
    [[nodiscard]] std::string summary() const;
    [[nodiscard]] std::string to_json() const;
};

[[nodiscard]] DxrCapabilities query_dxr_capabilities(const D3D12Context& context);
}
