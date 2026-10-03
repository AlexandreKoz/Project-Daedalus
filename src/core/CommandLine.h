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
    tangents,
    bounds,
    base_color,
    metallic,
    roughness,
    emissive,
    material_id,
    depth,
    overdraw
};

struct CommandLineOptions
{
    bool use_warp = false;
    bool show_help = false;
    bool dump_scene = false;
    bool stress_resize = false;
    bool report_live_objects = false;
    bool suppress_error_dialog = false;
    bool shadows_enabled = true;
    bool validate_hdr = false;
    std::optional<std::uint64_t> frame_limit;
    std::optional<std::uint64_t> stress_reload_count;
    std::optional<std::uint64_t> capture_frame;
    std::optional<std::uint64_t> benchmark_warmup_frames;
    std::optional<std::filesystem::path> asset_path;
    std::optional<std::string> scene_selector;
    std::optional<std::filesystem::path> import_report_path;
    std::optional<std::filesystem::path> stress_alternate_asset_path;
    std::optional<std::filesystem::path> capture_path;
    std::optional<std::filesystem::path> capture_metadata_path;
    std::optional<std::filesystem::path> benchmark_output_path;
    DiagnosticMode diagnostic_mode = DiagnosticMode::shaded;
    float exposure_ev = 0.0F;
    float environment_intensity = 1.0F;
    float shadow_constant_bias = 0.0010F;
    float shadow_normal_bias = 0.0020F;
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
