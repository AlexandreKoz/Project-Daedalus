#pragma once

#ifndef _WIN32
#error TemporalDenoiser is available only on Windows.
#endif

#include "core/RenderSettings.h"
#include "graphics/ComputePass.h"
#include "graphics/D3D12Helpers.h"
#include "graphics/GpuSceneResources.h"
#include "graphics/GpuTimer.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace daedalus
{
// Guide buffers in the renderer-wide G-buffer formats (src/rendering/TemporalContract.h).
struct TemporalGuide
{
    GpuTexture* normal_roughness = nullptr;
    GpuTexture* position_depth = nullptr;
    GpuTexture* identifiers = nullptr;
    GpuTexture* motion = nullptr;
};

struct TemporalDenoiserStats
{
    std::uint64_t frames = 0;
    std::uint64_t history_resets = 0;
};

// Campaign F denoiser: temporal reprojection/accumulation with explicit rejection followed by
// N edge-aware a-trous iterations. One instance per noisy signal (hybrid reflections, path-traced
// radiance). Histories are ping-pong pairs selected by this denoiser's own parity (never by the
// swap-chain frame slot), so no pass ever reads and writes the same history texture.
//
// Frame order inside record(): reproject (history[prev] -> history[cur], filter A, debug) ->
// a-trous A<->B -> output -> copy current guides into the previous-guide textures.
class TemporalDenoiser final
{
public:
    TemporalDenoiser(ID3D12Device* device,
                     GpuSceneResources& resources,
                     ID3D12RootSignature* compute_root_signature,
                     const std::filesystem::path& shader_directory,
                     DescriptorBlock reproject_block,
                     DescriptorBlock filter_block,
                     std::uint32_t width,
                     std::uint32_t height,
                     std::wstring name);

    TemporalDenoiser(const TemporalDenoiser&) = delete;
    TemporalDenoiser& operator=(const TemporalDenoiser&) = delete;
    TemporalDenoiser(TemporalDenoiser&&) = delete;
    TemporalDenoiser& operator=(TemporalDenoiser&&) = delete;

    // Both require GPU idle: they recreate resources / rewrite shader-visible descriptors.
    void resize(std::uint32_t width, std::uint32_t height);
    void bind(const GpuTexture& signal, const TemporalGuide& guide);

    void record(ID3D12GraphicsCommandList* command_list,
                D3D12_GPU_VIRTUAL_ADDRESS view_constants,
                const TemporalSettings& settings,
                bool history_valid,
                GpuTexture& signal,
                const TemporalGuide& guide,
                GpuTimer& timer);
    void invalidate_history() noexcept { history_initialized_ = false; }

    [[nodiscard]] GpuTexture& output() noexcept { return output_; }
    [[nodiscard]] GpuTexture& debug() noexcept { return debug_; }
    [[nodiscard]] GpuTexture& history_moments() noexcept { return history_moments_[current_]; }
    [[nodiscard]] const TemporalDenoiserStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::uint64_t committed_allocation_bytes() const noexcept;

private:
    void create_targets(std::uint32_t width, std::uint32_t height);
    void write_static_bindings();

    ID3D12Device* device_ = nullptr;
    GpuSceneResources& resources_;
    std::unique_ptr<ComputePipeline> reproject_;
    std::unique_ptr<ComputePipeline> atrous_;
    std::array<ComputeBindingSet, 2> reproject_sets_{};
    ComputeBindingSet atrous_a_to_b_;
    ComputeBindingSet atrous_b_to_a_;
    ComputeBindingSet atrous_a_to_output_;
    ComputeBindingSet atrous_b_to_output_;
    std::uint32_t srv_base_ = 0;
    std::array<GpuTexture, 2> history_color_{};
    std::array<GpuTexture, 2> history_moments_{};
    GpuTexture previous_normal_roughness_;
    GpuTexture previous_position_depth_;
    GpuTexture previous_identifiers_;
    GpuTexture filter_a_;
    GpuTexture filter_b_;
    GpuTexture output_;
    GpuTexture debug_;
    std::wstring name_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t current_ = 0;
    bool history_initialized_ = false;
    bool bound_ = false;
    TemporalDenoiserStats stats_{};
};
}
