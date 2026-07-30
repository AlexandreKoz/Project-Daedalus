#pragma once

#include <filesystem>
#include <string_view>

namespace daedalus
{
enum class LogLevel
{
    info,
    warning,
    error
};

class Log final
{
public:
    static void initialize(const std::filesystem::path& log_file);
    static void shutdown() noexcept;
    static void write(LogLevel level, std::string_view message);
    static void info(std::string_view message);
    static void warning(std::string_view message);
    static void error(std::string_view message);
};
}
