#include "core/CommandLine.h"

#include <charconv>
#include <limits>
#include <stdexcept>

namespace daedalus
{
namespace
{
[[nodiscard]] std::uint64_t parse_positive_integer(std::wstring_view value)
{
    if (value.empty())
    {
        throw CommandLineError("--frames requires a positive integer");
    }

    std::string narrow;
    narrow.reserve(value.size());
    for (const wchar_t character : value)
    {
        if (character < L'0' || character > L'9')
        {
            throw CommandLineError("--frames requires a positive integer");
        }
        narrow.push_back(static_cast<char>(character));
    }

    std::uint64_t parsed = 0;
    const auto [end, error] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), parsed);
    if (error != std::errc{} || end != narrow.data() + narrow.size() || parsed == 0)
    {
        throw CommandLineError("--frames requires a positive integer");
    }
    return parsed;
}
}

CommandLineOptions parse_command_line(std::span<const std::wstring_view> arguments)
{
    CommandLineOptions options;

    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        const std::wstring_view argument = arguments[index];
        if (argument == L"--warp")
        {
            options.use_warp = true;
        }
        else if (argument == L"--help" || argument == L"-h" || argument == L"/?")
        {
            options.show_help = true;
        }
        else if (argument == L"--frames")
        {
            if (index + 1 >= arguments.size())
            {
                throw CommandLineError("--frames requires a value");
            }
            if (options.frame_limit.has_value())
            {
                throw CommandLineError("--frames may be specified only once");
            }
            options.frame_limit = parse_positive_integer(arguments[++index]);
        }
        else
        {
            std::string printable;
            printable.reserve(argument.size());
            for (const wchar_t character : argument)
            {
                printable.push_back(character >= 32 && character <= 126 ? static_cast<char>(character) : '?');
            }
            throw CommandLineError("unknown argument: " + printable);
        }
    }

    return options;
}

std::string usage_text()
{
    return
        "Project Daedalus 0.0.1\n"
        "Usage: Daedalus.exe [options]\n\n"
        "Options:\n"
        "  --warp              Select the Microsoft WARP software adapter.\n"
        "  --frames <count>    Exit cleanly after presenting count frames.\n"
        "  --help, -h, /?      Show this help without initializing Direct3D 12.\n";
}
}
