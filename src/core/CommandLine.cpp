#include "core/CommandLine.h"

#include <charconv>
#include <cmath>
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

[[nodiscard]] float parse_exposure(std::wstring_view value)
{
    const std::string narrow = narrow_ascii(value, "--exposure");
    float parsed = 0.0F;
    const auto [end, error] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), parsed);
    if (error != std::errc{} || end != narrow.data() + narrow.size() || !std::isfinite(parsed) || parsed < -24.0F || parsed > 24.0F)
        throw CommandLineError("--exposure requires a finite EV value in [-24, +24]");
    return parsed;
}

[[nodiscard]] float parse_shadow_bias(std::wstring_view value, std::string_view option)
{
    const std::string narrow = narrow_ascii(value, option);
    float parsed = 0.0F;
    const auto [end, error] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), parsed);
    if (error != std::errc{} || end != narrow.data() + narrow.size() || !std::isfinite(parsed) || parsed < 0.0F || parsed > 0.05F)
        throw CommandLineError(std::string(option) + " requires a finite value in [0, 0.05]");
    return parsed;
}

[[nodiscard]] float parse_environment_intensity(std::wstring_view value)
{
    const std::string narrow = narrow_ascii(value, "--environment-intensity");
    float parsed = 0.0F;
    const auto [end, error] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), parsed);
    if (error != std::errc{} || end != narrow.data() + narrow.size() || !std::isfinite(parsed) || parsed < 0.0F || parsed > 64.0F)
        throw CommandLineError("--environment-intensity requires a finite value in [0, 64]");
    return parsed;
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
        else if (argument == L"--no-error-dialog") options.suppress_error_dialog = true;
        else if (argument == L"--no-shadows") options.shadows_enabled = false;
        else if (argument == L"--validate-hdr") options.validate_hdr = true;
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
        else if (argument == L"--capture")
        {
            if (options.capture_path.has_value()) throw CommandLineError("--capture may be specified only once");
            options.capture_path = std::filesystem::path(require_value(arguments, index, "--capture"));
        }
        else if (argument == L"--capture-metadata")
        {
            if (options.capture_metadata_path.has_value()) throw CommandLineError("--capture-metadata may be specified only once");
            options.capture_metadata_path = std::filesystem::path(require_value(arguments, index, "--capture-metadata"));
        }
        else if (argument == L"--capture-frame")
        {
            if (options.capture_frame.has_value()) throw CommandLineError("--capture-frame may be specified only once");
            options.capture_frame = parse_positive_integer(require_value(arguments, index, "--capture-frame"), "--capture-frame");
        }
        else if (argument == L"--benchmark-output")
        {
            if (options.benchmark_output_path.has_value()) throw CommandLineError("--benchmark-output may be specified only once");
            options.benchmark_output_path = std::filesystem::path(require_value(arguments, index, "--benchmark-output"));
        }
        else if (argument == L"--benchmark-warmup")
        {
            if (options.benchmark_warmup_frames.has_value()) throw CommandLineError("--benchmark-warmup may be specified only once");
            options.benchmark_warmup_frames = parse_positive_integer(require_value(arguments, index, "--benchmark-warmup"), "--benchmark-warmup");
        }
        else if (argument == L"--environment-intensity")
        {
            options.environment_intensity = parse_environment_intensity(require_value(arguments, index, "--environment-intensity"));
        }
        else if (argument == L"--shadow-bias")
        {
            options.shadow_constant_bias = parse_shadow_bias(require_value(arguments, index, "--shadow-bias"), "--shadow-bias");
        }
        else if (argument == L"--shadow-normal-bias")
        {
            options.shadow_normal_bias = parse_shadow_bias(require_value(arguments, index, "--shadow-normal-bias"), "--shadow-normal-bias");
        }
        else if (argument == L"--exposure")
        {
            options.exposure_ev = parse_exposure(require_value(arguments, index, "--exposure"));
        }
        else if (argument == L"--diagnostic")
        {
            const std::string value = narrow_ascii(require_value(arguments, index, "--diagnostic"), "--diagnostic");
            if (value == "shaded") options.diagnostic_mode = DiagnosticMode::shaded;
            else if (value == "normals") options.diagnostic_mode = DiagnosticMode::normals;
            else if (value == "uv") options.diagnostic_mode = DiagnosticMode::uv;
            else if (value == "tangents") options.diagnostic_mode = DiagnosticMode::tangents;
            else if (value == "bounds") options.diagnostic_mode = DiagnosticMode::bounds;
            else if (value == "base-color") options.diagnostic_mode = DiagnosticMode::base_color;
            else if (value == "metallic") options.diagnostic_mode = DiagnosticMode::metallic;
            else if (value == "roughness") options.diagnostic_mode = DiagnosticMode::roughness;
            else if (value == "emissive") options.diagnostic_mode = DiagnosticMode::emissive;
            else if (value == "material-id") options.diagnostic_mode = DiagnosticMode::material_id;
            else if (value == "depth") options.diagnostic_mode = DiagnosticMode::depth;
            else if (value == "overdraw") options.diagnostic_mode = DiagnosticMode::overdraw;
            else throw CommandLineError("--diagnostic must be shaded, normals, uv, tangents, bounds, base-color, metallic, roughness, emissive, material-id, depth, or overdraw");
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
    if (options.capture_metadata_path.has_value() && !options.capture_path.has_value() && !options.validate_hdr)
        throw CommandLineError("--capture-metadata requires --capture or --validate-hdr");
    if (options.capture_frame.has_value() && !options.capture_path.has_value() && !options.validate_hdr)
        throw CommandLineError("--capture-frame requires --capture or --validate-hdr");
    if (options.benchmark_output_path.has_value() && !options.frame_limit.has_value())
        throw CommandLineError("--benchmark-output requires --frames for a deterministic fixed-frame run");
    if (options.benchmark_output_path.has_value() && options.benchmark_warmup_frames.has_value() &&
        *options.benchmark_warmup_frames >= *options.frame_limit)
        throw CommandLineError("--benchmark-warmup must be smaller than --frames");
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
        "  --diagnostic <mode>            shaded, normals, uv, tangents, bounds, base-color, metallic,\n"
        "                                 roughness, emissive, material-id, depth, or overdraw.\n"
        "  --exposure <ev>                Tone-map exposure in EV, range [-24, +24], default 0.\n"
        "  --environment-intensity <v>     Procedural IBL intensity, range [0, 64], default 1.\n"
        "  --no-shadows                    Disable Campaign C shadow-map evaluation.\n"
        "  --shadow-bias <v>               Shadow constant depth bias [0, 0.05], default 0.001.\n"
        "  --shadow-normal-bias <v>        Shadow normal-dependent bias [0, 0.05], default 0.002.\n"
        "  --capture <png>                 Capture a deterministic presented frame to PNG.\n"
        "  --capture-frame <n>             1-based frame to capture/validate, default 1.\n"
        "  --capture-metadata <json>       Write machine-readable capture metadata.\n"
        "  --validate-hdr                  Read back HDR scene colour and fail on NaN/Inf.\n"
        "  --benchmark-output <json>       Write fixed-frame CPU/GPU/resource metrics JSON.\n"
        "  --benchmark-warmup <n>          Frames excluded from benchmark aggregation.\n"
        "  --warp                         Select the Microsoft WARP software adapter.\n"
        "  --frames <count>               Exit cleanly after presenting count frames.\n"
        "  --stress-reloads <count>       Recreate scene resources count times.\n"
        "  --stress-alternate-asset <p>   Alternate assets during reload stress.\n"
        "  --stress-resize                Exercise deterministic resize/window states.\n"
        "  --report-live-objects          Report D3D12/DXGI live objects after teardown.\n"
        "  --no-error-dialog              Never show a modal fatal-error dialog (automation-safe).\n"
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
    case DiagnosticMode::base_color: return "base-color";
    case DiagnosticMode::metallic: return "metallic";
    case DiagnosticMode::roughness: return "roughness";
    case DiagnosticMode::emissive: return "emissive";
    case DiagnosticMode::material_id: return "material-id";
    case DiagnosticMode::depth: return "depth";
    case DiagnosticMode::overdraw: return "overdraw";
    }
    return "shaded";
}
}
