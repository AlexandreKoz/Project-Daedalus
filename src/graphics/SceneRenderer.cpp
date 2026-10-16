#include "graphics/SceneRenderer.h"

#include "core/Error.h"
#include "core/Log.h"
#include "graphics/ComputePass.h"
#include "graphics/GpuSceneResources.h"
#include "graphics/PbrSceneRenderer.h"
#include "graphics/raytracing/HybridRayTracer.h"
#include "graphics/raytracing/RaytracingScene.h"
#include "graphics/raytracing/ReferencePathTracer.h"
#include "graphics/temporal/TemporalDenoiser.h"
#include "rendering/SceneAnimation.h"
#include "rendering/TemporalContract.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace daedalus
{
namespace
{
constexpr float kPi = 3.14159265358979323846F;
constexpr D3D12_RESOURCE_STATES kReadState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

[[nodiscard]] bool same_matrix(const Mat4& left, const Mat4& right) noexcept
{
    return left.values == right.values;
}

[[nodiscard]] bool raster_diagnostic_passthrough(DiagnosticMode mode) noexcept
{
    // Mirrors the C2 tone-map rule: shaded and bounds are tone mapped, all other diagnostics are
    // displayed as saturated [0,1] values.
    return mode != DiagnosticMode::shaded && mode != DiagnosticMode::bounds;
}

[[nodiscard]] bool hybrid_debug_passthrough(HybridDebugView view) noexcept
{
    return view == HybridDebugView::shadow_visibility || view == HybridDebugView::reflection_hit_distance ||
           view == HybridDebugView::instance_id;
}

[[nodiscard]] bool path_trace_debug_passthrough(PathTraceDebugView view) noexcept
{
    return view == PathTraceDebugView::sample_count || view == PathTraceDebugView::variance ||
           view == PathTraceDebugView::path_length || view == PathTraceDebugView::material_id ||
           view == PathTraceDebugView::nonfinite;
}

[[nodiscard]] std::string escape_json(const std::string& value)
{
    std::string result;
    for (const char c : value)
    {
        if (c == '"' || c == '\\') result.push_back('\\');
        if (c == '\n') { result += "\\n"; continue; }
        result.push_back(c);
    }
    return result;
}

[[nodiscard]] std::uint64_t allocation_size(ID3D12Device* device, ID3D12Resource* resource) noexcept
{
    if (resource == nullptr) return 0U;
    const D3D12_RESOURCE_DESC description = resource->GetDesc();
    const D3D12_RESOURCE_ALLOCATION_INFO info = device->GetResourceAllocationInfo(0, 1, &description);
    return info.SizeInBytes == std::numeric_limits<std::uint64_t>::max() ? 0U : info.SizeInBytes;
}
}

SceneRenderer::SceneRenderer(D3D12Context& context,
                             const CanonicalScene& scene,
                             const CommandLineOptions& options,
                             const std::filesystem::path& shader_directory,
                             const std::filesystem::path& application_directory)
    : context_(context),
      scene_(scene),
      shader_directory_(shader_directory),
      application_directory_(application_directory),
      settings_(options.renderer),
      diagnostic_mode_(options.diagnostic_mode),
      environment_intensity_(options.environment_intensity),
      exposure_ev_(options.exposure_ev),
      requested_mode_(options.renderer.mode),
      camera_(scene.selected_scene.valid() && scene.selected_scene.value() < scene.scenes.size()
                  ? scene.scenes[scene.selected_scene.value()].bounds
                  : Aabb{})
{
    if (!std::isfinite(exposure_ev_) || exposure_ev_ < -24.0F || exposure_ev_ > 24.0F)
        throw std::invalid_argument("exposure must be finite and within [-24, +24] EV");
    display_width_ = context_.width();
    display_height_ = context_.height();
    render_width_ = display_width_;
    render_height_ = display_height_;

    resources_ = std::make_unique<GpuSceneResources>(context_, scene_);
    timer_ = std::make_unique<GpuTimer>(context_);
    raster_ = std::make_unique<PbrSceneRenderer>(context_, *resources_, scene_, options.diagnostic_mode, options.environment_intensity,
                                                 options.shadows_enabled, options.shadow_constant_bias, options.shadow_normal_bias,
                                                 shader_directory_, render_width_, render_height_);
    display_ = std::make_unique<DisplayOutput>(context_, *resources_, shader_directory_);
    compute_root_signature_ = create_compute_root_signature(context_.device());
    dxr_ = query_dxr_capabilities(context_);
    Log::info(dxr_.summary());

    const std::vector<PreparedRasterDraw>& draws = raster_->draws();
    draw_world_.reserve(draws.size());
    for (const PreparedRasterDraw& draw : draws) draw_world_.push_back(draw.world);
    previous_draw_world_ = draw_world_;
    if (!settings_.node_animations.empty() || settings_.emissive_pulse_period_frames.has_value())
        animator_ = std::make_unique<SceneAnimator>(scene_, draws, settings_.node_animations, settings_.emissive_pulse_period_frames);

    view_constant_stride_ = static_cast<std::size_t>(
        align_up_u64(sizeof(GpuViewConstants), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, "view constants"));
    view_constants_ = create_buffer(context_.device(), checked_multiply_u64(view_constant_stride_, D3D12Context::kFrameCount, "view constants"),
                                    D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE,
                                    L"Daedalus view constants");
    const D3D12_RANGE read_range{0, 0};
    DAEDALUS_THROW_IF_FAILED(view_constants_->Map(0, &read_range, reinterpret_cast<void**>(&mapped_view_constants_)));

    temporal_debug_pipeline_ = std::make_unique<ComputePipeline>(context_.device(), compute_root_signature_.Get(), shader_directory_,
                                                                 "TemporalDebugCS.dxil");
    temporal_debug_bindings_ = std::make_unique<ComputeBindingSet>(*resources_, resources_->block_base(DescriptorBlock::temporal_debug));

    if (settings_.upscaler.mode == UpscalerMode::dlss)
    {
        dlss_ = DlssBackend::try_create(context_, *resources_, application_directory_, settings_.upscaler.dlss_quality,
                                        display_width_, display_height_, upscaler_report_);
        if (dlss_ == nullptr)
            Log::warning("DLSS requested but unavailable; rendering at native resolution. " + upscaler_report_.reason);
    }
    else
    {
        upscaler_report_.requested = settings_.upscaler.mode;
        upscaler_report_.active = settings_.upscaler.mode;
        upscaler_report_.status = settings_.upscaler.mode == UpscalerMode::none ? "INACTIVE" : "ACTIVE";
        upscaler_report_.vendor_glue_compiled = DlssBackend::vendor_glue_compiled();
    }
    configure_mode(requested_mode_);
}

SceneRenderer::~SceneRenderer()
{
    if (view_constants_ != nullptr && mapped_view_constants_ != nullptr) view_constants_->Unmap(0, nullptr);
    mapped_view_constants_ = nullptr;
}

bool SceneRenderer::mode_needs_geometry_buffer(RendererMode mode) const noexcept
{
    RendererSettings probe = settings_;
    probe.mode = mode;
    return mode == RendererMode::hybrid ||
           (mode == RendererMode::raster && (requires_geometry_buffer(probe) || dlss_ != nullptr));
}

bool SceneRenderer::jitter_active() const noexcept
{
    // Projection jitter is only applied where a temporal consumer integrates it (DLSS) or when
    // explicitly requested for validation. The path tracer integrates the pixel footprint itself.
    return active_mode_ != RendererMode::pathtrace && (settings_.temporal.jitter || dlss_ != nullptr);
}

float SceneRenderer::ray_extent() const noexcept
{
    float scene_extent = 1.0F;
    if (scene_.selected_scene.valid() && scene_.selected_scene.value() < scene_.scenes.size())
    {
        const float diagonal = length(extent(scene_.scenes[scene_.selected_scene.value()].bounds));
        if (std::isfinite(diagonal)) scene_extent = std::max(diagonal, 1.0F);
    }
    return std::max(camera_.far_plane(), scene_extent * 8.0F);
}

void SceneRenderer::ensure_ray_tracing_scene()
{
    if (ray_tracing_scene_ != nullptr) return;
    context_.wait_for_gpu();
    ray_tracing_scene_ = std::make_unique<RaytracingScene>(context_, *resources_, scene_, raster_->draws(), draw_world_);
}

void SceneRenderer::configure_resolution(std::uint32_t display_width, std::uint32_t display_height)
{
    display_width_ = display_width;
    display_height_ = display_height;
    if (dlss_ != nullptr)
    {
        dlss_->configure(display_width, display_height);
        upscaler_report_ = dlss_->report();
        render_width_ = dlss_->render_resolution().width;
        render_height_ = dlss_->render_resolution().height;
    }
    else if (settings_.upscaler.mode == UpscalerMode::bilinear)
    {
        render_width_ = std::max(1U, static_cast<std::uint32_t>(std::lround(static_cast<double>(display_width) * settings_.upscaler.render_scale)));
        render_height_ = std::max(1U, static_cast<std::uint32_t>(std::lround(static_cast<double>(display_height) * settings_.upscaler.render_scale)));
    }
    else
    {
        render_width_ = display_width;
        render_height_ = display_height;
    }
    upscaler_report_.render_width = render_width_;
    upscaler_report_.render_height = render_height_;
    upscaler_report_.display_width = display_width_;
    upscaler_report_.display_height = display_height_;

    raster_->resize(render_width_, render_height_);
    if (hybrid_ != nullptr) hybrid_->resize(render_width_, render_height_);
    if (path_tracer_ != nullptr) path_tracer_->resize(render_width_, render_height_);
    if (reflection_denoiser_ != nullptr) reflection_denoiser_->resize(render_width_, render_height_);
    if (path_denoiser_ != nullptr) path_denoiser_->resize(render_width_, render_height_);
    temporal_debug_output_ = create_texture(context_.device(), render_width_, render_height_, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, kReadState, nullptr,
                                            L"Daedalus temporal debug output");
    temporal_debug_output_.srv = temporal_debug_bindings_->base() + kComputeBindingSetSize;
    resources_->write_texture_srv(temporal_debug_output_.srv, temporal_debug_output_.resource.Get(), temporal_debug_output_.format);
    ++revisions_.render_resolution;
    bind_pass_inputs();
}

void SceneRenderer::configure_mode(RendererMode mode)
{
    std::string fallback;
    if (mode != RendererMode::raster && !dxr_.supported())
    {
        fallback = std::string(to_string(mode)) + " requested but DXR is UNSUPPORTED (" + dxr_.reason + "); raster remains active";
        mode = RendererMode::raster;
    }
    context_.wait_for_gpu();
    try
    {
        if (mode != RendererMode::raster) ensure_ray_tracing_scene();
        const bool needs_hybrid = mode == RendererMode::hybrid;
        const bool needs_path = mode == RendererMode::pathtrace;
        if (!needs_hybrid)
        {
            hybrid_.reset();
            reflection_denoiser_.reset();
        }
        if (!needs_path)
        {
            path_tracer_.reset();
            path_denoiser_.reset();
        }
        raster_->set_geometry_buffer_enabled(mode_needs_geometry_buffer(mode));
        active_mode_ = mode;
        configure_resolution(display_width_, display_height_);
        if (needs_hybrid && hybrid_ == nullptr)
            hybrid_ = std::make_unique<HybridRayTracer>(context_, *resources_, *ray_tracing_scene_, compute_root_signature_.Get(),
                                                        shader_directory_, render_width_, render_height_);
        if (needs_hybrid && settings_.hybrid.denoise_reflections && reflection_denoiser_ == nullptr)
            reflection_denoiser_ = std::make_unique<TemporalDenoiser>(context_.device(), *resources_, compute_root_signature_.Get(),
                                                                      shader_directory_, DescriptorBlock::reflection_denoiser,
                                                                      DescriptorBlock::reflection_denoiser_filter, render_width_,
                                                                      render_height_, L"Daedalus reflection denoiser");
        if (needs_path && path_tracer_ == nullptr)
            path_tracer_ = std::make_unique<ReferencePathTracer>(context_, *resources_, *ray_tracing_scene_, compute_root_signature_.Get(),
                                                                 shader_directory_, render_width_, render_height_);
        if (needs_path && settings_.path_trace.denoise && path_denoiser_ == nullptr)
            path_denoiser_ = std::make_unique<TemporalDenoiser>(context_.device(), *resources_, compute_root_signature_.Get(),
                                                                shader_directory_, DescriptorBlock::path_trace_denoiser,
                                                                DescriptorBlock::path_trace_denoiser_filter, render_width_,
                                                                render_height_, L"Daedalus path-trace denoiser");
        bind_pass_inputs();
    }
    catch (const std::exception& error)
    {
        if (mode == RendererMode::raster) throw;
        // A DXR mode failed to initialize (e.g. state-object creation). Report precisely and keep the
        // raster baseline alive rather than presenting a broken frame.
        Log::error(std::string("DXR mode initialization failed; falling back to raster: ") + error.what());
        context_.wait_for_gpu();
        hybrid_.reset();
        reflection_denoiser_.reset();
        path_tracer_.reset();
        path_denoiser_.reset();
        active_mode_ = RendererMode::raster;
        raster_->set_geometry_buffer_enabled(mode_needs_geometry_buffer(RendererMode::raster));
        configure_resolution(display_width_, display_height_);
        fallback = std::string("DXR mode initialization failed: ") + error.what();
    }
    if (mode_configured_) ++revisions_.mode;
    mode_configured_ = true;
    mode_fallback_reason_ = fallback;
    if (!fallback.empty()) Log::warning(fallback);
    Log::info(std::string("Renderer mode active: ") + std::string(to_string(active_mode_)) + " (requested " +
              std::string(to_string(requested_mode_)) + ")");
}

void SceneRenderer::bind_pass_inputs()
{
    std::array<GpuTexture, kGBufferCount>& gbuffer = raster_->geometry_buffer();
    if (hybrid_ != nullptr)
    {
        TemporalGuide guide{&gbuffer[kGBufferNormalRoughness], &gbuffer[kGBufferPositionDepth], &gbuffer[kGBufferIdentifiers],
                            &gbuffer[kGBufferMotion]};
        if (reflection_denoiser_ != nullptr) reflection_denoiser_->bind(hybrid_->reflection(), guide);
        hybrid_->bind_inputs(gbuffer, raster_->hdr_target(), reflection_denoiser_ != nullptr ? &reflection_denoiser_->output() : nullptr);
    }
    if (path_tracer_ != nullptr && path_denoiser_ != nullptr)
    {
        TemporalGuide guide{&path_tracer_->normal_roughness(), &path_tracer_->position_depth(), &path_tracer_->identifiers(),
                            &path_tracer_->motion()};
        path_denoiser_->bind(path_tracer_->noisy_demodulated(), guide);
        path_tracer_->bind_filtered_signal(path_denoiser_->output());
    }

    // Temporal debug inputs: guide of the active mode, denoiser debug state and raw signal.
    temporal_debug_bindings_->clear();
    if (active_mode_ == RendererMode::pathtrace && path_tracer_ != nullptr)
    {
        temporal_debug_bindings_->input(0U, path_tracer_->normal_roughness());
        temporal_debug_bindings_->input(1U, path_tracer_->position_depth());
        temporal_debug_bindings_->input(2U, path_tracer_->identifiers());
        temporal_debug_bindings_->input(3U, path_tracer_->motion());
        if (path_denoiser_ != nullptr) temporal_debug_bindings_->input(4U, path_denoiser_->debug());
        temporal_debug_bindings_->input(5U, path_tracer_->noisy_demodulated());
    }
    else if (raster_->geometry_buffer_enabled())
    {
        temporal_debug_bindings_->input(0U, gbuffer[kGBufferNormalRoughness]);
        temporal_debug_bindings_->input(1U, gbuffer[kGBufferPositionDepth]);
        temporal_debug_bindings_->input(2U, gbuffer[kGBufferIdentifiers]);
        temporal_debug_bindings_->input(3U, gbuffer[kGBufferMotion]);
        if (reflection_denoiser_ != nullptr) temporal_debug_bindings_->input(4U, reflection_denoiser_->debug());
        if (hybrid_ != nullptr) temporal_debug_bindings_->input(5U, hybrid_->reflection());
        else temporal_debug_bindings_->input(5U, raster_->hdr_target());
    }
    if (temporal_debug_output_.valid()) temporal_debug_bindings_->output(0U, temporal_debug_output_);
}

void SceneRenderer::resize(std::uint32_t width, std::uint32_t height)
{
    if (width == 0U || height == 0U) return;
    context_.wait_for_gpu();
    configure_resolution(width, height);
    revision_tracker_.force(ResetReason::resize);
}

void SceneRenderer::apply_input(const OrbitInput& input)
{
    camera_.apply_input(input, render_width_, render_height_);
    if (input.reset) ++revisions_.camera_cut;
}

void SceneRenderer::request_mode(RendererMode mode)
{
    requested_mode_ = mode;
}

void SceneRenderer::toggle_denoiser()
{
    if (active_mode_ == RendererMode::pathtrace)
    {
        if (settings_.path_trace.preset == PathTracePreset::reference)
        {
            Log::warning("The reference path-tracing preset never denoises; restart with --pt-preset interactive to use the denoiser");
            return;
        }
        settings_.path_trace.denoise = !settings_.path_trace.denoise;
    }
    else
    {
        settings_.hybrid.denoise_reflections = !settings_.hybrid.denoise_reflections;
    }
    ++revisions_.settings;
    const RendererMode mode = active_mode_;
    configure_mode(mode);
}

void SceneRenderer::adjust_exposure(float delta_ev)
{
    exposure_ev_ = std::clamp(exposure_ev_ + delta_ev, -24.0F, 24.0F);
    ++revisions_.exposure;
    Log::info("Exposure EV: " + std::to_string(exposure_ev_));
}

void SceneRenderer::request_history_reset()
{
    ++revisions_.user_reset;
}

void SceneRenderer::prepare_frame()
{
    if (requested_mode_ != active_mode_ && (mode_fallback_reason_.empty() || requested_mode_ == RendererMode::raster))
        configure_mode(requested_mode_);
    else if (requested_mode_ != active_mode_)
        requested_mode_ = active_mode_; // a DXR request that already fell back is not retried every frame

    // Scripted camera motion (Campaign F validation inputs).
    const std::uint64_t script_frame = frames_prepared_ + 1U;
    if (settings_.camera_script.kind == CameraScriptKind::orbit)
        camera_.orbit_by(settings_.camera_script.degrees_per_frame * kPi / 180.0F, 0.0F);
    else if (settings_.camera_script.kind == CameraScriptKind::teleport && script_frame == settings_.camera_script.teleport_frame)
    {
        camera_.orbit_by(2.0F * kPi / 3.0F, 0.15F);
        ++revisions_.camera_cut;
        Log::info("Scripted camera teleport at frame " + std::to_string(script_frame));
    }

    // Scripted rigid animation / emissive pulse.
    transforms_changed_ = false;
    if (animator_ != nullptr)
    {
        const AnimatedFrame& animated = animator_->evaluate(frames_prepared_, false);
        draw_world_ = animated.draw_world;
        previous_draw_world_ = animated.previous_draw_world;
        transforms_changed_ = animated.transforms_changed;
        emissive_scale_ = animated.emissive_scale;
        if (animated.transforms_changed) ++revisions_.transforms;
        if (animated.emissive_changed) ++revisions_.materials;
    }
    else
    {
        previous_draw_world_ = draw_world_;
    }

    const Mat4 view = camera_.view_matrix();
    if (last_view_valid_ && !same_matrix(view, last_view_)) ++revisions_.camera;
    last_view_ = view;
    last_view_valid_ = true;

    frame_reasons_ = revision_tracker_.observe(revisions_);
    if (invalidates_temporal_history(frame_reasons_))
    {
        // Reset reasons that invalidate reprojection also make the previous transforms meaningless.
        previous_draw_world_ = draw_world_;
    }

    // Path-tracer progression for this frame (used by fixed-sample captures).
    path_trace_dispatch_this_frame_ = false;
    samples_this_frame_ = 0U;
    samples_after_frame_ = 0U;
    if (active_mode_ == RendererMode::pathtrace && path_tracer_ != nullptr)
    {
        const std::uint64_t accumulated = invalidates_accumulation(frame_reasons_) ? 0U : path_tracer_->accumulated_samples();
        std::uint64_t samples = settings_.path_trace.samples_per_frame;
        if (settings_.path_trace.target_samples.has_value())
            samples = *settings_.path_trace.target_samples > accumulated ? std::min(samples, *settings_.path_trace.target_samples - accumulated) : 0U;
        samples_this_frame_ = static_cast<std::uint32_t>(samples);
        path_trace_dispatch_this_frame_ = samples_this_frame_ > 0U;
        samples_after_frame_ = accumulated + samples;
    }
    ++frames_prepared_;
}

D3D12_GPU_VIRTUAL_ADDRESS SceneRenderer::view_constants_address(std::uint32_t frame_index) const
{
    return view_constants_->GetGPUVirtualAddress() + static_cast<UINT64>(frame_index) * view_constant_stride_;
}

void SceneRenderer::write_view_constants(std::uint32_t frame_index, const CameraFrame& camera)
{
    const CameraFrame& previous = temporal_frames_.previous();
    GpuViewConstants constants{};
    constants.view = camera.view;
    constants.projection = camera.projection_jittered;
    constants.view_projection = camera.view_projection_jittered;
    constants.view_projection_unjittered = camera.view_projection;
    constants.inverse_view_projection_unjittered = inverse(camera.view_projection);
    constants.previous_view_projection_unjittered = previous.view_projection;
    constants.previous_view_projection = previous.view_projection_jittered;
    constants.inverse_view = inverse(camera.view);
    constants.inverse_projection = inverse(camera.projection_jittered);
    constants.camera_position_near = {camera.position.x, camera.position.y, camera.position.z, camera.near_plane};
    constants.previous_camera_position_far = {previous.position.x, previous.position.y, previous.position.z, camera.far_plane};
    constants.jitter_pixels = {camera.jitter_pixels.x, camera.jitter_pixels.y, previous.jitter_pixels.x, previous.jitter_pixels.y};
    constants.render_size = {static_cast<float>(render_width_), static_cast<float>(render_height_),
                             1.0F / static_cast<float>(render_width_), 1.0F / static_cast<float>(render_height_)};
    constants.display_size = {static_cast<float>(display_width_), static_cast<float>(display_height_),
                              1.0F / static_cast<float>(display_width_), 1.0F / static_cast<float>(display_height_)};
    constants.frame_index = static_cast<std::uint32_t>(frames_recorded_ & 0xFFFFFFFFULL);
    constants.jitter_index = static_cast<std::uint32_t>(jitter_sequence_index_ & 0xFFFFFFFFULL);
    constants.scene_revision = static_cast<std::uint32_t>(revisions_.scene & 0xFFFFFFFFULL);
    constants.reset_flags = static_cast<std::uint32_t>(frame_reasons_);
    constants.exposure_ev = exposure_ev_;
    constants.environment_intensity = environment_intensity_;
    constants.emissive_scale = emissive_scale_;
    constants.history_valid = temporal_frames_.history_valid() ? 1U : 0U;
    if (active_mode_ == RendererMode::hybrid)
    {
        if (settings_.hybrid.shadows) constants.hybrid_flags |= kHybridFlagRayTracedShadows;
        if (settings_.hybrid.reflections) constants.hybrid_flags |= kHybridFlagRayTracedReflections;
    }
    if (raster_->geometry_buffer_enabled()) constants.hybrid_flags |= kHybridFlagGeometryBuffer;
    constants.reflection_max_roughness = settings_.hybrid.reflection_max_roughness;
    std::memcpy(mapped_view_constants_ + static_cast<std::size_t>(frame_index) * view_constant_stride_, &constants, sizeof(constants));
}

void SceneRenderer::record(const FrameRecordingContext& frame)
{
    if (frame.frame_index >= D3D12Context::kFrameCount) throw std::runtime_error("frame index exceeds frame partition count");
    if (frame.width != display_width_ || frame.height != display_height_)
        throw std::logic_error("SceneRenderer::resize was not called after the swap-chain resize");
    ID3D12GraphicsCommandList* command_list = frame.command_list;
    timer_->begin_frame(command_list, frame.frame_index);

    // ---- Camera frame and temporal contract -------------------------------------------------
    const float aspect = static_cast<float>(display_width_) / static_cast<float>(display_height_);
    CameraFrame camera;
    camera.view = camera_.view_matrix();
    camera.projection = camera_.projection_matrix(aspect);
    camera.view_projection = multiply(camera.projection, camera.view);
    camera.position = camera_.eye();
    camera.near_plane = camera_.near_plane();
    camera.far_plane = camera_.far_plane();
    camera.jitter_pixels = jitter_active() ? halton_jitter_pixels(jitter_sequence_index_) : Vec2{};
    if (jitter_active()) ++jitter_sequence_index_;
    camera.projection_jittered = apply_projection_jitter(camera.projection, camera.jitter_pixels, render_width_, render_height_);
    camera.view_projection_jittered = multiply(camera.projection_jittered, camera.view);
    temporal_frames_.begin_frame(camera, !invalidates_temporal_history(frame_reasons_));
    write_view_constants(frame.frame_index, camera);
    const D3D12_GPU_VIRTUAL_ADDRESS view_address = view_constants_address(frame.frame_index);

    RasterFrameInputs raster_inputs;
    raster_inputs.frame_index = frame.frame_index;
    raster_inputs.view_projection = camera.view_projection_jittered;
    raster_inputs.camera_position = camera.position;
    raster_inputs.view_constants = view_address;
    raster_inputs.draw_world = draw_world_;
    raster_inputs.previous_draw_world = previous_draw_world_;
    raster_inputs.emissive_scale = emissive_scale_;
    raster_inputs.geometry_buffer = raster_->geometry_buffer_enabled();
    raster_inputs.hybrid_composite = active_mode_ == RendererMode::hybrid;

    GpuTexture* linear = nullptr;
    bool passthrough = false;
    last_linear_domain_ = "linear-hdr-scene-radiance";

    if (active_mode_ != RendererMode::raster)
    {
        if (frame.command_list4 == nullptr) throw std::logic_error("DXR mode active without ID3D12GraphicsCommandList4");
        ray_tracing_scene_->update(frame.command_list4, frame.frame_index, draw_world_, previous_draw_world_, transforms_changed_, *timer_);
    }

    if (active_mode_ == RendererMode::raster)
    {
        raster_->record_opaque(command_list, raster_inputs, *timer_);
        raster_->record_transparent(command_list, raster_inputs, raster_->hdr_target());
        timer_->mark(command_list, GpuPass::transparent);
        linear = &raster_->hdr_target();
        passthrough = raster_diagnostic_passthrough(diagnostic_mode_);
        last_linear_source_ = "raster-hdr";
    }
    else if (active_mode_ == RendererMode::hybrid)
    {
        raster_->record_opaque(command_list, raster_inputs, *timer_);
        HybridFrameInputs hybrid_inputs;
        hybrid_inputs.frame_index = frame.frame_index;
        hybrid_inputs.view_constants = view_address;
        hybrid_inputs.light_constants = raster_->light_constants_address(frame.frame_index);
        hybrid_inputs.light_count = static_cast<std::uint32_t>(raster_->lights().size());
        hybrid_inputs.ray_extent = ray_extent();
        hybrid_inputs.settings = settings_.hybrid;
        std::array<GpuTexture, kGBufferCount>& gbuffer = raster_->geometry_buffer();
        hybrid_->record_rays(frame.command_list4, hybrid_inputs, gbuffer, *timer_);
        GpuTexture* filtered = nullptr;
        if (reflection_denoiser_ != nullptr && settings_.hybrid.denoise_reflections && settings_.hybrid.reflections)
        {
            TemporalGuide guide{&gbuffer[kGBufferNormalRoughness], &gbuffer[kGBufferPositionDepth], &gbuffer[kGBufferIdentifiers],
                                &gbuffer[kGBufferMotion]};
            reflection_denoiser_->record(command_list, view_address, settings_.temporal, temporal_frames_.history_valid(),
                                         hybrid_->reflection(), guide, *timer_);
            filtered = &reflection_denoiser_->output();
        }
        hybrid_->record_composite(command_list, hybrid_inputs, raster_->hdr_target(), gbuffer, filtered, *timer_);
        if (settings_.hybrid.debug_view == HybridDebugView::none)
        {
            raster_->record_transparent(command_list, raster_inputs, hybrid_->output());
            timer_->mark(command_list, GpuPass::transparent);
        }
        linear = &hybrid_->output();
        passthrough = hybrid_debug_passthrough(settings_.hybrid.debug_view);
        last_linear_source_ = settings_.hybrid.debug_view == HybridDebugView::none
            ? "hybrid-composite" : "hybrid-debug:" + std::string(to_string(settings_.hybrid.debug_view));
    }
    else
    {
        if (path_trace_dispatch_this_frame_)
        {
            PathTraceFrameInputs inputs;
            inputs.frame_index = frame.frame_index;
            inputs.view_constants = view_address;
            inputs.light_constants = raster_->light_constants_address(frame.frame_index);
            inputs.light_count = static_cast<std::uint32_t>(raster_->lights().size());
            inputs.ray_extent = ray_extent();
            inputs.environment_intensity = environment_intensity_;
            inputs.settings = settings_.path_trace;
            inputs.reset_accumulation = invalidates_accumulation(frame_reasons_);
            inputs.samples_this_dispatch = samples_this_frame_;
            // The raster passes do not run in path-tracing mode; the shared per-frame light constants
            // (one canonical light set for raster, hybrid and path tracing) are still written here.
            raster_->update_frame_constants(raster_inputs);
            path_tracer_->record(frame.command_list4, inputs, *timer_);
        }
        if (path_denoiser_ != nullptr && settings_.path_trace.denoise && path_trace_dispatch_this_frame_)
        {
            TemporalGuide guide{&path_tracer_->normal_roughness(), &path_tracer_->position_depth(), &path_tracer_->identifiers(),
                                &path_tracer_->motion()};
            path_denoiser_->record(command_list, view_address, settings_.temporal, temporal_frames_.history_valid(),
                                   path_tracer_->noisy_demodulated(), guide, *timer_);
            path_tracer_->record_remodulate(command_list, view_address, path_denoiser_->output(), *timer_);
            linear = &path_tracer_->denoised_output();
            last_linear_source_ = "pathtrace-denoised (biased, not a reference)";
        }
        else
        {
            linear = &path_tracer_->output();
            last_linear_source_ = settings_.path_trace.debug_view == PathTraceDebugView::none
                ? "pathtrace-accumulation-mean" : "pathtrace-debug:" + std::string(to_string(settings_.path_trace.debug_view));
        }
        passthrough = path_trace_debug_passthrough(settings_.path_trace.debug_view);
    }

    // ---- Temporal diagnostics -------------------------------------------------------------------
    if (settings_.temporal.debug_view != TemporalDebugView::none &&
        (active_mode_ == RendererMode::pathtrace ? path_tracer_ != nullptr : raster_->geometry_buffer_enabled()))
    {
        BarrierBatch barriers;
        if (active_mode_ == RendererMode::pathtrace)
        {
            for (GpuTexture* guide : {&path_tracer_->normal_roughness(), &path_tracer_->position_depth(), &path_tracer_->identifiers(),
                                      &path_tracer_->motion(), &path_tracer_->noisy_demodulated()})
                barriers.transition(*guide, kReadState);
            if (path_denoiser_ != nullptr) barriers.transition(path_denoiser_->debug(), kReadState);
        }
        else
        {
            for (GpuTexture& guide : raster_->geometry_buffer()) barriers.transition(guide, kReadState);
            if (reflection_denoiser_ != nullptr) barriers.transition(reflection_denoiser_->debug(), kReadState);
            if (hybrid_ != nullptr) barriers.transition(hybrid_->reflection(), kReadState);
            else barriers.transition(raster_->hdr_target(), kReadState);
        }
        barriers.transition(temporal_debug_output_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barriers.flush(command_list);
        ComputeConstants constants;
        constants.set_uint(0, static_cast<std::uint32_t>(settings_.temporal.debug_view));
        constants.set_float(1, 16.0F);
        constants.set_float(2, camera_.far_plane());
        const bool has_denoiser = active_mode_ == RendererMode::pathtrace ? path_denoiser_ != nullptr : reflection_denoiser_ != nullptr;
        constants.set_uint(3, has_denoiser ? 1U : 0U);
        temporal_debug_pipeline_->dispatch(command_list, *resources_, view_address, constants, *temporal_debug_bindings_,
                                           render_width_, render_height_);
        barriers.uav(temporal_debug_output_.resource.Get());
        barriers.transition(temporal_debug_output_, kReadState);
        if (hybrid_ != nullptr && active_mode_ == RendererMode::hybrid)
            barriers.transition(hybrid_->reflection(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barriers.flush(command_list);
        linear = &temporal_debug_output_;
        passthrough = settings_.temporal.debug_view != TemporalDebugView::raw_signal;
        last_linear_source_ = "temporal-debug:" + std::string(to_string(settings_.temporal.debug_view));
    }

    // ---- Optional reconstruction (Campaign G) -------------------------------------------------------
    if (dlss_ != nullptr && !passthrough)
    {
        GpuTexture* motion = active_mode_ == RendererMode::pathtrace ? &path_tracer_->motion() : &raster_->geometry_buffer()[kGBufferMotion];
        GpuTexture* linear_depth = active_mode_ == RendererMode::pathtrace ? &path_tracer_->position_depth()
                                                                           : &raster_->geometry_buffer()[kGBufferPositionDepth];
        ID3D12Resource* hardware_depth = active_mode_ == RendererMode::pathtrace ? nullptr : raster_->depth_buffer();
        dlss_->record(command_list, *linear, hardware_depth, linear_depth, *motion, temporal_frames_.current().jitter_pixels.x,
                      temporal_frames_.current().jitter_pixels.y, exposure_ev_, static_cast<float>(timer_->latest().total_ms),
                      invalidates_upscaler_history(frame_reasons_), *timer_);
        linear = &dlss_->output();
        last_linear_source_ += " -> dlss-super-resolution";
    }
    if (passthrough) last_linear_domain_ = "diagnostic-display-values";

    // ---- Presentation ---------------------------------------------------------------------------
    if (capture_ldr_pending_ || capture_linear_pending_)
    {
        display_->request_capture(capture_ldr_pending_, capture_linear_pending_, display_width_, display_height_, *linear);
        capture_ldr_pending_ = false;
        capture_linear_pending_ = false;
    }
    display_->record(command_list, frame, *linear, passthrough, exposure_ev_, *timer_);
    timer_->end_frame(command_list);
    ++frames_recorded_;
}

void SceneRenderer::request_validation_capture(bool capture_ldr, bool capture_linear)
{
    if (!capture_ldr && !capture_linear) throw std::invalid_argument("validation capture requires LDR and/or linear readback");
    if (capture_ldr_pending_ || capture_linear_pending_ || display_->capture_recorded())
        throw std::runtime_error("a validation capture is already pending");
    capture_ldr_pending_ = capture_ldr;
    capture_linear_pending_ = capture_linear;
}

RendererCaptureResult SceneRenderer::finalize_validation_capture()
{
    return display_->finalize_capture();
}

GpuPassTimings SceneRenderer::gpu_timings() const noexcept
{
    return timer_->latest();
}

RendererCameraState SceneRenderer::camera_state() const noexcept
{
    return RendererCameraState{camera_.eye(), camera_.target(), camera_.vertical_fov_radians() * 180.0F / kPi};
}

std::uint64_t SceneRenderer::accumulated_samples() const noexcept
{
    return path_tracer_ != nullptr ? path_tracer_->accumulated_samples() : 0U;
}

bool SceneRenderer::path_trace_reference_certifiable() const noexcept
{
    if (active_mode_ != RendererMode::pathtrace || path_tracer_ == nullptr) return false;
    if (!path_trace_is_unbiased_reference(settings_.path_trace)) return false;
    const RayTracingCounterSnapshot& totals = path_tracer_->counters().cumulative();
    return totals[RtCounter::nonfinite_radiance] == 0U && totals[RtCounter::negative_radiance] == 0U &&
           totals[RtCounter::clamped_samples] == 0U && !ray_tracing_scene_->tables().emissive_budget_exceeded;
}

RendererResourceStats SceneRenderer::resource_stats() const noexcept
{
    RendererResourceStats stats{};
    stats.vertex_buffer_count = resources_->primitives().size();
    stats.index_buffer_count = resources_->primitives().size();
    stats.texture_count = resources_->texture_resource_count();
    stats.material_count = scene_.materials.size() + 1U;
    stats.light_count = raster_->lights().size();
    stats.material_buffer_count = 1U;
    stats.light_buffer_count = 1U;
    stats.hdr_target_count = raster_->render_target_count();
    stats.depth_target_count = 1U;
    stats.shadow_map_count = 1U;
    stats.diagnostic_resource_count = 1U;
    stats.descriptor_count = resources_->descriptor_count();
    stats.logical_geometry_bytes = scene_.source.resource_usage.canonical_geometry_bytes;
    stats.canonical_retained_bytes = scene_.source.resource_usage.retained_bytes;
    stats.committed_allocation_bytes = resources_->committed_allocation_bytes() + raster_->committed_allocation_bytes() +
                                       allocation_size(context_.device(), view_constants_.Get());
    if (ray_tracing_scene_ != nullptr)
    {
        stats.ray_tracing_allocation_bytes += ray_tracing_scene_->committed_allocation_bytes();
        stats.blas_count = ray_tracing_scene_->acceleration_stats().blas_count;
        stats.blas_bytes = ray_tracing_scene_->acceleration_stats().blas_bytes;
        stats.tlas_bytes = ray_tracing_scene_->acceleration_stats().tlas_bytes;
    }
    if (hybrid_ != nullptr) stats.ray_tracing_allocation_bytes += hybrid_->committed_allocation_bytes();
    if (path_tracer_ != nullptr) stats.ray_tracing_allocation_bytes += path_tracer_->committed_allocation_bytes();
    if (reflection_denoiser_ != nullptr) stats.temporal_allocation_bytes += reflection_denoiser_->committed_allocation_bytes();
    if (path_denoiser_ != nullptr) stats.temporal_allocation_bytes += path_denoiser_->committed_allocation_bytes();
    stats.temporal_allocation_bytes += allocation_size(context_.device(), temporal_debug_output_.resource.Get());
    stats.committed_allocation_bytes += stats.ray_tracing_allocation_bytes + stats.temporal_allocation_bytes;
    return stats;
}

std::string SceneRenderer::status_json() const
{
    std::ostringstream stream;
    stream << "{\n";
    stream << "    \"requested_mode\": \"" << to_string(requested_mode_) << "\",\n";
    stream << "    \"active_mode\": \"" << to_string(active_mode_) << "\",\n";
    stream << "    \"mode_fallback_reason\": \"" << escape_json(mode_fallback_reason_) << "\",\n";
    stream << "    \"dxr\": " << dxr_.to_json() << ",\n";
    stream << "    \"render_resolution\": [" << render_width_ << ", " << render_height_ << "],\n";
    stream << "    \"display_resolution\": [" << display_width_ << ", " << display_height_ << "],\n";
    stream << "    \"signal_domain\": \"" << last_linear_domain_ << "\",\n";
    stream << "    \"signal_source\": \"" << escape_json(last_linear_source_) << "\",\n";
    stream << "    \"frames_recorded\": " << frames_recorded_ << ",\n";
    stream << "    \"jitter\": {\"active\": " << (jitter_active() ? "true" : "false") << ", \"sequence\": \"halton-2-3-16\", \"current_pixels\": ["
           << temporal_frames_.current().jitter_pixels.x << ", " << temporal_frames_.current().jitter_pixels.y << "]},\n";
    stream << "    \"motion_vector_convention\": \"previous_pixel - current_pixel, render pixels, jitter-free; z = previous linear depth\",\n";
    stream << "    \"last_reset_reasons\": \"" << describe_reset_reasons(frame_reasons_) << "\",\n";
    stream << "    \"reset_counts\": " << revision_tracker_.counts_json() << ",\n";
    stream << "    \"geometry_buffer\": " << (raster_->geometry_buffer_enabled() ? "true" : "false") << ",\n";
    stream << "    \"hybrid\": {\"shadows\": " << (settings_.hybrid.shadows ? "true" : "false")
           << ", \"reflections\": " << (settings_.hybrid.reflections ? "true" : "false")
           << ", \"reflection_max_roughness\": " << settings_.hybrid.reflection_max_roughness
           << ", \"reflection_bounce_limit\": 1, \"denoise_reflections\": " << (settings_.hybrid.denoise_reflections ? "true" : "false")
           << ", \"debug_view\": \"" << to_string(settings_.hybrid.debug_view) << "\"";
    if (hybrid_ != nullptr)
        stream << ", \"counters_latest\": " << hybrid_->counters().to_json(hybrid_->counters().latest())
               << ", \"counters_cumulative\": " << hybrid_->counters().to_json(hybrid_->counters().cumulative());
    stream << "},\n";
    stream << "    \"path_trace\": {\"preset\": \"" << to_string(settings_.path_trace.preset) << "\", \"samples_per_frame\": "
           << settings_.path_trace.samples_per_frame << ", \"max_depth\": " << settings_.path_trace.max_depth
           << ", \"min_russian_roulette_depth\": " << settings_.path_trace.min_russian_roulette_depth
           << ", \"seed\": " << settings_.path_trace.seed << ", \"target_samples\": ";
    if (settings_.path_trace.target_samples.has_value()) stream << *settings_.path_trace.target_samples;
    else stream << "null";
    stream << ", \"accumulated_samples\": " << accumulated_samples() << ", \"firefly_clamp\": " << settings_.path_trace.firefly_clamp
           << ", \"denoise\": " << (settings_.path_trace.denoise ? "true" : "false")
           << ", \"configuration_unbiased\": " << (path_trace_is_unbiased_reference(settings_.path_trace) ? "true" : "false")
           << ", \"reference_certifiable\": " << (path_trace_reference_certifiable() ? "true" : "false")
           << ", \"rng\": \"pcg4d(pixel.x, pixel.y, sample_index, seed) + pcg32 stream\""
           << ", \"debug_view\": \"" << to_string(settings_.path_trace.debug_view) << "\"";
    if (path_tracer_ != nullptr)
        stream << ", \"counters_latest\": " << path_tracer_->counters().to_json(path_tracer_->counters().latest())
               << ", \"counters_cumulative\": " << path_tracer_->counters().to_json(path_tracer_->counters().cumulative());
    stream << "},\n";
    stream << "    \"temporal\": {\"atrous_iterations\": " << settings_.temporal.atrous_iterations
           << ", \"max_history_length\": " << settings_.temporal.max_history_length
           << ", \"history_clip_gamma\": " << settings_.temporal.history_clip_gamma
           << ", \"debug_view\": \"" << to_string(settings_.temporal.debug_view) << "\"";
    if (reflection_denoiser_ != nullptr)
        stream << ", \"reflection_denoiser\": {\"frames\": " << reflection_denoiser_->stats().frames
               << ", \"history_resets\": " << reflection_denoiser_->stats().history_resets << '}';
    if (path_denoiser_ != nullptr)
        stream << ", \"path_denoiser\": {\"frames\": " << path_denoiser_->stats().frames
               << ", \"history_resets\": " << path_denoiser_->stats().history_resets << '}';
    stream << "},\n";
    stream << "    \"animation\": {\"camera_script\": \"";
    switch (settings_.camera_script.kind)
    {
    case CameraScriptKind::none: stream << "static"; break;
    case CameraScriptKind::orbit: stream << "orbit:" << settings_.camera_script.degrees_per_frame; break;
    case CameraScriptKind::teleport: stream << "teleport:" << settings_.camera_script.teleport_frame; break;
    }
    stream << "\", \"node_animations\": " << settings_.node_animations.size() << ", \"emissive_pulse_period\": ";
    if (settings_.emissive_pulse_period_frames.has_value()) stream << *settings_.emissive_pulse_period_frames;
    else stream << "null";
    stream << ", \"skinned_motion\": \"UNSUPPORTED\"},\n";
    if (ray_tracing_scene_ != nullptr)
        stream << "    \"acceleration_structures\": " << ray_tracing_scene_->acceleration_stats().to_json() << ",\n"
               << "    \"emissive_triangles\": " << ray_tracing_scene_->emissive_triangle_count()
               << ", \"emissive_nee_budget_exceeded\": " << (ray_tracing_scene_->tables().emissive_budget_exceeded ? "true" : "false") << ",\n";
    stream << "    \"upscaler\": " << upscaler_report_.to_json() << "\n";
    stream << "  }";
    return stream.str();
}
}
