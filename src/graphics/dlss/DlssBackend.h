#pragma once

#ifndef _WIN32
#error DlssBackend is available only on Windows.
#endif

#include "core/RenderSettings.h"
#include "graphics/D3D12Context.h"
#include "graphics/D3D12Helpers.h"
#include "graphics/GpuSceneResources.h"
#include "graphics/GpuTimer.h"
#include "graphics/dlss/DlssVendorRuntime.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace daedalus
{
struct UpscalerReport
{
    UpscalerMode requested = UpscalerMode::none;
    UpscalerMode active = UpscalerMode::none;
    DlssQualityMode quality = DlssQualityMode::quality;
    bool vendor_glue_compiled = false;
    std::string status;      // ACTIVE, INACTIVE, FALLBACK, BLOCKED
    std::string reason;
    std::string runtime;     // SDK-reported description when active
    std::uint32_t render_width = 0;
    std::uint32_t render_height = 0;
    std::uint32_t display_width = 0;
    std::uint32_t display_height = 0;
    std::uint64_t evaluations = 0;
    std::uint64_t resets = 0;
    std::uint64_t feature_creations = 0;

    [[nodiscard]] std::string to_json() const;
};

// Renderer-owned DLSS Super Resolution backend (Campaign G). Owns the display-resolution output
// texture and the vendor runtime lifetime; Frame Generation, Multi Frame Generation and Ray
// Reconstruction are separate SDK feature families and are NOT exposed here.
class DlssBackend final
{
public:
    // True only when the build defined DAEDALUS_DLSS_VENDOR_GLUE (CMake DAEDALUS_ENABLE_DLSS=ON with
    // a user-supplied glue source); otherwise every creation attempt reports BLOCKED.
    [[nodiscard]] static bool vendor_glue_compiled() noexcept;

    // Returns nullptr with `report` describing the fallback reason when DLSS cannot be used.
    [[nodiscard]] static std::unique_ptr<DlssBackend> try_create(D3D12Context& context,
                                                                 GpuSceneResources& resources,
                                                                 const std::filesystem::path& application_directory,
                                                                 DlssQualityMode quality,
                                                                 std::uint32_t display_width,
                                                                 std::uint32_t display_height,
                                                                 UpscalerReport& report);
    ~DlssBackend();

    DlssBackend(const DlssBackend&) = delete;
    DlssBackend& operator=(const DlssBackend&) = delete;
    DlssBackend(DlssBackend&&) = delete;
    DlssBackend& operator=(DlssBackend&&) = delete;

    // GPU idle required: queries the SDK render resolution and recreates the feature + output.
    void configure(std::uint32_t display_width, std::uint32_t display_height);
    [[nodiscard]] DlssRenderResolution render_resolution() const noexcept { return render_resolution_; }

    void record(ID3D12GraphicsCommandList* command_list,
                GpuTexture& color,
                ID3D12Resource* hardware_depth,
                GpuTexture* linear_depth,
                GpuTexture& motion,
                float jitter_x,
                float jitter_y,
                float exposure_ev,
                float frame_time_ms,
                bool reset,
                GpuTimer& timer);

    [[nodiscard]] GpuTexture& output() noexcept { return output_; }
    [[nodiscard]] UpscalerReport& report() noexcept { return report_; }

private:
    DlssBackend(D3D12Context& context, GpuSceneResources& resources, std::unique_ptr<DlssVendorRuntime> runtime,
                DlssQualityMode quality, UpscalerReport report);

    D3D12Context& context_;
    GpuSceneResources& resources_;
    std::unique_ptr<DlssVendorRuntime> runtime_;
    DlssQualityMode quality_ = DlssQualityMode::quality;
    DlssRenderResolution render_resolution_{};
    GpuTexture output_;
    UpscalerReport report_;
    bool feature_created_ = false;
};
}
