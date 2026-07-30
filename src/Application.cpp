#include "Application.h"

#include "core/Log.h"
#include "core/Version.h"
#include "graphics/D3D12Context.h"
#include "graphics/TriangleRenderer.h"
#include "platform/Win32Window.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdlib>
#include <filesystem>
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
        MessageBoxA(
            nullptr,
            error.what(),
            "Project Daedalus - fatal error",
            MB_OK | MB_ICONERROR | MB_TASKMODAL);
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
        }

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

    std::ostringstream dimensions;
    dimensions << "Initial client dimensions: " << initial_width << 'x' << initial_height;
    Log::info(dimensions.str());

    window_ = std::make_unique<Win32Window>(L"Project Daedalus - Campaign A", initial_width, initial_height);
    graphics_ = std::make_unique<D3D12Context>(
        window_->native_handle(), window_->client_width(), window_->client_height(), options_.use_warp);

    const std::filesystem::path shader_dir = executable_dir / "shaders";
    renderer_ = std::make_unique<TriangleRenderer>(
        graphics_->device(), shader_dir / "TriangleVS.dxil", shader_dir / "TrianglePS.dxil");

    Log::info("Selected adapter: " + graphics_->adapter_name());
    Log::info("Feature level: " + graphics_->feature_level_name());
    Log::info("Debug layer enabled: " + boolean_text(graphics_->debug_layer_enabled()));
    Log::info("WARP in use: " + boolean_text(graphics_->using_warp()));
    Log::info("Swap-chain buffer count: " + std::to_string(D3D12Context::kFrameCount));
    initialized_ = true;
}

void Application::shutdown() noexcept
{
    renderer_.reset();
    if (graphics_ != nullptr)
    {
        graphics_->shutdown();
        graphics_.reset();
    }
    window_.reset();
    if (initialized_)
    {
        Log::info("Project Daedalus shutdown complete");
    }
    initialized_ = false;
    Log::shutdown();
}
}
