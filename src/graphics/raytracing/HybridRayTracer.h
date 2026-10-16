#pragma once

#ifndef _WIN32
#error HybridRayTracer is available only on Windows.
#endif

#include "core/RenderSettings.h"
#include "graphics/ComputePass.h"
#include "graphics/D3D12Helpers.h"
#include "graphics/GpuSceneResources.h"
#include "graphics/GpuTimer.h"
#include "graphics/raytracing/RayTracingCounters.h"
#include "graphics/raytracing/RaytracingPipeline.h"
#include "graphics/raytracing/RaytracingScene.h"
#include "rendering/TemporalContract.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace daedalus
{
struct HybridFrameInputs
{
    std::uint32_t frame_index = 0;
    D3D12_GPU_VIRTUAL_ADDRESS view_constants = 0;
    D3D12_GPU_VIRTUAL_ADDRESS light_constants = 0;
    std::uint32_t light_count = 0;
    float ray_extent = 1.0e30F;
    HybridSettings settings;
};

// Campaign D hybrid renderer stage. Consumes the raster G-buffer, traces real DXR visibility and
// reflection rays (DispatchRays over the render resolution), and composites the results into a
// linear HDR target onto which the raster BLEND pass is drawn afterwards.
//
// Frame order (SceneRenderer): raster opaque+G-buffer -> record_rays -> [reflection denoiser] ->
// record_composite -> raster transparent onto output() -> display.
class HybridRayTracer final
{
public:
    HybridRayTracer(D3D12Context& context,
                    GpuSceneResources& resources,
                    RaytracingScene& scene,
                    ID3D12RootSignature* compute_root_signature,
                    const std::filesystem::path& shader_directory,
                    std::uint32_t width,
                    std::uint32_t height);

    HybridRayTracer(const HybridRayTracer&) = delete;
    HybridRayTracer& operator=(const HybridRayTracer&) = delete;
    HybridRayTracer(HybridRayTracer&&) = delete;
    HybridRayTracer& operator=(HybridRayTracer&&) = delete;

    // Recreates outputs (GPU idle required) and rewrites every descriptor that references them.
    void resize(std::uint32_t width, std::uint32_t height);
    // Rewrites the G-buffer/raster/denoiser input descriptors (GPU idle required).
    void bind_inputs(std::array<GpuTexture, kGBufferCount>& geometry_buffer, const GpuTexture& raster_hdr,
                     const GpuTexture* filtered_reflection);

    void record_rays(ID3D12GraphicsCommandList4* command_list, const HybridFrameInputs& inputs,
                     std::array<GpuTexture, kGBufferCount>& geometry_buffer, GpuTimer& timer);
    void record_composite(ID3D12GraphicsCommandList* command_list, const HybridFrameInputs& inputs,
                          GpuTexture& raster_hdr, std::array<GpuTexture, kGBufferCount>& geometry_buffer,
                          GpuTexture* filtered_reflection, GpuTimer& timer);

    [[nodiscard]] GpuTexture& output() noexcept { return output_; }
    [[nodiscard]] GpuTexture& reflection() noexcept { return reflection_; }
    [[nodiscard]] GpuTexture& direct() noexcept { return direct_; }
    [[nodiscard]] const RayTracingCounters& counters() const noexcept { return *counters_; }
    [[nodiscard]] std::uint64_t committed_allocation_bytes() const noexcept;

private:
    void create_targets(std::uint32_t width, std::uint32_t height);

    D3D12Context& context_;
    GpuSceneResources& resources_;
    RaytracingScene& scene_;
    ID3D12Device* device_ = nullptr;
    std::unique_ptr<RaytracingPipeline> pipeline_;
    std::unique_ptr<ComputePipeline> composite_;
    std::unique_ptr<RayTracingCounters> counters_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    std::uint32_t ray_inputs_base_ = 0;
    std::uint32_t ray_outputs_base_ = 0;
    ComputeBindingSet composite_bindings_;
    GpuTexture direct_;
    GpuTexture reflection_;
    GpuTexture output_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool filtered_bound_ = false;
};
}
