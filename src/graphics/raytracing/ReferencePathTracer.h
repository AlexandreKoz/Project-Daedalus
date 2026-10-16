#pragma once

#ifndef _WIN32
#error ReferencePathTracer is available only on Windows.
#endif

#include "core/RenderSettings.h"
#include "graphics/ComputePass.h"
#include "graphics/D3D12Helpers.h"
#include "graphics/GpuSceneResources.h"
#include "graphics/GpuTimer.h"
#include "graphics/raytracing/RayTracingCounters.h"
#include "graphics/raytracing/RaytracingPipeline.h"
#include "graphics/raytracing/RaytracingScene.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace daedalus
{
struct PathTraceFrameInputs
{
    std::uint32_t frame_index = 0;
    D3D12_GPU_VIRTUAL_ADDRESS view_constants = 0;
    D3D12_GPU_VIRTUAL_ADDRESS light_constants = 0;
    std::uint32_t light_count = 0;
    float ray_extent = 1.0e30F;
    float environment_intensity = 1.0F;
    PathTraceSettings settings;
    bool reset_accumulation = false;
    std::uint32_t samples_this_dispatch = 1;
};

// Campaign E progressive DXR path tracer. Accumulation buffers are R32G32B32A32_FLOAT sums of
// linear radiance plus an explicit per-pixel sample count, so the displayed mean is exact
// (sum / count) and independent of the swap-chain frame-slot count. Sample indices are global per
// pixel: sample_base = samples accumulated since the last reset.
class ReferencePathTracer final
{
public:
    ReferencePathTracer(D3D12Context& context,
                        GpuSceneResources& resources,
                        RaytracingScene& scene,
                        ID3D12RootSignature* compute_root_signature,
                        const std::filesystem::path& shader_directory,
                        std::uint32_t width,
                        std::uint32_t height);

    ReferencePathTracer(const ReferencePathTracer&) = delete;
    ReferencePathTracer& operator=(const ReferencePathTracer&) = delete;
    ReferencePathTracer(ReferencePathTracer&&) = delete;
    ReferencePathTracer& operator=(ReferencePathTracer&&) = delete;

    void resize(std::uint32_t width, std::uint32_t height); // GPU idle required

    void record(ID3D12GraphicsCommandList4* command_list, const PathTraceFrameInputs& inputs, GpuTimer& timer);
    // Remodulates a denoised demodulated signal into denoised_output().
    void bind_filtered_signal(const GpuTexture& filtered);
    void record_remodulate(ID3D12GraphicsCommandList* command_list, D3D12_GPU_VIRTUAL_ADDRESS view_constants,
                           GpuTexture& filtered, GpuTimer& timer);

    [[nodiscard]] std::uint64_t accumulated_samples() const noexcept { return accumulated_samples_; }
    [[nodiscard]] GpuTexture& output() noexcept { return output_; }
    [[nodiscard]] GpuTexture& denoised_output() noexcept { return denoised_output_; }
    [[nodiscard]] GpuTexture& noisy_demodulated() noexcept { return noisy_; }
    [[nodiscard]] GpuTexture& normal_roughness() noexcept { return normal_roughness_; }
    [[nodiscard]] GpuTexture& position_depth() noexcept { return position_depth_; }
    [[nodiscard]] GpuTexture& identifiers() noexcept { return identifiers_; }
    [[nodiscard]] GpuTexture& motion() noexcept { return motion_; }
    [[nodiscard]] const RayTracingCounters& counters() const noexcept { return *counters_; }
    [[nodiscard]] std::uint64_t committed_allocation_bytes() const noexcept;

private:
    void create_targets(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] std::array<GpuTexture*, 11> uav_targets() noexcept;

    D3D12Context& context_;
    GpuSceneResources& resources_;
    RaytracingScene& scene_;
    ID3D12Device* device_ = nullptr;
    std::unique_ptr<RaytracingPipeline> pipeline_;
    std::unique_ptr<ComputePipeline> remodulate_;
    std::unique_ptr<RayTracingCounters> counters_;
    std::uint32_t ray_inputs_base_ = 0;
    std::uint32_t ray_outputs_base_ = 0;
    ComputeBindingSet remodulate_bindings_;
    GpuTexture accumulation_;
    GpuTexture moments_;
    GpuTexture direct_accumulation_;
    GpuTexture output_;
    GpuTexture noisy_;
    GpuTexture albedo_;
    GpuTexture primary_emission_;
    GpuTexture normal_roughness_;
    GpuTexture position_depth_;
    GpuTexture identifiers_;
    GpuTexture motion_;
    GpuTexture denoised_output_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint64_t accumulated_samples_ = 0;
};
}
