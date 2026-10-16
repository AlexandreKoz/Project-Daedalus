#pragma once

#ifndef _WIN32
#error ComputePass is available only on Windows.
#endif

#include "graphics/D3D12Helpers.h"
#include "graphics/GpuSceneResources.h"

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace daedalus
{
// Shared compute root signature for every Daedalus post/temporal/composite pass
// (mirrors shaders/compute/ComputeCommon.hlsli):
//   0  CBV b0          GpuViewConstants
//   1  constants b1    16 x 32-bit pass constants
//   2  table t0..t15   pass inputs
//   3  table u0..u7    pass outputs
//   static s0 = linear clamp, s1 = point clamp
inline constexpr std::uint32_t kComputeInputCount = 16U;
inline constexpr std::uint32_t kComputeOutputCount = 8U;
inline constexpr std::uint32_t kComputeBindingSetSize = kComputeInputCount + kComputeOutputCount;
inline constexpr std::uint32_t kComputeGroupSize = 8U;

struct ComputeConstants
{
    std::array<std::uint32_t, 16> values{};
    void set_uint(std::size_t index, std::uint32_t value) { values.at(index) = value; }
    void set_float(std::size_t index, float value) { values.at(index) = std::bit_cast<std::uint32_t>(value); }
};

[[nodiscard]] Microsoft::WRL::ComPtr<ID3D12RootSignature> create_compute_root_signature(ID3D12Device* device);

// A contiguous [16 SRV | 8 UAV] descriptor range inside a renderer-owned DescriptorBlock.
// Unused slots are written as null descriptors so every table entry is always initialized.
class ComputeBindingSet final
{
public:
    ComputeBindingSet() = default;
    ComputeBindingSet(GpuSceneResources& resources, std::uint32_t base);

    void clear();
    void input(std::uint32_t slot, const GpuTexture& texture);
    void input_raw(std::uint32_t slot, ID3D12Resource* resource, DXGI_FORMAT format);
    void output(std::uint32_t slot, const GpuTexture& texture);
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE inputs() const;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE outputs() const;
    [[nodiscard]] std::uint32_t base() const noexcept { return base_; }

private:
    GpuSceneResources* resources_ = nullptr;
    std::uint32_t base_ = 0;
};

class ComputePipeline final
{
public:
    ComputePipeline(ID3D12Device* device, ID3D12RootSignature* root_signature,
                    const std::filesystem::path& shader_directory, std::string_view file_name);

    // Binds the shared root signature + heap and dispatches ceil(width/8) x ceil(height/8) groups.
    void dispatch(ID3D12GraphicsCommandList* command_list,
                  const GpuSceneResources& resources,
                  D3D12_GPU_VIRTUAL_ADDRESS view_constants,
                  const ComputeConstants& constants,
                  const ComputeBindingSet& bindings,
                  std::uint32_t width,
                  std::uint32_t height) const;

private:
    ID3D12RootSignature* root_signature_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
};
}
