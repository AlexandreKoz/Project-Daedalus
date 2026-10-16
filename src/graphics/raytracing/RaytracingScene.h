#pragma once

#ifndef _WIN32
#error RaytracingScene is available only on Windows.
#endif

#include "graphics/D3D12Context.h"
#include "graphics/GpuSceneResources.h"
#include "graphics/GpuTimer.h"
#include "graphics/raytracing/AccelerationStructureManager.h"
#include "rendering/RayTracingPreparation.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace daedalus
{
// Pass constant slots inside RaytracingScene's per-frame RtPassConstants buffer.
enum class RtPassSlot : std::uint32_t
{
    hybrid = 0,
    path_trace = 1,
    count = 2
};

// GPU-side ray-tracing scene: acceleration structures, instance/geometry/material/emissive tables
// and the global root signature shared by every DXR pipeline in Daedalus.
//
// Global root signature (mirrors shaders/raytracing/RayTracingCommon.hlsli):
//   0  CBV  b0        GpuViewConstants
//   1  CBV  b1        punctual lights (RasterLightConstants layout, 32 entries)
//   2  CBV  b2        RtPassConstants
//   3  SRV  t0        TLAS (RaytracingAccelerationStructure)
//   4  SRV  t1        StructuredBuffer<RtInstanceData>
//   5  SRV  t2        StructuredBuffer<RtGeometryData>
//   6  SRV  t3        StructuredBuffer<RtMaterialData>
//   7  SRV  t4        StructuredBuffer<RtEmissiveTriangle>
//   8  table t0 space1  Texture2D<float4> textures[] (unbounded, from shared-heap index 0)
//   9  table t0 space2  ByteAddressBuffer buffers[]  (unbounded, from the raw-buffer base)
//   10 table s0 space1  SamplerState samplers[]      (unbounded, shared sampler heap)
//   11 table t5..t20    pass inputs  (16 SRVs)
//   12 table u0..u15    pass outputs (16 UAVs)
class RaytracingScene final
{
public:
    static constexpr std::uint32_t kPassInputCount = 16U;
    static constexpr std::uint32_t kPassOutputCount = 16U;

    RaytracingScene(D3D12Context& context,
                    GpuSceneResources& resources,
                    const CanonicalScene& scene,
                    std::span<const PreparedRasterDraw> draws,
                    std::span<const Mat4> initial_world);
    ~RaytracingScene();

    RaytracingScene(const RaytracingScene&) = delete;
    RaytracingScene& operator=(const RaytracingScene&) = delete;
    RaytracingScene(RaytracingScene&&) = delete;
    RaytracingScene& operator=(RaytracingScene&&) = delete;

    // Uploads changed instance data and builds/updates the TLAS for this frame.
    void update(ID3D12GraphicsCommandList4* command_list,
                std::uint32_t frame_index,
                std::span<const Mat4> world,
                std::span<const Mat4> previous_world,
                bool transforms_changed,
                GpuTimer& timer);

    void write_pass_constants(std::uint32_t frame_index, RtPassSlot slot, const RtPassConstants& constants);

    // Binds the global root signature, descriptor heaps and every scene-level argument.
    void bind(ID3D12GraphicsCommandList4* command_list,
              std::uint32_t frame_index,
              RtPassSlot slot,
              D3D12_GPU_VIRTUAL_ADDRESS view_constants,
              D3D12_GPU_VIRTUAL_ADDRESS light_constants,
              D3D12_GPU_DESCRIPTOR_HANDLE pass_inputs,
              D3D12_GPU_DESCRIPTOR_HANDLE pass_outputs) const;

    [[nodiscard]] ID3D12RootSignature* global_root_signature() const noexcept { return root_signature_.Get(); }
    [[nodiscard]] const RtSceneTables& tables() const noexcept { return tables_; }
    [[nodiscard]] const AccelerationStructureStats& acceleration_stats() const noexcept { return acceleration_->stats(); }
    [[nodiscard]] std::uint32_t emissive_triangle_count() const noexcept;
    [[nodiscard]] std::uint64_t committed_allocation_bytes() const noexcept;

private:
    void create_root_signature();
    void create_tables();

    D3D12Context& context_;
    GpuSceneResources& resources_;
    ID3D12Device* device_ = nullptr;
    RtSceneTables tables_;
    std::unique_ptr<AccelerationStructureManager> acceleration_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12Resource> instance_buffer_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, D3D12Context::kFrameCount> instance_staging_{};
    std::array<std::byte*, D3D12Context::kFrameCount> mapped_instance_staging_{};
    std::vector<RtInstanceData> uploaded_instances_;
    bool instance_buffer_initialized_ = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> geometry_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> material_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> emissive_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pass_constants_;
    std::byte* mapped_pass_constants_ = nullptr;
    std::size_t pass_constant_stride_ = 256;
};
}
