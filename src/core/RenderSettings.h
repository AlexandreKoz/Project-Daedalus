#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daedalus
{
// Renderer-facing configuration shared by the CLI, the application, the renderer facade and
// capture/benchmark metadata. Everything here is API independent; D3D12/DXR/SDK state never
// appears in these types.

enum class RendererMode
{
    raster,    // Campaign C2 forward PBR baseline. Always available.
    hybrid,    // Raster primary visibility + DXR shadows/reflections (Campaign D).
    pathtrace  // Progressive DXR reference path tracer (Campaign E).
};

enum class UpscalerMode
{
    none,      // Native: render resolution == display resolution.
    bilinear,  // Explicit non-temporal bilinear resample of linear HDR (not a reconstruction technique).
    dlss       // NVIDIA DLSS Super Resolution through the opt-in vendor boundary (Campaign G).
};

enum class DlssQualityMode
{
    dlaa,
    quality,
    balanced,
    performance,
    ultra_performance
};

enum class HybridDebugView
{
    none,
    shadow_visibility,
    reflection,
    reflection_hit_distance,
    direct_lighting,
    instance_id
};

enum class PathTraceDebugView
{
    none,
    sample_count,
    variance,
    direct,
    indirect,
    path_length,
    material_id,
    nonfinite
};

enum class TemporalDebugView
{
    none,
    motion,
    depth,
    normals,
    disocclusion,
    history_length,
    variance,
    reactive,
    raw_signal
};

enum class PathTracePreset
{
    reference,   // Unbiased progressive accumulation: no clamp, no denoiser, box-filter pixel integration.
    interactive  // Denoised 1-spp-per-frame display; may clamp fireflies. Never labelled ground truth.
};

enum class CameraScriptKind
{
    none,
    orbit,    // Continuous yaw orbit (camera-only motion).
    teleport  // One explicit camera cut at a given frame.
};

enum class NodeAnimationKind
{
    rotate_y,
    translate_x,
    bob_y
};

struct HybridSettings
{
    bool shadows = true;
    bool reflections = true;
    float reflection_max_roughness = 0.5F;
    bool denoise_reflections = true;
    HybridDebugView debug_view = HybridDebugView::none;
};

struct PathTraceSettings
{
    PathTracePreset preset = PathTracePreset::reference;
    std::uint32_t samples_per_frame = 1;
    std::uint32_t max_depth = 8;
    std::uint32_t min_russian_roulette_depth = 3;
    std::uint32_t seed = 0;
    std::optional<std::uint64_t> target_samples;
    // 0 disables clamping. Any non-zero value makes the output biased and is recorded as such.
    float firefly_clamp = 0.0F;
    bool denoise = false;
    PathTraceDebugView debug_view = PathTraceDebugView::none;
};

struct TemporalSettings
{
    std::uint32_t atrous_iterations = 4;
    std::uint32_t max_history_length = 32;
    // Neighbourhood clip half-width in standard deviations; 0 disables clipping.
    float history_clip_gamma = 0.0F;
    bool jitter = false;
    TemporalDebugView debug_view = TemporalDebugView::none;
};

struct UpscalerSettings
{
    UpscalerMode mode = UpscalerMode::none;
    DlssQualityMode dlss_quality = DlssQualityMode::quality;
    // Only meaningful for UpscalerMode::bilinear. DLSS render resolution is chosen by the SDK.
    float render_scale = 1.0F;
};

struct CameraScript
{
    CameraScriptKind kind = CameraScriptKind::none;
    float degrees_per_frame = 0.0F;
    std::uint64_t teleport_frame = 0;
};

struct NodeAnimation
{
    std::uint32_t node = 0;
    NodeAnimationKind kind = NodeAnimationKind::rotate_y;
    float rate = 0.0F; // radians/frame for rotation, scene units/frame amplitude-rate for translation
};

struct RendererSettings
{
    RendererMode mode = RendererMode::raster;
    HybridSettings hybrid;
    PathTraceSettings path_trace;
    TemporalSettings temporal;
    UpscalerSettings upscaler;
    CameraScript camera_script;
    std::vector<NodeAnimation> node_animations;
    std::optional<std::uint32_t> emissive_pulse_period_frames;
};

[[nodiscard]] std::string_view to_string(RendererMode value) noexcept;
[[nodiscard]] std::string_view to_string(UpscalerMode value) noexcept;
[[nodiscard]] std::string_view to_string(DlssQualityMode value) noexcept;
[[nodiscard]] std::string_view to_string(HybridDebugView value) noexcept;
[[nodiscard]] std::string_view to_string(PathTraceDebugView value) noexcept;
[[nodiscard]] std::string_view to_string(TemporalDebugView value) noexcept;
[[nodiscard]] std::string_view to_string(PathTracePreset value) noexcept;
[[nodiscard]] std::string_view to_string(NodeAnimationKind value) noexcept;

[[nodiscard]] std::optional<RendererMode> parse_renderer_mode(std::string_view value) noexcept;
[[nodiscard]] std::optional<UpscalerMode> parse_upscaler_mode(std::string_view value) noexcept;
[[nodiscard]] std::optional<DlssQualityMode> parse_dlss_quality(std::string_view value) noexcept;
[[nodiscard]] std::optional<HybridDebugView> parse_hybrid_debug_view(std::string_view value) noexcept;
[[nodiscard]] std::optional<PathTraceDebugView> parse_path_trace_debug_view(std::string_view value) noexcept;
[[nodiscard]] std::optional<TemporalDebugView> parse_temporal_debug_view(std::string_view value) noexcept;
[[nodiscard]] std::optional<PathTracePreset> parse_path_trace_preset(std::string_view value) noexcept;

// True when the path-tracer configuration may legitimately be described as an unbiased
// reference estimate (no clamp, no denoiser, no debug substitution). This flag is written to
// capture metadata; it says nothing about whether the run converged.
[[nodiscard]] bool path_trace_is_unbiased_reference(const PathTraceSettings& settings) noexcept;

// True when the selected configuration needs a renderer-wide temporal/geometry buffer set
// (motion vectors, linear depth, normals, stable IDs).
[[nodiscard]] bool requires_geometry_buffer(const RendererSettings& settings) noexcept;
}
