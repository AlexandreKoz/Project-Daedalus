#include "rendering/FrameRevision.h"

#include <sstream>

namespace daedalus
{
std::string_view reset_reason_name(std::size_t bit_index) noexcept
{
    static constexpr std::array<std::string_view, kResetReasonCount> names{
        "initial", "scene_reload", "resize", "camera_cut", "camera_moved", "object_moved",
        "material_changed", "light_changed", "environment_changed", "exposure_changed",
        "settings_changed", "mode_changed", "seed_changed", "render_resolution_changed", "user_requested"};
    return bit_index < names.size() ? names[bit_index] : std::string_view("unknown");
}

std::string describe_reset_reasons(ResetReason reasons)
{
    if (!any(reasons)) return "none";
    std::string result;
    for (std::size_t bit = 0; bit < kResetReasonCount; ++bit)
    {
        if ((static_cast<std::uint32_t>(reasons) & (1U << bit)) == 0U) continue;
        if (!result.empty()) result += '|';
        result += reset_reason_name(bit);
    }
    return result;
}

ResetReason accumulation_invalidating_reasons() noexcept
{
    return ResetReason::initial | ResetReason::scene_reload | ResetReason::resize | ResetReason::camera_cut |
           ResetReason::camera_moved | ResetReason::object_moved | ResetReason::material_changed |
           ResetReason::light_changed | ResetReason::environment_changed | ResetReason::settings_changed |
           ResetReason::mode_changed | ResetReason::seed_changed | ResetReason::render_resolution_changed |
           ResetReason::user_requested;
}

ResetReason temporal_history_invalidating_reasons() noexcept
{
    return ResetReason::initial | ResetReason::scene_reload | ResetReason::resize | ResetReason::camera_cut |
           ResetReason::settings_changed | ResetReason::mode_changed | ResetReason::render_resolution_changed |
           ResetReason::environment_changed | ResetReason::user_requested;
}

ResetReason upscaler_history_invalidating_reasons() noexcept
{
    return ResetReason::initial | ResetReason::scene_reload | ResetReason::resize | ResetReason::camera_cut |
           ResetReason::mode_changed | ResetReason::render_resolution_changed | ResetReason::settings_changed |
           ResetReason::user_requested;
}

bool invalidates_accumulation(ResetReason reasons) noexcept
{
    return any(reasons & accumulation_invalidating_reasons());
}

bool invalidates_temporal_history(ResetReason reasons) noexcept
{
    return any(reasons & temporal_history_invalidating_reasons());
}

bool invalidates_upscaler_history(ResetReason reasons) noexcept
{
    return any(reasons & upscaler_history_invalidating_reasons());
}

ResetReason RevisionTracker::observe(const RenderRevisions& current) noexcept
{
    ResetReason reasons = forced_;
    forced_ = ResetReason::none;
    if (!initialized_)
    {
        reasons |= ResetReason::initial;
        initialized_ = true;
    }
    else
    {
        if (current.scene != previous_.scene) reasons |= ResetReason::scene_reload;
        if (current.transforms != previous_.transforms) reasons |= ResetReason::object_moved;
        if (current.materials != previous_.materials) reasons |= ResetReason::material_changed;
        if (current.lights != previous_.lights) reasons |= ResetReason::light_changed;
        if (current.environment != previous_.environment) reasons |= ResetReason::environment_changed;
        if (current.camera != previous_.camera) reasons |= ResetReason::camera_moved;
        if (current.camera_cut != previous_.camera_cut) reasons |= ResetReason::camera_cut;
        if (current.exposure != previous_.exposure) reasons |= ResetReason::exposure_changed;
        if (current.settings != previous_.settings) reasons |= ResetReason::settings_changed;
        if (current.seed != previous_.seed) reasons |= ResetReason::seed_changed;
        if (current.mode != previous_.mode) reasons |= ResetReason::mode_changed;
        if (current.render_resolution != previous_.render_resolution) reasons |= ResetReason::render_resolution_changed;
        if (current.user_reset != previous_.user_reset) reasons |= ResetReason::user_requested;
    }
    previous_ = current;
    for (std::size_t bit = 0; bit < kResetReasonCount; ++bit)
        if ((static_cast<std::uint32_t>(reasons) & (1U << bit)) != 0U) ++counts_[bit];
    return reasons;
}

void RevisionTracker::force(ResetReason reasons) noexcept
{
    forced_ |= reasons;
}

std::uint64_t RevisionTracker::count(std::size_t bit_index) const noexcept
{
    return bit_index < counts_.size() ? counts_[bit_index] : 0U;
}

const std::array<std::uint64_t, kResetReasonCount>& RevisionTracker::counts() const noexcept
{
    return counts_;
}

std::string RevisionTracker::counts_json() const
{
    std::ostringstream stream;
    stream << '{';
    for (std::size_t bit = 0; bit < kResetReasonCount; ++bit)
    {
        if (bit != 0U) stream << ", ";
        stream << '"' << reset_reason_name(bit) << "\": " << counts_[bit];
    }
    stream << '}';
    return stream.str();
}
}
