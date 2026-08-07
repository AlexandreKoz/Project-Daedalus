#include "core/CommandLine.h"

#include <charconv>
#include <limits>

namespace daedalus
{
namespace
{
[[nodiscard]] std::uint64_t parse_positive_integer(std::wstring_view value, std::string_view option)
{
    if (value.empty()) throw CommandLineError(std::string(option) + " requires a positive integer");
    std::string narrow;
    narrow.reserve(value.size());
    for (const wchar_t character : value)
    {
        if (character < L'0' || character > L'9') throw CommandLineError(std::string(option) + " requires a positive integer");
        narrow.push_back(static_cast<char>(character));
    }
    std::uint64_t parsed = 0;
    const auto [end, error] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), parsed);
    if (error != std::errc{} || end != narrow.data() + narrow.size() || parsed == 0)
        throw CommandLineError(std::string(option) + " requires a positive integer");
    return parsed;
}

[[nodiscard]] std::string narrow_ascii(std::wstring_view value, std::string_view option)
{
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value)
    {
        if (character < 0x20 || character > 0x7E)
            throw CommandLineError(std::string(option) + " currently requires an ASCII value");
        result.push_back(static_cast<char>(character));
    }
    return result;
}

[[nodiscard]] std::wstring_view require_value(std::span<const std::wstring_view> arguments,
                                              std::size_t& index,
                                              std::string_view option)
{
    if (index + 1 >= arguments.size()) throw CommandLineError(std::string(option) + " requires a value");
    return arguments[++index];
}
}

CommandLineOptions parse_command_line(std::span<const std::wstring_view> arguments)
{
    CommandLineOptions options;
    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        const std::wstring_view argument = arguments[index];
        if (argument == L"--warp") options.use_warp = true;
        else if (argument == L"--help" || argument == L"-h" || argument == L"/?") options.show_help = true;
        else if (argument == L"--dump-scene") options.dump_scene = true;
        else if (argument == L"--stress-resize") options.stress_resize = true;
        else if (argument == L"--report-live-objects") options.report_live_objects = true;
        else if (argument == L"--stress-reloads")
        {
            if (options.stress_reload_count.has_value()) throw CommandLineError("--stress-reloads may be specified only once");
            options.stress_reload_count = parse_positive_integer(require_value(arguments, index, "--stress-reloads"), "--stress-reloads");
        }
        else if (argument == L"--frames")
        {
            if (options.frame_limit.has_value()) throw CommandLineError("--frames may be specified only once");
            options.frame_limit = parse_positive_integer(require_value(arguments, index, "--frames"), "--frames");
        }
        else if (argument == L"--asset")
        {
            if (options.asset_path.has_value()) throw CommandLineError("--asset may be specified only once");
            options.asset_path = std::filesystem::path(require_value(arguments, index, "--asset"));
        }
        else if (argument == L"--scene")
        {
            if (options.scene_selector.has_value()) throw CommandLineError("--scene may be specified only once");
            options.scene_selector = narrow_ascii(require_value(arguments, index, "--scene"), "--scene");
        }
        else if (argument == L"--import-report")
        {
            if (options.import_report_path.has_value()) throw CommandLineError("--import-report may be specified only once");
            options.import_report_path = std::filesystem::path(require_value(arguments, index, "--import-report"));
        }
        else if (argument == L"--stress-alternate-asset")
        {
            if (options.stress_alternate_asset_path.has_value()) throw CommandLineError("--stress-alternate-asset may be specified only once");
            options.stress_alternate_asset_path = std::filesystem::path(require_value(arguments, index, "--stress-alternate-asset"));
        }
        else if (argument == L"--diagnostic")
        {
            const std::string value = narrow_ascii(require_value(arguments, index, "--diagnostic"), "--diagnostic");
            if (value == "shaded") options.diagnostic_mode = DiagnosticMode::shaded;
            else if (value == "normals") options.diagnostic_mode = DiagnosticMode::normals;
            else if (value == "uv") options.diagnostic_mode = DiagnosticMode::uv;
            else if (value == "tangents") options.diagnostic_mode = DiagnosticMode::tangents;
            else if (value == "bounds") options.diagnostic_mode = DiagnosticMode::bounds;
            else throw CommandLineError("--diagnostic must be shaded, normals, uv, tangents, or bounds");
        }
        else
        {
            throw CommandLineError("unknown argument: " + narrow_ascii(argument, "argument"));
        }
    }
    if (options.scene_selector.has_value() && !options.asset_path.has_value())
        throw CommandLineError("--scene requires --asset");
    if (options.import_report_path.has_value() && !options.asset_path.has_value())
        throw CommandLineError("--import-report requires --asset");
    if (options.stress_alternate_asset_path.has_value() && !options.asset_path.has_value())
        throw CommandLineError("--stress-alternate-asset requires --asset");
    if (options.stress_alternate_asset_path.has_value() && !options.stress_reload_count.has_value())
        throw CommandLineError("--stress-alternate-asset requires --stress-reloads");
    return options;
}

std::string usage_text()
{
    return
        "Project Daedalus 0.1.0\n"
        "Usage: Daedalus.exe [options]\n\n"
        "Options:\n"
        "  --asset <path>                 Load a supported .gltf or .glb asset.\n"
        "  --scene <index-or-name>        Select a source scene.\n"
        "  --dump-scene                   Print the canonical scene hierarchy.\n"
        "  --import-report <path>         Write the deterministic JSON import report.\n"
        "  --diagnostic <mode>            shaded, normals, uv, tangents, or bounds.\n"
        "  --warp                         Select the Microsoft WARP software adapter.\n"
        "  --frames <count>               Exit cleanly after presenting count frames.\n"
        "  --stress-reloads <count>       Recreate scene resources count times.\n"
        "  --stress-alternate-asset <p>   Alternate assets during reload stress.\n"
        "  --stress-resize                Exercise deterministic resize/window states.\n"
        "  --report-live-objects          Report D3D12/DXGI live objects after teardown.\n"
        "  --help, -h, /?                 Show this help without initializing Direct3D 12.\n";
}

std::string_view to_string(DiagnosticMode mode) noexcept
{
    switch (mode)
    {
    case DiagnosticMode::shaded: return "shaded";
    case DiagnosticMode::normals: return "normals";
    case DiagnosticMode::uv: return "uv";
    case DiagnosticMode::tangents: return "tangents";
    case DiagnosticMode::bounds: return "bounds";
    }
    return "shaded";
}
}
