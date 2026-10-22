#pragma once

#ifndef _WIN32
#error RaytracingPipeline is available only on Windows.
#endif

#include "graphics/GpuSceneResources.h"
#include "rendering/RayTracingPreparation.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace daedalus
{
struct RaytracingHitGroup
{
    std::wstring name;
    std::wstring closest_hit; // empty = none
    std::wstring any_hit;     // empty = none
};

struct RaytracingPipelineDescription
{
    std::vector<std::byte> library;            // DXIL library compiled with -T lib_6_3
    std::wstring ray_generation;
    std::vector<std::wstring> miss;            // indexed by ray type (MissShaderIndex)
    std::vector<RaytracingHitGroup> hit_groups; // indexed by ray type (RayContributionToHitGroupIndex)
    ID3D12RootSignature* global_root_signature = nullptr;
    std::uint32_t max_payload_bytes = 0;
    std::uint32_t max_attribute_bytes = 0;
    std::uint32_t max_recursion_depth = 1;
};

// One DXR state object plus its shader binding table. Records carry no local root arguments: all
// resources are bound through the global root signature (bindless tables indexed by InstanceID()),
// so every record is exactly one 32-byte shader identifier padded to the 32-byte record alignment,
// and each table section starts on a 64-byte boundary (see compute_shader_table_layout).
class RaytracingPipeline final
{
public:
    RaytracingPipeline(ID3D12Device5* device,
                       GpuSceneResources& resources,
                       const RaytracingPipelineDescription& description,
                       std::wstring_view debug_name);

    RaytracingPipeline(const RaytracingPipeline&) = delete;
    RaytracingPipeline& operator=(const RaytracingPipeline&) = delete;
    RaytracingPipeline(RaytracingPipeline&&) = delete;
    RaytracingPipeline& operator=(RaytracingPipeline&&) = delete;

    // Binds the state object and issues DispatchRays(width, height, 1). The global root signature
    // and its arguments must already be set by the caller.
    void dispatch(ID3D12GraphicsCommandList4* command_list, std::uint32_t width, std::uint32_t height) const;

    [[nodiscard]] const ShaderTableLayout& shader_table_layout() const noexcept { return layout_; }
    [[nodiscard]] std::uint64_t committed_allocation_bytes() const noexcept;

private:
    ID3D12Device5* device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12StateObject> state_object_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shader_table_;
    ShaderTableLayout layout_{};
};
}
