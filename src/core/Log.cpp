#include "core/Log.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace daedalus
{
namespace
{
std::mutex g_mutex;
std::ofstream g_file;

[[nodiscard]] const char* level_name(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::info:
        return "INFO";
    case LogLevel::warning:
        return "WARN";
    case LogLevel::error:
        return "ERROR";
    }
    return "UNKNOWN";
}

[[nodiscard]] std::string timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::ostringstream stream;
    stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}
}

void Log::initialize(const std::filesystem::path& log_file)
{
    std::scoped_lock lock(g_mutex);
    std::filesystem::create_directories(log_file.parent_path());
    g_file.open(log_file, std::ios::out | std::ios::trunc);
    if (!g_file)
    {
        throw std::runtime_error("unable to open log file: " + log_file.string());
    }
}

void Log::shutdown() noexcept
{
    std::scoped_lock lock(g_mutex);
    if (g_file.is_open())
    {
        g_file.flush();
        g_file.close();
    }
}

void Log::write(LogLevel level, std::string_view message)
{
    std::scoped_lock lock(g_mutex);
    const std::string line = '[' + timestamp() + "] [" + level_name(level) + "] " + std::string(message);

    std::ostream& console = level == LogLevel::error ? std::cerr : std::cout;
    console << line << '\n';
    console.flush();

    if (g_file.is_open())
    {
        g_file << line << '\n';
        g_file.flush();
    }

#ifdef _WIN32
    const std::string debugger_line = line + '\n';
    OutputDebugStringA(debugger_line.c_str());
#endif
}

void Log::info(std::string_view message)
{
    write(LogLevel::info, message);
}

void Log::warning(std::string_view message)
{
    write(LogLevel::warning, message);
}

void Log::error(std::string_view message)
{
    write(LogLevel::error, message);
}
}
