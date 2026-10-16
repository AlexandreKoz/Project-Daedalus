#pragma once

#ifndef _WIN32
#error DisplayOutput is available only on Windows.
#endif

#include "graphics/D3D12Context.h"
#include "graphics/D3D12Helpers.h"
#include "graphics/GpuSceneResources.h"
#include "graphics/GpuTimer.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace daedalus
{
struct RendererCaptureResult
{
    std::uint32_t width = 0;   // display (swap-chain) resolution of the LDR capture
    std::uint32_t height = 0;
    std::vector<std::byte> rgba8;
    // Pre-tone-map linear signal that was presented (render or display resolution, see linear_*).
    std::uint32_t linear_width = 0;
    std::uint32_t linear_height = 0;
    std::string linear_format;
    std::vector<float> linear_rgb;
    std::uint64_t hdr_nan_count = 0;
    std::uint64_t hdr_infinity_count = 0;
};

// Final presentation boundary: exposure, compact ACES fit and exactly one linear->sRGB encode into
// the R8G8B8A8_UNORM back buffer, optional explicit bilinear resampling for --upscaler bilinear,
// and deterministic readback of both the LDR back buffer and the pre-tone-map linear signal.
class DisplayOutput final
{
public:
    DisplayOutput(D3D12Context& context, GpuSceneResources& resources, const std::filesystem::path& shader_directory);
    ~DisplayOutput() = default;

    DisplayOutput(const DisplayOutput&) = delete;
    DisplayOutput& operator=(const DisplayOutput&) = delete;
    DisplayOutput(DisplayOutput&&) = delete;
    DisplayOutput& operator=(DisplayOutput&&) = delete;

    // Allocates readback buffers for the next recorded frame. linear_source must be the texture that
    // will be passed to record(); its format must be R16G16B16A16_FLOAT or R32G32B32A32_FLOAT.
    void request_capture(bool capture_ldr, bool capture_linear, std::uint32_t display_width, std::uint32_t display_height,
                         const GpuTexture& linear_source);
    void record(ID3D12GraphicsCommandList* command_list,
                const FrameRecordingContext& frame,
                GpuTexture& linear_source,
                bool passthrough,
                float exposure_ev,
                GpuTimer& timer);
    [[nodiscard]] bool capture_recorded() const noexcept { return capture_recorded_; }
    [[nodiscard]] RendererCaptureResult finalize_capture();
    [[nodiscard]] std::uint64_t committed_allocation_bytes() const noexcept;

private:
    D3D12Context& context_;
    GpuSceneResources& resources_;
    ID3D12Device* device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;

    Microsoft::WRL::ComPtr<ID3D12Resource> ldr_readback_;
    Microsoft::WRL::ComPtr<ID3D12Resource> linear_readback_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT ldr_footprint_{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT linear_footprint_{};
    std::uint64_t ldr_size_ = 0;
    std::uint64_t linear_size_ = 0;
    std::uint32_t capture_width_ = 0;
    std::uint32_t capture_height_ = 0;
    std::uint32_t linear_width_ = 0;
    std::uint32_t linear_height_ = 0;
    DXGI_FORMAT linear_format_ = DXGI_FORMAT_UNKNOWN;
    bool capture_ldr_requested_ = false;
    bool capture_linear_requested_ = false;
    bool capture_recorded_ = false;
};
}
