#include "graphics/raytracing/DxrDeviceSupport.h"

#include "core/Error.h"

#include <sstream>

namespace daedalus
{
namespace
{
[[nodiscard]] bool typed_uav_load(ID3D12Device* device, DXGI_FORMAT format) noexcept
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
    support.Format = format;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)))) return false;
    return (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0 &&
           (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0;
}

[[nodiscard]] const char* shader_model_name(D3D_SHADER_MODEL model) noexcept
{
    switch (model)
    {
    case D3D_SHADER_MODEL_5_1: return "5.1";
    case D3D_SHADER_MODEL_6_0: return "6.0";
    case D3D_SHADER_MODEL_6_1: return "6.1";
    case D3D_SHADER_MODEL_6_2: return "6.2";
    case D3D_SHADER_MODEL_6_3: return "6.3";
    case D3D_SHADER_MODEL_6_4: return "6.4";
    case D3D_SHADER_MODEL_6_5: return "6.5";
    default: return ">=6.6";
    }
}
}

std::string DxrCapabilities::tier_name() const
{
    switch (raytracing_tier)
    {
    case D3D12_RAYTRACING_TIER_NOT_SUPPORTED: return "not-supported";
    case D3D12_RAYTRACING_TIER_1_0: return "1.0";
    case D3D12_RAYTRACING_TIER_1_1: return "1.1";
    default: return ">1.1";
    }
}

std::string DxrCapabilities::summary() const
{
    std::ostringstream stream;
    stream << "DXR " << (supported() ? "SUPPORTED" : "UNSUPPORTED") << ": raytracing_tier=" << tier_name()
           << " resource_binding_tier=" << static_cast<int>(resource_binding_tier)
           << " shader_model=" << shader_model_name(highest_shader_model)
           << " device5=" << (device5 ? "yes" : "no") << " command_list4=" << (command_list4 ? "yes" : "no")
           << " typed_uav_load_rgba32f=" << (typed_uav_load_rgba32f ? "yes" : "no")
           << " typed_uav_load_rgba16f=" << (typed_uav_load_rgba16f ? "yes" : "no")
           << " warp=" << (warp ? "yes" : "no");
    if (!reason.empty()) stream << " reason=\"" << reason << '"';
    return stream.str();
}

std::string DxrCapabilities::to_json() const
{
    std::ostringstream stream;
    stream << "{\"status\": \"" << (supported() ? "SUPPORTED" : "UNSUPPORTED") << "\", \"raytracing_tier\": \"" << tier_name()
           << "\", \"resource_binding_tier\": " << static_cast<int>(resource_binding_tier)
           << ", \"shader_model\": \"" << shader_model_name(highest_shader_model)
           << "\", \"device5\": " << (device5 ? "true" : "false")
           << ", \"command_list4\": " << (command_list4 ? "true" : "false")
           << ", \"typed_uav_load_rgba32f\": " << (typed_uav_load_rgba32f ? "true" : "false")
           << ", \"typed_uav_load_rgba16f\": " << (typed_uav_load_rgba16f ? "true" : "false")
           << ", \"warp\": " << (warp ? "true" : "false") << ", \"reason\": \"";
    for (const char c : reason) { if (c == '"' || c == '\\') stream << '\\'; stream << c; }
    stream << "\"}";
    return stream.str();
}

DxrCapabilities query_dxr_capabilities(const D3D12Context& context)
{
    DxrCapabilities capabilities;
    ID3D12Device* device = context.device();
    capabilities.warp = context.using_warp();
    capabilities.device5 = context.device5() != nullptr;
    capabilities.command_list4 = context.supports_command_list4();
    if (device == nullptr)
    {
        capabilities.reason = "no D3D12 device";
        return capabilities;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
        capabilities.raytracing_tier = options5.RaytracingTier;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
        capabilities.resource_binding_tier = options.ResourceBindingTier;
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_5};
    HRESULT result = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model));
    if (FAILED(result))
    {
        shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_3;
        result = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model));
    }
    capabilities.highest_shader_model = SUCCEEDED(result) ? shader_model.HighestShaderModel : D3D_SHADER_MODEL_5_1;
    capabilities.typed_uav_load_rgba32f = typed_uav_load(device, DXGI_FORMAT_R32G32B32A32_FLOAT);
    capabilities.typed_uav_load_rgba16f = typed_uav_load(device, DXGI_FORMAT_R16G16B16A16_FLOAT);

    if (capabilities.raytracing_tier < D3D12_RAYTRACING_TIER_1_0)
        capabilities.reason = "D3D12_FEATURE_D3D12_OPTIONS5 reports RaytracingTier NOT_SUPPORTED";
    else if (!capabilities.device5)
        capabilities.reason = "ID3D12Device5 is unavailable";
    else if (!capabilities.command_list4)
        capabilities.reason = "ID3D12GraphicsCommandList4 is unavailable";
    else if (capabilities.highest_shader_model < D3D_SHADER_MODEL_6_3)
        capabilities.reason = "shader model 6.3 (DXIL library lib_6_3) is unavailable";
    else if (capabilities.resource_binding_tier < D3D12_RESOURCE_BINDING_TIER_3)
        capabilities.reason = "resource binding tier 3 is required for the unbounded bindless DXR tables";
    else if (!capabilities.typed_uav_load_rgba32f || !capabilities.typed_uav_load_rgba16f)
        capabilities.reason = "typed UAV loads for R32G32B32A32_FLOAT/R16G16B16A16_FLOAT are required for accumulation/history";
    else
        capabilities.status = RayTracingSupportStatus::supported;
    return capabilities;
}
}
