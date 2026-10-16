#include "Application.h"

#include "assets/AssetLoader.h"
#include "assets/FloatImage.h"
#include "assets/ImageEncoder.h"
#include "core/Log.h"
#include "core/Version.h"
#include "graphics/D3D12Context.h"
#include "graphics/SceneRenderer.h"
#include "platform/Win32Window.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef DAEDALUS_BUILD_TYPE
#define DAEDALUS_BUILD_TYPE "Unknown"
#endif

namespace daedalus
{
namespace
{
[[nodiscard]] std::filesystem::path executable_directory()
{
    std::vector<wchar_t> buffer(1024);
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            throw std::runtime_error("GetModuleFileNameW failed");
        }
        if (length < buffer.size() - 1)
        {
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        }
        if (buffer.size() >= 32768)
        {
            throw std::runtime_error("executable path exceeds the supported Win32 path length");
        }
        buffer.resize(buffer.size() * 2);
    }
}

[[nodiscard]] std::string boolean_text(bool value)
{
    return value ? "yes" : "no";
}

[[nodiscard]] bool should_show_fatal_error_dialog(const CommandLineOptions& options) noexcept
{
    // Frame-limited and stress runs are automation paths; a modal MessageBox would deadlock CI/acceptance runs.
    return !options.suppress_error_dialog &&
           !options.frame_limit.has_value() &&
           !options.stress_reload_count.has_value() &&
           !options.stress_resize;
}


[[nodiscard]] std::string json_escape(std::string_view value)
{
    std::string result;
    result.reserve(value.size() + 8U);
    for (const char c : value)
    {
        switch (c)
        {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += c; break;
        }
    }
    return result;
}

void write_text_file(const std::filesystem::path& path, const std::string& text)
{
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("failed to open output: " + path.string());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("failed to write output: " + path.string());
}

void write_capture_metadata(const std::filesystem::path& path,
                            const CommandLineOptions& options,
                            const CanonicalScene& scene,
                            const D3D12Context& graphics,
                            const SceneRenderer& renderer,
                            const RendererCaptureResult& capture,
                            std::uint64_t frame_number)
{
    const RendererCameraState camera = renderer.camera_state();
    std::string selected_scene = "";
    if (scene.selected_scene.valid() && scene.selected_scene.value() < scene.scenes.size())
        selected_scene = scene.scenes[scene.selected_scene.value()].name;
    std::ostringstream stream;
    stream << "{\n"
           << "  \"schema\": \"daedalus.capture-metadata/2\",\n"
           << "  \"project_version\": \"" << json_escape(std::string(kVersion)) << "\",\n"
           << "  \"build_configuration\": \"" << json_escape(DAEDALUS_BUILD_TYPE) << "\",\n"
           << "  \"source_asset\": \"" << json_escape(scene.source.display_name) << "\",\n"
           << "  \"asset_key\": \"" << json_escape(scene.source.deterministic_asset_key) << "\",\n"
           << "  \"scene\": \"" << json_escape(selected_scene) << "\",\n"
           << "  \"scene_index\": " << (scene.selected_scene.valid() ? std::to_string(scene.selected_scene.value()) : std::string("null")) << ",\n"
           << "  \"diagnostic_mode\": \"" << to_string(options.diagnostic_mode) << "\",\n"
           << "  \"width\": " << capture.width << ",\n"
           << "  \"height\": " << capture.height << ",\n"
           << "  \"frame\": " << frame_number << ",\n"
           << "  \"exposure_ev\": " << renderer.exposure_ev() << ",\n"
           << "  \"tone_map\": \"ACES-fitted\",\n"
           << "  \"environment\": \"daedalus-procedural-sky-v1\",\n"
           << "  \"environment_intensity\": " << options.environment_intensity << ",\n"
           << "  \"shadows_enabled\": " << (options.shadows_enabled ? "true" : "false") << ",\n"
           << "  \"shadow_map_size\": 2048,\n"
           << "  \"shadow_constant_bias\": " << options.shadow_constant_bias << ",\n"
           << "  \"shadow_normal_bias\": " << options.shadow_normal_bias << ",\n"
           << "  \"camera\": {\n"
           << "    \"mode\": \"orbit-frame-bounds\",\n"
           << "    \"eye\": [" << camera.eye.x << ", " << camera.eye.y << ", " << camera.eye.z << "],\n"
           << "    \"target\": [" << camera.target.x << ", " << camera.target.y << ", " << camera.target.z << "],\n"
           << "    \"vertical_fov_degrees\": " << camera.vertical_fov_degrees << "\n"
           << "  },\n"
           << "  \"adapter\": \"" << json_escape(graphics.adapter_name()) << "\",\n"
           << "  \"warp\": " << (graphics.using_warp() ? "true" : "false") << ",\n"
           << "  \"dedicated_vram_bytes\": " << graphics.dedicated_video_memory() << ",\n"
           << "  \"feature_level\": \"" << json_escape(graphics.feature_level_name()) << "\",\n"
           << "  \"debug_layer\": " << (graphics.debug_layer_enabled() ? "true" : "false") << ",\n"
           << "  \"hdr_nan_count\": " << capture.hdr_nan_count << ",\n"
           << "  \"hdr_infinity_count\": " << capture.hdr_infinity_count << ",\n"
           << "  \"linear_capture\": {\"width\": " << capture.linear_width << ", \"height\": " << capture.linear_height
           << ", \"format\": \"" << json_escape(capture.linear_format) << "\", \"pfm\": "
           << (options.capture_hdr_path.has_value() ? "\"" + json_escape(options.capture_hdr_path->generic_string()) + "\"" : std::string("null"))
           << "},\n"
           << "  \"path_trace_accumulated_samples\": " << renderer.accumulated_samples() << ",\n"
           << "  \"path_trace_reference_certifiable\": " << (renderer.path_trace_reference_certifiable() ? "true" : "false") << ",\n"
           << "  \"renderer\": " << renderer.status_json() << "\n"
           << "}\n";
    write_text_file(path, stream.str());
}

void write_benchmark_report(const std::filesystem::path& path,
                            const CommandLineOptions& options,
                            const CanonicalScene& scene,
                            const D3D12Context& graphics,
                            const SceneRenderer& renderer,
                            std::span<const double> cpu_frame_ms,
                            std::span<const GpuPassTimings> gpu_samples,
                            const RendererResourceStats& resources,
                            std::uint64_t presented_frames)
{
    if (cpu_frame_ms.empty()) throw std::runtime_error("benchmark produced no post-warmup samples");
    double sum = 0.0;
    double minimum = cpu_frame_ms.front();
    double maximum = cpu_frame_ms.front();
    for (const double value : cpu_frame_ms)
    {
        sum += value;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }

    double gpu_shadow = 0.0;
    double gpu_opaque = 0.0;
    double gpu_transparent = 0.0;
    double gpu_tone = 0.0;
    double gpu_total = 0.0;
    std::array<double, static_cast<std::size_t>(GpuPass::count)> gpu_pass_sums{};
    for (const GpuPassTimings& sample : gpu_samples)
    {
        gpu_shadow += sample.shadow_ms;
        gpu_opaque += sample.opaque_ms;
        gpu_transparent += sample.transparent_ms;
        gpu_tone += sample.tone_map_ms;
        gpu_total += sample.total_ms;
        for (std::size_t pass = 0; pass < gpu_pass_sums.size(); ++pass) gpu_pass_sums[pass] += sample.pass_ms[pass];
    }
    const double gpu_divisor = gpu_samples.empty() ? 1.0 : static_cast<double>(gpu_samples.size());
    std::string selected_scene;
    if (scene.selected_scene.valid() && scene.selected_scene.value() < scene.scenes.size())
        selected_scene = scene.scenes[scene.selected_scene.value()].name;

    std::ostringstream stream;
    stream << "{\n"
           << "  \"schema\": \"daedalus.benchmark/2\",\n"
           << "  \"project_version\": \"" << json_escape(std::string(kVersion)) << "\",\n"
           << "  \"build_configuration\": \"" << json_escape(DAEDALUS_BUILD_TYPE) << "\",\n"
           << "  \"source_asset\": \"" << json_escape(scene.source.display_name) << "\",\n"
           << "  \"scene\": \"" << json_escape(selected_scene) << "\",\n"
           << "  \"diagnostic_mode\": \"" << to_string(options.diagnostic_mode) << "\",\n"
           << "  \"asset_key\": \"" << json_escape(scene.source.deterministic_asset_key) << "\",\n"
           << "  \"resolution\": [" << graphics.width() << ", " << graphics.height() << "],\n"
           << "  \"presented_frames\": " << presented_frames << ",\n"
           << "  \"warmup_frames\": " << options.benchmark_warmup_frames.value_or(0U) << ",\n"
           << "  \"sample_count\": " << cpu_frame_ms.size() << ",\n"
           << "  \"cpu_frame_ms_mean\": " << (sum / static_cast<double>(cpu_frame_ms.size())) << ",\n"
           << "  \"cpu_frame_ms_min\": " << minimum << ",\n"
           << "  \"cpu_frame_ms_max\": " << maximum << ",\n"
           << "  \"gpu_sample_count\": " << gpu_samples.size() << ",\n"
           << "  \"gpu_timing_valid\": " << (!gpu_samples.empty() ? "true" : "false") << ",\n"
           << "  \"gpu_shadow_ms_mean\": " << (gpu_shadow / gpu_divisor) << ",\n"
           << "  \"gpu_opaque_ms_mean\": " << (gpu_opaque / gpu_divisor) << ",\n"
           << "  \"gpu_transparent_ms_mean\": " << (gpu_transparent / gpu_divisor) << ",\n"
           << "  \"gpu_tone_map_ms_mean\": " << (gpu_tone / gpu_divisor) << ",\n"
           << "  \"gpu_total_ms_mean\": " << (gpu_total / gpu_divisor) << ",\n"
           << "  \"gpu_pass_ms_mean\": {";
    for (std::size_t pass = 0; pass < gpu_pass_sums.size(); ++pass)
        stream << (pass == 0U ? "" : ", ") << '"' << to_string(static_cast<GpuPass>(pass)) << "\": " << (gpu_pass_sums[pass] / gpu_divisor);
    stream << "},\n"
           << "  \"adapter\": \"" << json_escape(graphics.adapter_name()) << "\",\n"
           << "  \"warp\": " << (graphics.using_warp() ? "true" : "false") << ",\n"
           << "  \"dedicated_vram_bytes\": " << graphics.dedicated_video_memory() << ",\n"
           << "  \"resources\": {\n"
           << "    \"vertex_buffers\": " << resources.vertex_buffer_count << ",\n"
           << "    \"index_buffers\": " << resources.index_buffer_count << ",\n"
           << "    \"textures\": " << resources.texture_count << ",\n"
           << "    \"materials\": " << resources.material_count << ",\n"
           << "    \"lights\": " << resources.light_count << ",\n"
           << "    \"material_buffers\": " << resources.material_buffer_count << ",\n"
           << "    \"light_buffers\": " << resources.light_buffer_count << ",\n"
           << "    \"hdr_targets\": " << resources.hdr_target_count << ",\n"
           << "    \"depth_targets\": " << resources.depth_target_count << ",\n"
           << "    \"shadow_maps\": " << resources.shadow_map_count << ",\n"
           << "    \"diagnostic_resources\": " << resources.diagnostic_resource_count << ",\n"
           << "    \"descriptors\": " << resources.descriptor_count << ",\n"
           << "    \"logical_geometry_bytes\": " << resources.logical_geometry_bytes << ",\n"
           << "    \"canonical_retained_bytes\": " << resources.canonical_retained_bytes << ",\n"
           << "    \"committed_allocation_estimate_bytes\": " << resources.committed_allocation_bytes << ",\n"
           << "    \"ray_tracing_allocation_estimate_bytes\": " << resources.ray_tracing_allocation_bytes << ",\n"
           << "    \"temporal_allocation_estimate_bytes\": " << resources.temporal_allocation_bytes << ",\n"
           << "    \"blas_count\": " << resources.blas_count << ",\n"
           << "    \"blas_bytes\": " << resources.blas_bytes << ",\n"
           << "    \"tlas_bytes\": " << resources.tlas_bytes << "\n"
           << "  },\n"
           << "  \"renderer\": " << renderer.status_json() << ",\n"
           << "  \"exposure_ev\": " << renderer.exposure_ev() << ",\n"
           << "  \"environment_intensity\": " << options.environment_intensity << ",\n"
           << "  \"shadows_enabled\": " << (options.shadows_enabled ? "true" : "false") << ",\n"
           << "  \"shadow_constant_bias\": " << options.shadow_constant_bias << ",\n"
           << "  \"shadow_normal_bias\": " << options.shadow_normal_bias << "\n"
           << "}\n";
    write_text_file(path, stream.str());
}

void log_import_report(const ImportReport& report)
{
    Log::info(report.summary());
    for (const Diagnostic& diagnostic : report.diagnostics)
    {
        std::ostringstream stream;
        stream << '[' << to_string(diagnostic.severity) << "] " << to_string(diagnostic.code)
               << " at " << diagnostic.location << ": " << diagnostic.message;
        if (!diagnostic.expected.empty()) stream << " expected=" << diagnostic.expected;
        if (!diagnostic.observed.empty()) stream << " observed=" << diagnostic.observed;
        if (diagnostic.severity == DiagnosticSeverity::error) Log::error(stream.str());
        else if (diagnostic.severity == DiagnosticSeverity::warning) Log::warning(stream.str());
        else Log::info(stream.str());
    }
}
}

Application::Application(CommandLineOptions options) : options_(std::move(options)), primary_asset_path_(options_.asset_path)
{
}

Application::~Application()
{
    shutdown();
}

int Application::execute(const CommandLineOptions& options)
{
    Application application(options);
    try
    {
        application.initialize();
        return application.run();
    }
    catch (const std::exception& error)
    {
        Log::error(error.what());
        if (should_show_fatal_error_dialog(options))
            MessageBoxA(nullptr, error.what(), "Project Daedalus - fatal error", MB_OK | MB_ICONERROR | MB_TASKMODAL);
        application.shutdown();
        return EXIT_FAILURE;
    }
    catch (...)
    {
        constexpr const char* message = "unknown fatal error";
        Log::error(message);
        if (should_show_fatal_error_dialog(options))
            MessageBoxA(nullptr, message, "Project Daedalus - fatal error", MB_OK | MB_ICONERROR | MB_TASKMODAL);
        application.shutdown();
        return EXIT_FAILURE;
    }
}

int Application::run()
{
    std::uint64_t presented_frames = 0;
    bool capture_completed = false;
    const std::uint64_t capture_frame = options_.capture_frame.value_or(1U);
    std::vector<double> benchmark_cpu_frame_ms;
    std::vector<GpuPassTimings> benchmark_gpu_samples;
    const std::uint64_t warmup_frames = options_.benchmark_warmup_frames.value_or(0U);
    window_->show(SW_SHOWDEFAULT);
    if (options_.stress_resize) window_->begin_stress_sequence();

    while (window_->process_messages())
    {
        const auto cpu_frame_begin = std::chrono::steady_clock::now();
        if (const auto resized = window_->consume_resize(); resized.has_value())
        {
            graphics_->resize(resized->first, resized->second);
            renderer_->resize(resized->first, resized->second);
        }
        if (options_.stress_resize && !stress_resize_completed_)
            stress_resize_completed_ = window_->advance_stress_sequence();

        if (window_->consume_reload_request())
        {
            reload_scene();
        }
        const RendererKeyCommands commands = window_->consume_renderer_commands();
        if (commands.mode.has_value()) renderer_->request_mode(*commands.mode);
        if (commands.toggle_denoiser) renderer_->toggle_denoiser();
        if (commands.exposure_delta != 0.0F) renderer_->adjust_exposure(commands.exposure_delta);
        if (commands.reset_history) renderer_->request_history_reset();
        renderer_->apply_input(window_->consume_orbit_input());

        if (window_->minimized())
        {
            Sleep(16);
            continue;
        }

        // Recreation (mode switches) and per-frame scripted state happen before the frame's command
        // list is opened; capture decisions use the prepared sample count for fixed-sample captures.
        renderer_->prepare_frame();
        const std::uint64_t next_frame_number = presented_frames + 1U;
        const bool capture_due = options_.capture_samples.has_value()
            ? renderer_->samples_after_prepared_frame() == *options_.capture_samples
            : next_frame_number == capture_frame;
        const bool capture_this_frame = !capture_completed && capture_requested(options_) && capture_due;
        if (capture_this_frame)
            renderer_->request_validation_capture(options_.capture_path.has_value(),
                                                  options_.validate_hdr || options_.capture_hdr_path.has_value());

        const FrameRecordingContext frame = graphics_->begin_frame();
        renderer_->record(frame);
        graphics_->end_frame();
        ++presented_frames;

        if (capture_this_frame)
        {
            complete_capture(presented_frames);
            capture_completed = true;
        }

        const auto cpu_frame_end = std::chrono::steady_clock::now();
        if (options_.benchmark_output_path.has_value() && presented_frames > warmup_frames)
        {
            benchmark_cpu_frame_ms.push_back(
                std::chrono::duration<double, std::milli>(cpu_frame_end - cpu_frame_begin).count());
            const GpuPassTimings gpu_sample = renderer_->gpu_timings();
            if (gpu_sample.valid) benchmark_gpu_samples.push_back(gpu_sample);
        }
        run_stress_actions(presented_frames);

        if (options_.frame_limit.has_value() && presented_frames >= *options_.frame_limit)
        {
            Log::info("Requested frame limit reached; beginning clean shutdown");
            break;
        }
    }

    if (options_.stress_reload_count.has_value() && stress_reloads_completed_ != *options_.stress_reload_count)
        throw std::runtime_error("runtime stress ended before all requested scene reloads completed");
    if (options_.stress_resize && !stress_resize_completed_)
        throw std::runtime_error("runtime stress ended before the resize-state sequence completed");
    if (capture_requested(options_) && !capture_completed)
        throw std::runtime_error(options_.capture_samples.has_value()
                                     ? "application ended before the requested path-tracing sample count was captured"
                                     : "application ended before the requested validation capture frame");

    graphics_->wait_for_gpu();
    if (options_.benchmark_output_path.has_value())
    {
        const RendererResourceStats resources = renderer_->resource_stats();
        write_benchmark_report(*options_.benchmark_output_path, options_, scene_, *graphics_, *renderer_,
                               benchmark_cpu_frame_ms, benchmark_gpu_samples, resources, presented_frames);
        Log::info("Wrote benchmark report to " + options_.benchmark_output_path->string());
    }
    std::ostringstream stream;
    stream << "Application exiting cleanly after " << presented_frames << " presented frames";
    Log::info(stream.str());
    shutdown();
    return EXIT_SUCCESS;
}

void Application::complete_capture(std::uint64_t presented_frames)
{
    graphics_->wait_for_gpu();
    const RendererCaptureResult capture = renderer_->finalize_validation_capture();
    if (options_.capture_path.has_value())
    {
        encode_png_rgba8(*options_.capture_path, capture.width, capture.height, capture.rgba8);
        Log::info("Wrote deterministic capture to " + options_.capture_path->string());
    }
    if (options_.capture_hdr_path.has_value())
    {
        FloatImageRgb image;
        image.width = capture.linear_width;
        image.height = capture.linear_height;
        image.rgb = capture.linear_rgb;
        write_pfm(*options_.capture_hdr_path, image);
        Log::info("Wrote linear float32 capture (" + capture.linear_format + ") to " + options_.capture_hdr_path->string());
    }
    std::optional<std::filesystem::path> metadata_path = options_.capture_metadata_path;
    if (!metadata_path.has_value() && options_.capture_path.has_value())
        metadata_path = std::filesystem::path(options_.capture_path->string() + ".json");
    if (!metadata_path.has_value() && options_.capture_hdr_path.has_value())
        metadata_path = std::filesystem::path(options_.capture_hdr_path->string() + ".json");
    if (metadata_path.has_value())
    {
        write_capture_metadata(*metadata_path, options_, scene_, *graphics_, *renderer_, capture, presented_frames);
        Log::info("Wrote capture metadata to " + metadata_path->string());
    }
    if (options_.validate_hdr && (capture.hdr_nan_count != 0U || capture.hdr_infinity_count != 0U))
        throw std::runtime_error("HDR validation failed: NaN/Inf values were detected in the pre-tone-map linear signal");
    if (options_.validate_hdr)
        Log::info("HDR scan: zero NaN/Inf components in the captured pre-tone-map signal (" + capture.linear_format + ")");
}

void Application::load_scene()
{
    if (options_.builtin_scene.has_value() || !options_.asset_path.has_value())
    {
        const std::string name = options_.builtin_scene.value_or("triangle");
        scene_ = make_builtin_scene(name);
        import_report_ = make_import_report(scene_, ImportStatus::success, {}, "builtin-canonical-scene:" + name);
        Log::info(options_.builtin_scene.has_value() ? "Using built-in validation scene '" + name + "'"
                                                     : std::string("No --asset was supplied; using the canonical built-in triangle"));
        if (options_.dump_scene)
        {
            const std::string hierarchy = dump_scene_hierarchy(scene_);
            std::cout << hierarchy;
            Log::info("Canonical scene hierarchy:\n" + hierarchy);
        }
        write_import_report();
        return;
    }

    AssetLoadOptions load;
    load.source = *options_.asset_path;
    load.import_settings.scene_selector = options_.scene_selector;
    load.cache_directory = options_.asset_cache_directory;
    load.texture_search_roots = options_.texture_search_roots;
    load.blender_executable = options_.blender_executable;
    load.blender_timeout_seconds = options_.blender_timeout_seconds;
    load.temporary_directory = executable_directory_ / "runtime" / "conversion";
    AssetLoadResult result = load_asset(load);
    import_report_ = result.import.report;
    log_import_report(import_report_);
    for (const std::string& note : result.notes) Log::info(note);
    if (!result.import.succeeded())
    {
        write_import_report();
        throw std::runtime_error("asset import failed with status " + std::string(to_string(import_report_.status)));
    }
    scene_ = std::move(result.import.scene);

    if (options_.dump_scene)
    {
        const std::string hierarchy = dump_scene_hierarchy(scene_);
        std::cout << hierarchy;
        Log::info("Canonical scene hierarchy:\n" + hierarchy);
    }
    write_import_report();
}

void Application::create_renderer()
{
    renderer_ = std::make_unique<SceneRenderer>(*graphics_, scene_, options_, shader_directory_, executable_directory_);
}

void Application::reload_scene()
{
    Log::info("Reload requested; waiting for GPU before replacing scene resources");
    graphics_->wait_for_gpu();
    renderer_.reset();
    load_scene();
    create_renderer();
    Log::info("Scene reload complete");
}

void Application::run_stress_actions(std::uint64_t presented_frames)
{
    if (!options_.stress_reload_count.has_value() || stress_reloads_completed_ >= *options_.stress_reload_count)
        return;
    constexpr std::uint64_t frames_per_reload = 5;
    if (presented_frames % frames_per_reload != 0) return;

    if (options_.stress_alternate_asset_path.has_value())
    {
        alternate_asset_active_ = !alternate_asset_active_;
        options_.asset_path = alternate_asset_active_ ? options_.stress_alternate_asset_path : primary_asset_path_;
    }
    reload_scene();
    ++stress_reloads_completed_;
    Log::info("Runtime stress reload " + std::to_string(stress_reloads_completed_) + "/" +
              std::to_string(*options_.stress_reload_count) + " complete");
}

void Application::write_import_report() const
{
    if (!options_.import_report_path.has_value()) return;

    const std::filesystem::path parent = options_.import_report_path->parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    std::ofstream output(*options_.import_report_path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("failed to open import report output: " + options_.import_report_path->string());
    const std::string json = import_report_.to_json();
    output.write(json.data(), static_cast<std::streamsize>(json.size()));
    if (!output) throw std::runtime_error("failed to write import report: " + options_.import_report_path->string());
    Log::info("Wrote import report to " + options_.import_report_path->string());
}

void Application::initialize()
{
    constexpr std::uint32_t initial_width = 1280;
    constexpr std::uint32_t initial_height = 720;
    const std::filesystem::path executable_dir = executable_directory();
    executable_directory_ = executable_dir;
    const std::filesystem::path runtime_dir = executable_dir / "runtime";
    Log::initialize(runtime_dir / "log" / "Daedalus.log");

    Log::info("Project Daedalus version " + std::string(kVersion));
    Log::info("Build type: " DAEDALUS_BUILD_TYPE);
    Log::info(std::string("Process architecture: ") + (sizeof(void*) == 8 ? "x64" : "non-x64"));
    Log::info("Diagnostic mode: " + std::string(to_string(options_.diagnostic_mode)));
    Log::info("Exposure EV: " + std::to_string(options_.exposure_ev));
    Log::info("Environment intensity: " + std::to_string(options_.environment_intensity));
    Log::info(std::string("Shadows enabled: ") + (options_.shadows_enabled ? "yes" : "no"));
    Log::info("Requested renderer mode: " + std::string(to_string(options_.renderer.mode)) +
              " | upscaler: " + std::string(to_string(options_.renderer.upscaler.mode)));

    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com_result)) throw std::runtime_error("CoInitializeEx failed for Windows graphics services");
    com_initialized_ = true;

    load_scene();

    std::ostringstream dimensions;
    dimensions << "Initial client dimensions: " << initial_width << 'x' << initial_height;
    Log::info(dimensions.str());

    window_ = std::make_unique<Win32Window>(L"Project Daedalus - Raster / Hybrid DXR / Path Tracing Viewer", initial_width, initial_height);
    graphics_ = std::make_unique<D3D12Context>(
        window_->native_handle(), window_->client_width(), window_->client_height(), options_.use_warp);

    shader_directory_ = executable_dir / "shaders";
    create_renderer();

    Log::info("Selected adapter: " + graphics_->adapter_name());
    Log::info("Feature level: " + graphics_->feature_level_name());
    Log::info("Debug layer enabled: " + boolean_text(graphics_->debug_layer_enabled()));
    Log::info("WARP in use: " + boolean_text(graphics_->using_warp()));
    Log::info("Swap-chain buffer count: " + std::to_string(D3D12Context::kFrameCount));
    initialized_ = true;
}

void Application::shutdown() noexcept
{
    if (shutdown_complete_)
    {
        return;
    }
    shutdown_complete_ = true;

    const bool gpu_idle = graphics_ == nullptr || graphics_->prepare_for_shutdown();
    bool resources_abandoned = false;

    if (gpu_idle)
    {
        renderer_.reset();
        if (graphics_ != nullptr)
        {
            graphics_->shutdown(options_.report_live_objects);
            graphics_.reset();
        }
        window_.reset();
        if (options_.report_live_objects)
            static_cast<void>(D3D12Context::report_live_objects());
    }
    else
    {
        Log::error(
            "GPU idle could not be proven. Daedalus is intentionally retaining graphics and window resources until "
            "process exit rather than releasing objects that queued GPU work may still reference.");
        static_cast<void>(renderer_.release());
        static_cast<void>(graphics_.release());
        static_cast<void>(window_.release());
        resources_abandoned = true;
    }

    if (com_initialized_)
    {
        CoUninitialize();
        com_initialized_ = false;
    }

    if (initialized_)
    {
        Log::info(resources_abandoned ? "Project Daedalus shutdown deferred to process cleanup"
                                      : "Project Daedalus shutdown complete");
    }
    initialized_ = false;
    Log::shutdown();
}
}
