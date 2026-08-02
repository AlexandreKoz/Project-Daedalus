#include "Application.h"

#include "assets/GltfImporter.h"
#include "core/Log.h"
#include "core/Version.h"
#include "graphics/D3D12Context.h"
#include "graphics/DiagnosticSceneRenderer.h"
#include "platform/Win32Window.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <objbase.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
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

Application::Application(CommandLineOptions options) : options_(std::move(options))
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
        MessageBoxA(nullptr, error.what(), "Project Daedalus - fatal error", MB_OK | MB_ICONERROR | MB_TASKMODAL);
        application.shutdown();
        return EXIT_FAILURE;
    }
    catch (...)
    {
        constexpr const char* message = "unknown fatal error";
        Log::error(message);
        MessageBoxA(nullptr, message, "Project Daedalus - fatal error", MB_OK | MB_ICONERROR | MB_TASKMODAL);
        application.shutdown();
        return EXIT_FAILURE;
    }
}

int Application::run()
{
    std::uint64_t presented_frames = 0;
    window_->show(SW_SHOWDEFAULT);

    while (window_->process_messages())
    {
        if (const auto resized = window_->consume_resize(); resized.has_value())
        {
            graphics_->resize(resized->first, resized->second);
            renderer_->resize(resized->first, resized->second);
        }

        if (window_->consume_reload_request())
        {
            reload_scene();
        }
        renderer_->apply_input(window_->consume_orbit_input());

        if (window_->minimized())
        {
            Sleep(16);
            continue;
        }

        const FrameRecordingContext frame = graphics_->begin_frame();
        renderer_->record(frame);
        graphics_->end_frame();
        ++presented_frames;

        if (options_.frame_limit.has_value() && presented_frames >= *options_.frame_limit)
        {
            Log::info("Requested frame limit reached; beginning clean shutdown");
            break;
        }
    }

    graphics_->wait_for_gpu();
    std::ostringstream stream;
    stream << "Application exiting cleanly after " << presented_frames << " presented frames";
    Log::info(stream.str());
    shutdown();
    return EXIT_SUCCESS;
}

void Application::load_scene()
{
    if (!options_.asset_path.has_value())
    {
        scene_ = make_builtin_triangle_scene();
        import_report_ = make_import_report(scene_, ImportStatus::success, {}, "builtin-diagnostic-scene");
        Log::info("No --asset was supplied; using the canonical built-in diagnostic triangle");
        if (options_.dump_scene)
        {
            const std::string hierarchy = dump_scene_hierarchy(scene_);
            std::cout << hierarchy;
            Log::info("Canonical scene hierarchy:\n" + hierarchy);
        }
        write_import_report();
        return;
    }

    ImportSettings settings;
    settings.scene_selector = options_.scene_selector;
    ImportResult result = GltfImporter{}.import_file(*options_.asset_path, settings);
    import_report_ = result.report;
    log_import_report(import_report_);
    if (!result.succeeded())
    {
        write_import_report();
        throw std::runtime_error("asset import failed with status " + std::string(to_string(import_report_.status)));
    }
    scene_ = std::move(result.scene);

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
    renderer_ = std::make_unique<DiagnosticSceneRenderer>(
        *graphics_, scene_, options_.diagnostic_mode,
        shader_directory_ / "DiagnosticVS.dxil", shader_directory_ / "DiagnosticPS.dxil");
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
    const std::filesystem::path runtime_dir = executable_dir / "runtime";
    Log::initialize(runtime_dir / "log" / "Daedalus.log");

    Log::info("Project Daedalus version " + std::string(kVersion));
    Log::info("Build type: " DAEDALUS_BUILD_TYPE);
    Log::info(std::string("Process architecture: ") + (sizeof(void*) == 8 ? "x64" : "non-x64"));
    Log::info("Diagnostic mode: " + std::string(to_string(options_.diagnostic_mode)));

    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com_result)) throw std::runtime_error("CoInitializeEx failed for WIC image decoding");
    com_initialized_ = true;

    load_scene();

    std::ostringstream dimensions;
    dimensions << "Initial client dimensions: " << initial_width << 'x' << initial_height;
    Log::info(dimensions.str());

    window_ = std::make_unique<Win32Window>(L"Project Daedalus - Campaign B Asset Viewer", initial_width, initial_height);
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
    const bool gpu_idle = graphics_ == nullptr || graphics_->prepare_for_shutdown();
    bool resources_abandoned = false;

    if (gpu_idle)
    {
        renderer_.reset();
        if (graphics_ != nullptr)
        {
            graphics_->shutdown();
            graphics_.reset();
        }
        window_.reset();
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
