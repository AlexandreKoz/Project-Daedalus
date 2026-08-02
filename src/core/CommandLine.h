#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace daedalus
{
enum class DiagnosticMode
{
    shaded,
    normals,
    uv,
    bounds
};

struct CommandLineOptions
{
    bool use_warp = false;
    bool show_help = false;
    bool dump_scene = false;
    std::optional<std::uint64_t> frame_limit;
    std::optional<std::filesystem::path> asset_path;
    std::optional<std::string> scene_selector;
    std::optional<std::filesystem::path> import_report_path;
    DiagnosticMode diagnostic_mode = DiagnosticMode::shaded;
};

class CommandLineError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] CommandLineOptions parse_command_line(std::span<const std::wstring_view> arguments);
[[nodiscard]] std::string usage_text();
[[nodiscard]] std::string_view to_string(DiagnosticMode mode) noexcept;
}
