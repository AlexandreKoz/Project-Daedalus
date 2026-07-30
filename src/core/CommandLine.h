#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace daedalus
{
struct CommandLineOptions
{
    bool use_warp = false;
    bool show_help = false;
    std::optional<std::uint64_t> frame_limit;
};

class CommandLineError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] CommandLineOptions parse_command_line(std::span<const std::wstring_view> arguments);
[[nodiscard]] std::string usage_text();
}
