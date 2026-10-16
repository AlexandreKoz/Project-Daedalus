#pragma once

#ifndef _WIN32
#error DlssVendorRuntime is available only on Windows.
#endif

#include "core/RenderSettings.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace daedalus
{
// ---------------------------------------------------------------------------------------------
// Daedalus-owned boundary between the renderer and a vendor DLSS Super Resolution integration.
//
// STATUS: the vendor glue that implements this interface against the real NVIDIA SDK is BLOCKED.
// The SDK headers/libraries/runtime were not available to the implementation session, so no
// NVSDK_NGX_* / Streamline call, structure, parameter name or DLL name appears anywhere in this
// repository. A developer with the SDK supplies ONE translation unit (configured through
// DAEDALUS_DLSS_VENDOR_GLUE_SOURCE) that implements create_dlss_vendor_runtime() and this class
// using the SDK version they actually have. Everything on the Daedalus side of the boundary
// (resolution plumbing, resource states, jitter/motion conventions, reset policy, fallback,
// metadata) is implemented and documented here.
//
// Daedalus conventions handed to the glue (the glue converts to whatever the SDK requires):
//  * colour: LINEAR HDR scene radiance at render resolution, pre-exposure, never tone mapped
//    (R16G16B16A16_FLOAT or R32G32B32A32_FLOAT);
//  * depth: either the raster D32_FLOAT hardware depth (standard, not reversed; 0 = near) or, when
//    absent (path tracing), the linear view depth in the .w channel of position_depth (0 = sky);
//  * motion: R16G16B16A16_FLOAT, .xy = previous_pixel - current_pixel in RENDER pixels, jitter-free
//    (see src/rendering/TemporalMath.h); scale factors are 1.0 in these units;
//  * jitter: the sub-pixel offset applied to the projection this frame, render pixels, +x right,
//    +y down (pixel-coordinate axes);
//  * exposure: the EV the display path will apply afterwards (scene radiance itself is linear);
//  * resource states on entry and required on exit: colour/motion/linear depth
//    PIXEL|NON_PIXEL_SHADER_RESOURCE, hardware depth DEPTH_WRITE (R32_TYPELESS resource, view it as
//    R32_FLOAT), output UNORDERED_ACCESS; the glue may transition internally but must restore them;
//  * command list: the frame's direct command list, open; no queue submission inside evaluate().
// ---------------------------------------------------------------------------------------------

struct DlssRenderResolution
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct DlssFeatureDescription
{
    DlssQualityMode quality = DlssQualityMode::quality;
    std::uint32_t render_width = 0;
    std::uint32_t render_height = 0;
    std::uint32_t display_width = 0;
    std::uint32_t display_height = 0;
    bool hdr_linear_input = true;
    bool jittered_input = true;
    bool motion_vectors_at_render_resolution = true;
};

struct DlssEvaluateInputs
{
    ID3D12GraphicsCommandList* command_list = nullptr;
    ID3D12Resource* color = nullptr;
    ID3D12Resource* hardware_depth = nullptr; // may be null
    ID3D12Resource* linear_depth = nullptr;   // position_depth texture (.w); may be null when hardware depth exists
    ID3D12Resource* motion_vectors = nullptr;
    ID3D12Resource* output = nullptr;         // display resolution, R16G16B16A16_FLOAT, UAV
    float jitter_x = 0.0F;
    float jitter_y = 0.0F;
    float motion_scale_x = 1.0F;
    float motion_scale_y = 1.0F;
    float exposure_ev = 0.0F;
    float frame_time_ms = 0.0F;
    bool reset = false;
    std::uint32_t render_width = 0;
    std::uint32_t render_height = 0;
};

class DlssVendorRuntime
{
public:
    virtual ~DlssVendorRuntime() = default;
    // Version/identity string reported by the SDK and runtime at run time (never hard-coded).
    [[nodiscard]] virtual std::string runtime_description() const = 0;
    // False with an SDK-provided reason when Super Resolution is not available on this device/driver.
    [[nodiscard]] virtual bool super_resolution_available(std::string& reason) = 0;
    // Optimal render resolution for a quality mode as reported by the SDK; nullopt if unsupported.
    [[nodiscard]] virtual std::optional<DlssRenderResolution> optimal_render_resolution(DlssQualityMode quality,
                                                                                       std::uint32_t display_width,
                                                                                       std::uint32_t display_height) = 0;
    // (Re)creates the feature; called with GPU idle on create, resize, quality change, reload.
    virtual void create_feature(ID3D12GraphicsCommandList* command_list, const DlssFeatureDescription& description) = 0;
    virtual void evaluate(const DlssEvaluateInputs& inputs) = 0;
    virtual void release_feature() noexcept = 0;
};

// Implemented ONLY by the vendor glue translation unit. Returns nullptr and fills `failure` when the
// SDK runtime cannot be initialized (missing runtime DLL, unsupported driver/GPU, SDK error code).
[[nodiscard]] std::unique_ptr<DlssVendorRuntime> create_dlss_vendor_runtime(ID3D12Device* device,
                                                                            ID3D12CommandQueue* queue,
                                                                            IDXGIAdapter1* adapter,
                                                                            const std::filesystem::path& application_directory,
                                                                            std::string& failure);
}
