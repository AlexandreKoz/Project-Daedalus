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

[[nodiscard]] std::uint64_t parse_unsigned(std::wstring_view value, std::string_view option, std::uint64_t maximum)
{
    if (value.empty()) throw CommandLineError(std::string(option) + " requires a non-negative integer");
    std::string narrow;
    narrow.reserve(value.size());
    for (const wchar_t character : value)
    {
        if (character < L'0' || character > L'9') throw CommandLineError(std::string(option) + " requires a non-negative integer");
        narrow.push_back(static_cast<char>(character));
    }
    std::uint64_t parsed = 0;
    const auto [end, error] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), parsed);
    if (error != std::errc{} || end != narrow.data() + narrow.size() || parsed > maximum)
        throw CommandLineError(std::string(option) + " requires an integer in [0, " + std::to_string(maximum) + "]");
    return parsed;
}

[[nodiscard]] float parse_float_in_range(std::wstring_view value, std::string_view option, float minimum, float maximum)
{
    const std::string narrow = narrow_ascii(value, option);
    float parsed = 0.0F;
    const auto [end, error] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), parsed);
    if (error != std::errc{} || end != narrow.data() + narrow.size() || !std::isfinite(parsed) || parsed < minimum || parsed > maximum)
        throw CommandLineError(std::string(option) + " requires a finite value in [" + std::to_string(minimum) + ", " +
                               std::to_string(maximum) + "]");
    return parsed;
}

[[nodiscard]] bool parse_on_off(std::wstring_view value, std::string_view option)
{
    const std::string narrow = narrow_ascii(value, option);
    if (narrow == "on") return true;
    if (narrow == "off") return false;
    throw CommandLineError(std::string(option) + " requires on or off");
}

template <typename Parsed>
[[nodiscard]] Parsed require_parsed(std::optional<Parsed> parsed, std::string_view option, std::string_view accepted)
{
    if (!parsed.has_value()) throw CommandLineError(std::string(option) + " must be one of: " + std::string(accepted));
    return *parsed;
}

[[nodiscard]] std::vector<std::string> split_colon(const std::string& value)
{
    std::vector<std::string> parts;
    std::size_t begin = 0;
    for (;;)
    {
        const std::size_t colon = value.find(':', begin);
        parts.push_back(value.substr(begin, colon == std::string::npos ? std::string::npos : colon - begin));
        if (colon == std::string::npos) break;
        begin = colon + 1U;
    }
    return parts;
}

[[nodiscard]] float parse_float_text(const std::string& text, std::string_view option)
{
    float parsed = 0.0F;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(parsed))
        throw CommandLineError(std::string(option) + " contains a non-numeric component: " + text);
    return parsed;
}

[[nodiscard]] std::uint64_t parse_uint_text(const std::string& text, std::string_view option)
{
    std::uint64_t parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size())
        throw CommandLineError(std::string(option) + " contains a non-integer component: " + text);
    return parsed;
}

[[nodiscard]] CameraScript parse_camera_path(std::wstring_view value)
{
    const std::vector<std::string> parts = split_colon(narrow_ascii(value, "--camera-path"));
    CameraScript script;
    if (parts.size() == 1U && parts[0] == "static") return script;
    if (parts.size() == 2U && parts[0] == "orbit")
    {
        script.kind = CameraScriptKind::orbit;
        script.degrees_per_frame = parse_float_text(parts[1], "--camera-path");
        if (std::abs(script.degrees_per_frame) > 45.0F)
            throw CommandLineError("--camera-path orbit rate must be within [-45, 45] degrees per frame");
        return script;
    }
    if (parts.size() == 2U && parts[0] == "teleport")
    {
        script.kind = CameraScriptKind::teleport;
        script.teleport_frame = parse_uint_text(parts[1], "--camera-path");
        if (script.teleport_frame == 0U) throw CommandLineError("--camera-path teleport frame must be positive");
        return script;
    }
    throw CommandLineError("--camera-path must be static, orbit:<degrees-per-frame>, or teleport:<frame>");
}

[[nodiscard]] NodeAnimation parse_node_animation(std::wstring_view value)
{
    const std::vector<std::string> parts = split_colon(narrow_ascii(value, "--animate-node"));
    if (parts.size() != 3U)
        throw CommandLineError("--animate-node requires <node-index>:<rotate-y|translate-x|bob-y>:<rate>");
    NodeAnimation animation;
    const std::uint64_t node = parse_uint_text(parts[0], "--animate-node");
    if (node > 0xFFFFFFFEULL) throw CommandLineError("--animate-node node index is out of range");
    animation.node = static_cast<std::uint32_t>(node);
    if (parts[1] == "rotate-y") animation.kind = NodeAnimationKind::rotate_y;
    else if (parts[1] == "translate-x") animation.kind = NodeAnimationKind::translate_x;
    else if (parts[1] == "bob-y") animation.kind = NodeAnimationKind::bob_y;
    else throw CommandLineError("--animate-node kind must be rotate-y, translate-x, or bob-y");
    animation.rate = parse_float_text(parts[2], "--animate-node");
    if (std::abs(animation.rate) > 10.0F) throw CommandLineError("--animate-node rate must be within [-10, 10]");
    return animation;
}
}

CommandLineOptions parse_command_line(std::span<const std::wstring_view> arguments)
{
    CommandLineOptions options;
    bool pt_preset_explicit = false;
    bool pt_clamp_explicit = false;
    bool pt_denoise_explicit = false;
    bool dlss_quality_explicit = false;
    bool render_scale_explicit = false;
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
        else if (argument == L"--renderer")
        {
            options.renderer.mode = require_parsed(parse_renderer_mode(narrow_ascii(require_value(arguments, index, "--renderer"), "--renderer")),
                                                   "--renderer", "raster, hybrid, pathtrace");
        }
        else if (argument == L"--rt-shadows") options.renderer.hybrid.shadows = parse_on_off(require_value(arguments, index, "--rt-shadows"), "--rt-shadows");
        else if (argument == L"--rt-reflections") options.renderer.hybrid.reflections = parse_on_off(require_value(arguments, index, "--rt-reflections"), "--rt-reflections");
        else if (argument == L"--rt-denoise") options.renderer.hybrid.denoise_reflections = parse_on_off(require_value(arguments, index, "--rt-denoise"), "--rt-denoise");
        else if (argument == L"--rt-reflection-max-roughness")
            options.renderer.hybrid.reflection_max_roughness = parse_float_in_range(require_value(arguments, index, "--rt-reflection-max-roughness"), "--rt-reflection-max-roughness", 0.0F, 1.0F);
        else if (argument == L"--rt-debug")
            options.renderer.hybrid.debug_view = require_parsed(parse_hybrid_debug_view(narrow_ascii(require_value(arguments, index, "--rt-debug"), "--rt-debug")),
                "--rt-debug", "none, shadow-visibility, reflection, reflection-hit-distance, direct-lighting, instance-id");
        else if (argument == L"--pt-preset")
        {
            options.renderer.path_trace.preset = require_parsed(parse_path_trace_preset(narrow_ascii(require_value(arguments, index, "--pt-preset"), "--pt-preset")),
                                                                "--pt-preset", "reference, interactive");
            pt_preset_explicit = true;
        }
        else if (argument == L"--pt-spp")
            options.renderer.path_trace.samples_per_frame = static_cast<std::uint32_t>(parse_positive_integer(require_value(arguments, index, "--pt-spp"), "--pt-spp"));
        else if (argument == L"--pt-max-depth")
            options.renderer.path_trace.max_depth = static_cast<std::uint32_t>(parse_unsigned(require_value(arguments, index, "--pt-max-depth"), "--pt-max-depth", 64U));
        else if (argument == L"--pt-rr-depth")
            options.renderer.path_trace.min_russian_roulette_depth = static_cast<std::uint32_t>(parse_unsigned(require_value(arguments, index, "--pt-rr-depth"), "--pt-rr-depth", 64U));
        else if (argument == L"--pt-seed")
            options.renderer.path_trace.seed = static_cast<std::uint32_t>(parse_unsigned(require_value(arguments, index, "--pt-seed"), "--pt-seed", 0xFFFFFFFFULL));
        else if (argument == L"--pt-target-samples")
            options.renderer.path_trace.target_samples = parse_positive_integer(require_value(arguments, index, "--pt-target-samples"), "--pt-target-samples");
        else if (argument == L"--pt-clamp")
        {
            options.renderer.path_trace.firefly_clamp = parse_float_in_range(require_value(arguments, index, "--pt-clamp"), "--pt-clamp", 0.0F, 1.0e6F);
            pt_clamp_explicit = true;
        }
        else if (argument == L"--pt-denoise")
        {
            options.renderer.path_trace.denoise = parse_on_off(require_value(arguments, index, "--pt-denoise"), "--pt-denoise");
            pt_denoise_explicit = true;
        }
        else if (argument == L"--pt-debug")
            options.renderer.path_trace.debug_view = require_parsed(parse_path_trace_debug_view(narrow_ascii(require_value(arguments, index, "--pt-debug"), "--pt-debug")),
                "--pt-debug", "none, sample-count, variance, direct, indirect, path-length, material-id, nonfinite");
        else if (argument == L"--atrous-iterations")
            options.renderer.temporal.atrous_iterations = static_cast<std::uint32_t>(parse_unsigned(require_value(arguments, index, "--atrous-iterations"), "--atrous-iterations", 5U));
        else if (argument == L"--history-length")
            options.renderer.temporal.max_history_length = static_cast<std::uint32_t>(parse_positive_integer(require_value(arguments, index, "--history-length"), "--history-length"));
        else if (argument == L"--history-clip")
            options.renderer.temporal.history_clip_gamma = parse_float_in_range(require_value(arguments, index, "--history-clip"), "--history-clip", 0.0F, 16.0F);
        else if (argument == L"--jitter") options.renderer.temporal.jitter = true;
        else if (argument == L"--temporal-debug")
            options.renderer.temporal.debug_view = require_parsed(parse_temporal_debug_view(narrow_ascii(require_value(arguments, index, "--temporal-debug"), "--temporal-debug")),
                "--temporal-debug", "none, motion, depth, normals, disocclusion, history-length, variance, reactive, raw-signal");
        else if (argument == L"--upscaler")
            options.renderer.upscaler.mode = require_parsed(parse_upscaler_mode(narrow_ascii(require_value(arguments, index, "--upscaler"), "--upscaler")),
                                                            "--upscaler", "none, bilinear, dlss");
        else if (argument == L"--dlss-quality")
        {
            options.renderer.upscaler.dlss_quality = require_parsed(parse_dlss_quality(narrow_ascii(require_value(arguments, index, "--dlss-quality"), "--dlss-quality")),
                "--dlss-quality", "dlaa, quality, balanced, performance, ultra-performance");
            dlss_quality_explicit = true;
        }
        else if (argument == L"--render-scale")
        {
            options.renderer.upscaler.render_scale = parse_float_in_range(require_value(arguments, index, "--render-scale"), "--render-scale", 0.25F, 1.0F);
            render_scale_explicit = true;
        }
        else if (argument == L"--camera-path") options.renderer.camera_script = parse_camera_path(require_value(arguments, index, "--camera-path"));
        else if (argument == L"--animate-node") options.renderer.node_animations.push_back(parse_node_animation(require_value(arguments, index, "--animate-node")));
        else if (argument == L"--animate-emissive")
        {
            const std::uint64_t period = parse_positive_integer(require_value(arguments, index, "--animate-emissive"), "--animate-emissive");
            if (period < 2U || period > 100000U) throw CommandLineError("--animate-emissive period must be in [2, 100000] frames");
            options.renderer.emissive_pulse_period_frames = static_cast<std::uint32_t>(period);
        }
        else if (argument == L"--builtin-scene")
        {
            if (options.builtin_scene.has_value()) throw CommandLineError("--builtin-scene may be specified only once");
            const std::string value = narrow_ascii(require_value(arguments, index, "--builtin-scene"), "--builtin-scene");
            if (value != "triangle" && value != "cornell-box" && value != "alpha-instancing")
                throw CommandLineError("--builtin-scene must be triangle, cornell-box, or alpha-instancing");
            options.builtin_scene = value;
        }
        else if (argument == L"--capture-samples")
        {
            if (options.capture_samples.has_value()) throw CommandLineError("--capture-samples may be specified only once");
            options.capture_samples = parse_positive_integer(require_value(arguments, index, "--capture-samples"), "--capture-samples");
        }
        else if (argument == L"--capture-hdr")
        {
            if (options.capture_hdr_path.has_value()) throw CommandLineError("--capture-hdr may be specified only once");
            options.capture_hdr_path = std::filesystem::path(require_value(arguments, index, "--capture-hdr"));
        }
        else if (argument == L"--asset-cache")
        {
            if (options.asset_cache_directory.has_value()) throw CommandLineError("--asset-cache may be specified only once");
            options.asset_cache_directory = std::filesystem::path(require_value(arguments, index, "--asset-cache"));
        }
        else if (argument == L"--texture-search-root")
            options.texture_search_roots.emplace_back(require_value(arguments, index, "--texture-search-root"));
        else if (argument == L"--blender")
        {
            if (options.blender_executable.has_value()) throw CommandLineError("--blender may be specified only once");
            options.blender_executable = std::filesystem::path(require_value(arguments, index, "--blender"));
        }
        else if (argument == L"--blender-timeout")
        {
            const std::uint64_t seconds = parse_positive_integer(require_value(arguments, index, "--blender-timeout"), "--blender-timeout");
            if (seconds > 86400U) throw CommandLineError("--blender-timeout must not exceed 86400 seconds");
            options.blender_timeout_seconds = static_cast<std::uint32_t>(seconds);
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
    if (options.capture_metadata_path.has_value() && !options.capture_path.has_value() && !options.validate_hdr &&
        !options.capture_hdr_path.has_value())
        throw CommandLineError("--capture-metadata requires --capture, --capture-hdr, or --validate-hdr");
    if (options.capture_frame.has_value() && !options.capture_path.has_value() && !options.validate_hdr &&
        !options.capture_hdr_path.has_value())
        throw CommandLineError("--capture-frame requires --capture, --capture-hdr, or --validate-hdr");
    if (options.builtin_scene.has_value() && options.asset_path.has_value())
        throw CommandLineError("--builtin-scene and --asset are mutually exclusive");
    if (options.builtin_scene.has_value() && options.stress_reload_count.has_value() && options.stress_alternate_asset_path.has_value())
        throw CommandLineError("--stress-alternate-asset cannot be combined with --builtin-scene");

    RendererSettings& renderer = options.renderer;
    if (options.diagnostic_mode != DiagnosticMode::shaded && renderer.mode != RendererMode::raster)
        throw CommandLineError("--diagnostic modes other than shaded are raster-path diagnostics; use --rt-debug, --pt-debug, or --temporal-debug with --renderer hybrid/pathtrace");
    PathTraceSettings& pt = renderer.path_trace;
    if (pt.preset == PathTracePreset::interactive)
    {
        if (!pt_denoise_explicit) pt.denoise = true;
        if (!pt_clamp_explicit) pt.firefly_clamp = 16.0F;
    }
    else
    {
        if (pt.denoise) throw CommandLineError("--pt-denoise on is not permitted with the reference preset; use --pt-preset interactive");
        if (pt.firefly_clamp != 0.0F) throw CommandLineError("--pt-clamp is biased and is not permitted with the reference preset; use --pt-preset interactive");
    }
    static_cast<void>(pt_preset_explicit);
    if (pt.min_russian_roulette_depth > pt.max_depth)
        throw CommandLineError("--pt-rr-depth must not exceed --pt-max-depth");
    if (pt.max_depth == 0U) throw CommandLineError("--pt-max-depth must be at least 1");
    if (pt.samples_per_frame > 64U) throw CommandLineError("--pt-spp must not exceed 64 samples per dispatch");
    if (options.capture_samples.has_value())
    {
        if (renderer.mode != RendererMode::pathtrace)
            throw CommandLineError("--capture-samples requires --renderer pathtrace");
        if (options.capture_frame.has_value())
            throw CommandLineError("--capture-samples and --capture-frame are mutually exclusive");
        if (!options.capture_path.has_value() && !options.validate_hdr && !options.capture_hdr_path.has_value())
            throw CommandLineError("--capture-samples requires --capture, --capture-hdr, or --validate-hdr");
        if (pt.denoise)
            throw CommandLineError("--capture-samples captures the progressive accumulation; it cannot be combined with the denoised interactive display");
        if (!pt.target_samples.has_value()) pt.target_samples = options.capture_samples;
        if (*pt.target_samples < *options.capture_samples)
            throw CommandLineError("--pt-target-samples must be at least --capture-samples");
    }
    if (renderer.upscaler.mode == UpscalerMode::dlss && render_scale_explicit)
        throw CommandLineError("--render-scale is chosen by the DLSS SDK quality mode and cannot be combined with --upscaler dlss");
    if (dlss_quality_explicit && renderer.upscaler.mode != UpscalerMode::dlss)
        throw CommandLineError("--dlss-quality requires --upscaler dlss");
    if (render_scale_explicit && renderer.upscaler.mode != UpscalerMode::bilinear)
        throw CommandLineError("--render-scale requires --upscaler bilinear");
    if (renderer.upscaler.mode == UpscalerMode::bilinear && !render_scale_explicit)
        throw CommandLineError("--upscaler bilinear requires an explicit --render-scale");
    if (renderer.camera_script.kind == CameraScriptKind::teleport && !options.frame_limit.has_value())
        throw CommandLineError("--camera-path teleport requires --frames so the cut is reproducible");
    if (options.blender_executable.has_value() && !options.asset_path.has_value())
        throw CommandLineError("--blender requires --asset");
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
        "  --asset <path>                 Load .gltf/.glb, compatibility .obj/.fbx/.3ds (optional build), or .blend\n"
        "                                 through an external headless Blender conversion.\n"
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
        "  --help, -h, /?                 Show this help without initializing Direct3D 12.\n\n"
        "Renderer modes (Campaigns D-G):\n"
        "  --renderer <mode>              raster (default), hybrid (DXR shadows/reflections), pathtrace.\n"
        "                                 DXR modes report UNSUPPORTED and keep raster on non-DXR adapters.\n"
        "  --rt-shadows on|off            Hybrid ray-traced punctual-light visibility, default on.\n"
        "  --rt-reflections on|off        Hybrid ray-traced GGX reflections, default on.\n"
        "  --rt-reflection-max-roughness <v>  Roughness ceiling for ray-traced reflections, default 0.5.\n"
        "  --rt-denoise on|off            Temporal/a-trous filtering of hybrid reflections, default on.\n"
        "  --rt-debug <view>              none, shadow-visibility, reflection, reflection-hit-distance,\n"
        "                                 direct-lighting, instance-id.\n"
        "  --pt-preset reference|interactive  Reference = unbiased accumulation; interactive = denoised.\n"
        "  --pt-spp <n>                   Path-tracer samples per pixel per frame (1-64), default 1.\n"
        "  --pt-max-depth <n>             Maximum path vertices (1-64), default 8.\n"
        "  --pt-rr-depth <n>              First depth at which Russian roulette may terminate, default 3.\n"
        "  --pt-seed <n>                  Deterministic RNG seed, default 0.\n"
        "  --pt-target-samples <n>        Stop accumulating at n samples per pixel.\n"
        "  --pt-clamp <v>                 Interactive firefly luminance clamp (biased; 0 = off).\n"
        "  --pt-denoise on|off            Interactive preset only.\n"
        "  --pt-debug <view>              none, sample-count, variance, direct, indirect, path-length,\n"
        "                                 material-id, nonfinite.\n"
        "  --capture-samples <n>          Capture when the progressive accumulation reaches n samples.\n"
        "  --capture-hdr <pfm>            Write the pre-tone-map linear signal as float32 PFM.\n"
        "  --atrous-iterations <n>        Edge-aware a-trous iterations (0-5), default 4.\n"
        "  --history-length <n>           Maximum temporal history length, default 32.\n"
        "  --history-clip <gamma>         Neighbourhood history clip in std-devs (0 = off).\n"
        "  --jitter                       Halton(2,3) projection jitter (motion vectors stay jitter-free).\n"
        "  --temporal-debug <view>        none, motion, depth, normals, disocclusion, history-length,\n"
        "                                 variance, reactive, raw-signal.\n"
        "  --upscaler none|bilinear|dlss  Output reconstruction; dlss requires an opt-in SDK build.\n"
        "  --dlss-quality <mode>          dlaa, quality, balanced, performance, ultra-performance.\n"
        "  --render-scale <v>             Internal render scale for --upscaler bilinear [0.25, 1].\n"
        "  --camera-path <script>         static, orbit:<degrees-per-frame>, teleport:<frame>.\n"
        "  --animate-node <i>:<kind>:<r>  Rigid node animation: rotate-y, translate-x, bob-y (repeatable).\n"
        "  --animate-emissive <period>    Pulse emissive strength with the given period in frames.\n"
        "  --builtin-scene <name>         triangle, cornell-box, or alpha-instancing validation scene.\n\n"
        "Asset compatibility (Campaign H):\n"
        "  --asset-cache <dir>            Content-addressed canonical-scene cache directory.\n"
        "  --texture-search-root <dir>    Additional texture search root (repeatable, ordered).\n"
        "  --blender <exe>                Blender executable used for headless .blend conversion.\n"
        "  --blender-timeout <seconds>    Blender conversion timeout, default 300.\n";
}

bool capture_requested(const CommandLineOptions& options) noexcept
{
    return options.capture_path.has_value() || options.validate_hdr || options.capture_hdr_path.has_value();
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
