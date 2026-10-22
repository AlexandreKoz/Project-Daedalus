#include "core/RenderSettings.h"

namespace daedalus
{
std::string_view to_string(RendererMode value) noexcept
{
    switch (value)
    {
    case RendererMode::raster: return "raster";
    case RendererMode::hybrid: return "hybrid";
    case RendererMode::pathtrace: return "pathtrace";
    }
    return "raster";
}

std::string_view to_string(UpscalerMode value) noexcept
{
    switch (value)
    {
    case UpscalerMode::none: return "none";
    case UpscalerMode::bilinear: return "bilinear";
    case UpscalerMode::dlss: return "dlss";
    }
    return "none";
}

std::string_view to_string(DlssQualityMode value) noexcept
{
    switch (value)
    {
    case DlssQualityMode::dlaa: return "dlaa";
    case DlssQualityMode::quality: return "quality";
    case DlssQualityMode::balanced: return "balanced";
    case DlssQualityMode::performance: return "performance";
    case DlssQualityMode::ultra_performance: return "ultra-performance";
    }
    return "quality";
}

std::string_view to_string(HybridDebugView value) noexcept
{
    switch (value)
    {
    case HybridDebugView::none: return "none";
    case HybridDebugView::shadow_visibility: return "shadow-visibility";
    case HybridDebugView::reflection: return "reflection";
    case HybridDebugView::reflection_hit_distance: return "reflection-hit-distance";
    case HybridDebugView::direct_lighting: return "direct-lighting";
    case HybridDebugView::instance_id: return "instance-id";
    }
    return "none";
}

std::string_view to_string(PathTraceDebugView value) noexcept
{
    switch (value)
    {
    case PathTraceDebugView::none: return "none";
    case PathTraceDebugView::sample_count: return "sample-count";
    case PathTraceDebugView::variance: return "variance";
    case PathTraceDebugView::direct: return "direct";
    case PathTraceDebugView::indirect: return "indirect";
    case PathTraceDebugView::path_length: return "path-length";
    case PathTraceDebugView::material_id: return "material-id";
    case PathTraceDebugView::nonfinite: return "nonfinite";
    }
    return "none";
}

std::string_view to_string(TemporalDebugView value) noexcept
{
    switch (value)
    {
    case TemporalDebugView::none: return "none";
    case TemporalDebugView::motion: return "motion";
    case TemporalDebugView::depth: return "depth";
    case TemporalDebugView::normals: return "normals";
    case TemporalDebugView::disocclusion: return "disocclusion";
    case TemporalDebugView::history_length: return "history-length";
    case TemporalDebugView::variance: return "variance";
    case TemporalDebugView::reactive: return "reactive";
    case TemporalDebugView::raw_signal: return "raw-signal";
    }
    return "none";
}

std::string_view to_string(PathTracePreset value) noexcept
{
    return value == PathTracePreset::interactive ? "interactive" : "reference";
}

std::string_view to_string(NodeAnimationKind value) noexcept
{
    switch (value)
    {
    case NodeAnimationKind::rotate_y: return "rotate-y";
    case NodeAnimationKind::translate_x: return "translate-x";
    case NodeAnimationKind::bob_y: return "bob-y";
    }
    return "rotate-y";
}

std::optional<RendererMode> parse_renderer_mode(std::string_view value) noexcept
{
    if (value == "raster") return RendererMode::raster;
    if (value == "hybrid") return RendererMode::hybrid;
    if (value == "pathtrace") return RendererMode::pathtrace;
    return std::nullopt;
}

std::optional<UpscalerMode> parse_upscaler_mode(std::string_view value) noexcept
{
    if (value == "none") return UpscalerMode::none;
    if (value == "bilinear") return UpscalerMode::bilinear;
    if (value == "dlss") return UpscalerMode::dlss;
    return std::nullopt;
}

std::optional<DlssQualityMode> parse_dlss_quality(std::string_view value) noexcept
{
    if (value == "dlaa") return DlssQualityMode::dlaa;
    if (value == "quality") return DlssQualityMode::quality;
    if (value == "balanced") return DlssQualityMode::balanced;
    if (value == "performance") return DlssQualityMode::performance;
    if (value == "ultra-performance") return DlssQualityMode::ultra_performance;
    return std::nullopt;
}

std::optional<HybridDebugView> parse_hybrid_debug_view(std::string_view value) noexcept
{
    if (value == "none") return HybridDebugView::none;
    if (value == "shadow-visibility") return HybridDebugView::shadow_visibility;
    if (value == "reflection") return HybridDebugView::reflection;
    if (value == "reflection-hit-distance") return HybridDebugView::reflection_hit_distance;
    if (value == "direct-lighting") return HybridDebugView::direct_lighting;
    if (value == "instance-id") return HybridDebugView::instance_id;
    return std::nullopt;
}

std::optional<PathTraceDebugView> parse_path_trace_debug_view(std::string_view value) noexcept
{
    if (value == "none") return PathTraceDebugView::none;
    if (value == "sample-count") return PathTraceDebugView::sample_count;
    if (value == "variance") return PathTraceDebugView::variance;
    if (value == "direct") return PathTraceDebugView::direct;
    if (value == "indirect") return PathTraceDebugView::indirect;
    if (value == "path-length") return PathTraceDebugView::path_length;
    if (value == "material-id") return PathTraceDebugView::material_id;
    if (value == "nonfinite") return PathTraceDebugView::nonfinite;
    return std::nullopt;
}

std::optional<TemporalDebugView> parse_temporal_debug_view(std::string_view value) noexcept
{
    if (value == "none") return TemporalDebugView::none;
    if (value == "motion") return TemporalDebugView::motion;
    if (value == "depth") return TemporalDebugView::depth;
    if (value == "normals") return TemporalDebugView::normals;
    if (value == "disocclusion") return TemporalDebugView::disocclusion;
    if (value == "history-length") return TemporalDebugView::history_length;
    if (value == "variance") return TemporalDebugView::variance;
    if (value == "reactive") return TemporalDebugView::reactive;
    if (value == "raw-signal") return TemporalDebugView::raw_signal;
    return std::nullopt;
}

std::optional<PathTracePreset> parse_path_trace_preset(std::string_view value) noexcept
{
    if (value == "reference") return PathTracePreset::reference;
    if (value == "interactive") return PathTracePreset::interactive;
    return std::nullopt;
}

bool path_trace_is_unbiased_reference(const PathTraceSettings& settings) noexcept
{
    return settings.preset == PathTracePreset::reference && settings.firefly_clamp == 0.0F && !settings.denoise &&
           settings.debug_view == PathTraceDebugView::none;
}

bool requires_geometry_buffer(const RendererSettings& settings) noexcept
{
    return settings.mode != RendererMode::raster || settings.upscaler.mode == UpscalerMode::dlss ||
           settings.temporal.jitter || settings.temporal.debug_view != TemporalDebugView::none;
}
}
