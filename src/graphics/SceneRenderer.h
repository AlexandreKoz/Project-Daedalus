#pragma once

#ifndef _WIN32
#error SceneRenderer is available only on Windows.
#endif

#include "core/CommandLine.h"
#include "core/RenderSettings.h"
#include "graphics/D3D12Context.h"
#include "graphics/DisplayOutput.h"
#include "graphics/GpuTimer.h"
#include "graphics/dlss/DlssBackend.h"
#include "graphics/raytracing/DxrDeviceSupport.h"
#include "rendering/FrameRevision.h"
#include "rendering/OrbitCamera.h"
#include "rendering/TemporalMath.h"
#include "scene/Scene.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace daedalus
{
class GpuSceneResources;
class PbrSceneRenderer;
class RaytracingScene;
class HybridRayTracer;
class ReferencePathTracer;
class TemporalDenoiser;
class ComputePipeline;
class ComputeBindingSet;
class SceneAnimator;

struct RendererCameraState
{
    Vec3 eye{};
    Vec3 target{};
    float vertical_fov_degrees = 50.0F;
};

struct RendererResourceStats
{
    std::uint64_t vertex_buffer_count = 0;
    std::uint64_t index_buffer_count = 0;
    std::uint64_t texture_count = 0;
    std::uint64_t material_count = 0;
    std::uint64_t light_count = 0;
    std::uint64_t material_buffer_count = 0;
    std::uint64_t light_buffer_count = 0;
    std::uint64_t hdr_target_count = 0;
    std::uint64_t depth_target_count = 0;
    std::uint64_t shadow_map_count = 0;
    std::uint64_t diagnostic_resource_count = 0;
    std::uint64_t descriptor_count = 0;
    std::uint64_t logical_geometry_bytes = 0;
    std::uint64_t canonical_retained_bytes = 0;
    std::uint64_t committed_allocation_bytes = 0;
    // Campaign D-G additions (committed-resource estimates via GetResourceAllocationInfo).
    std::uint64_t ray_tracing_allocation_bytes = 0;
    std::uint64_t temporal_allocation_bytes = 0;
    std::uint64_t blas_count = 0;
    std::uint64_t blas_bytes = 0;
    std::uint64_t tlas_bytes = 0;
};

// Renderer-facing interface driven by Application. Owns every GPU renderer component for one
// canonical scene and records one frame as an explicit, ordered sequence of passes:
//
//   raster:    [TLAS?] shadow -> opaque(+G-buffer) -> transparent -> [temporal debug] -> [upscale] -> display
//   hybrid:    TLAS -> shadow -> opaque+G-buffer -> DXR rays -> [reflection denoise] -> composite
//              -> transparent -> [temporal debug] -> [upscale] -> display
//   pathtrace: TLAS -> DXR path tracing -> [temporal denoise -> remodulate] -> [temporal debug]
//              -> [upscale] -> display
//
// All recreation (mode switches, resolution changes) happens in prepare_frame()/resize() OUTSIDE
// an open frame command list, after proving GPU idle; per-frame recording never frees resources.
class SceneRenderer final
{
public:
    SceneRenderer(D3D12Context& context,
                  const CanonicalScene& scene,
                  const CommandLineOptions& options,
                  const std::filesystem::path& shader_directory,
                  const std::filesystem::path& application_directory);
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;
    SceneRenderer(SceneRenderer&&) = delete;
    SceneRenderer& operator=(SceneRenderer&&) = delete;

    // Must be called before D3D12Context::begin_frame each frame.
    void prepare_frame();
    void record(const FrameRecordingContext& frame);
    // Display (swap-chain) resolution changed; D3D12Context::resize already proved GPU idle.
    void resize(std::uint32_t width, std::uint32_t height);
    void apply_input(const OrbitInput& input);

    void request_mode(RendererMode mode);
    void toggle_denoiser();
    void adjust_exposure(float delta_ev);
    void request_history_reset();

    void request_validation_capture(bool capture_ldr, bool capture_linear);
    [[nodiscard]] RendererCaptureResult finalize_validation_capture();

    [[nodiscard]] GpuPassTimings gpu_timings() const noexcept;
    [[nodiscard]] RendererResourceStats resource_stats() const noexcept;
    [[nodiscard]] RendererCameraState camera_state() const noexcept;
    [[nodiscard]] RendererMode active_mode() const noexcept { return active_mode_; }
    [[nodiscard]] float exposure_ev() const noexcept { return exposure_ev_; }
    [[nodiscard]] std::uint64_t accumulated_samples() const noexcept;
    // Accumulated per-pixel sample count after the frame prepared by the last prepare_frame().
    [[nodiscard]] std::uint64_t samples_after_prepared_frame() const noexcept { return samples_after_frame_; }
    [[nodiscard]] bool path_trace_reference_certifiable() const noexcept;
    [[nodiscard]] std::string status_json() const;

private:
    void configure_mode(RendererMode mode);
    void configure_resolution(std::uint32_t display_width, std::uint32_t display_height);
    void bind_pass_inputs();
    void ensure_ray_tracing_scene();
    void write_view_constants(std::uint32_t frame_index, const CameraFrame& camera);
    [[nodiscard]] bool mode_needs_geometry_buffer(RendererMode mode) const noexcept;
    [[nodiscard]] bool jitter_active() const noexcept;
    [[nodiscard]] float ray_extent() const noexcept;
    [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS view_constants_address(std::uint32_t frame_index) const;

    D3D12Context& context_;
    const CanonicalScene& scene_;
    std::filesystem::path shader_directory_;
    std::filesystem::path application_directory_;
    RendererSettings settings_;
    DiagnosticMode diagnostic_mode_ = DiagnosticMode::shaded;
    float environment_intensity_ = 1.0F;
    float exposure_ev_ = 0.0F;
    DxrCapabilities dxr_{};
    RendererMode requested_mode_ = RendererMode::raster;
    RendererMode active_mode_ = RendererMode::raster;
    bool mode_configured_ = false;
    std::string mode_fallback_reason_;

    std::unique_ptr<GpuSceneResources> resources_;
    std::unique_ptr<GpuTimer> timer_;
    std::unique_ptr<PbrSceneRenderer> raster_;
    std::unique_ptr<DisplayOutput> display_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> compute_root_signature_;
    std::unique_ptr<RaytracingScene> ray_tracing_scene_;
    std::unique_ptr<HybridRayTracer> hybrid_;
    std::unique_ptr<ReferencePathTracer> path_tracer_;
    std::unique_ptr<TemporalDenoiser> reflection_denoiser_;
    std::unique_ptr<TemporalDenoiser> path_denoiser_;
    std::unique_ptr<ComputePipeline> temporal_debug_pipeline_;
    std::unique_ptr<ComputeBindingSet> temporal_debug_bindings_;
    GpuTexture temporal_debug_output_;
    std::unique_ptr<DlssBackend> dlss_;
    UpscalerReport upscaler_report_{};
    std::unique_ptr<SceneAnimator> animator_;

    OrbitCamera camera_;
    TemporalFrameData temporal_frames_;
    RevisionTracker revision_tracker_;
    RenderRevisions revisions_{};
    ResetReason frame_reasons_ = ResetReason::none;
    Mat4 last_view_{};
    bool last_view_valid_ = false;
    float last_emissive_scale_ = 1.0F;
    std::uint64_t frames_prepared_ = 0;
    std::uint64_t frames_recorded_ = 0;
    std::uint64_t jitter_sequence_index_ = 0;
    std::uint64_t samples_after_frame_ = 0;
    std::uint32_t samples_this_frame_ = 0;
    bool path_trace_dispatch_this_frame_ = false;
    std::vector<Mat4> draw_world_;
    std::vector<Mat4> previous_draw_world_;
    bool transforms_changed_ = false;
    float emissive_scale_ = 1.0F;

    std::uint32_t display_width_ = 0;
    std::uint32_t display_height_ = 0;
    std::uint32_t render_width_ = 0;
    std::uint32_t render_height_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> view_constants_;
    std::byte* mapped_view_constants_ = nullptr;
    std::size_t view_constant_stride_ = 768;

    bool capture_ldr_pending_ = false;
    bool capture_linear_pending_ = false;
    std::string last_linear_domain_ = "linear-hdr-scene-radiance";
    std::string last_linear_source_;
};
}
