#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace daedalus
{
// Every reason a renderer-owned history (progressive accumulation, temporal denoiser history,
// DLSS feature history) may need to be discarded. Reasons are a bitmask so one frame can report
// several simultaneous causes.
enum class ResetReason : std::uint32_t
{
    none = 0,
    initial = 1U << 0U,
    scene_reload = 1U << 1U,
    resize = 1U << 2U,
    camera_cut = 1U << 3U,
    camera_moved = 1U << 4U,
    object_moved = 1U << 5U,
    material_changed = 1U << 6U,
    light_changed = 1U << 7U,
    environment_changed = 1U << 8U,
    exposure_changed = 1U << 9U,
    settings_changed = 1U << 10U,
    mode_changed = 1U << 11U,
    seed_changed = 1U << 12U,
    render_resolution_changed = 1U << 13U,
    user_requested = 1U << 14U
};

inline constexpr std::size_t kResetReasonCount = 15U;

[[nodiscard]] constexpr ResetReason operator|(ResetReason left, ResetReason right) noexcept
{
    return static_cast<ResetReason>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}
[[nodiscard]] constexpr ResetReason operator&(ResetReason left, ResetReason right) noexcept
{
    return static_cast<ResetReason>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
}
constexpr ResetReason& operator|=(ResetReason& left, ResetReason right) noexcept
{
    left = left | right;
    return left;
}
[[nodiscard]] constexpr bool any(ResetReason value) noexcept
{
    return static_cast<std::uint32_t>(value) != 0U;
}

[[nodiscard]] std::string_view reset_reason_name(std::size_t bit_index) noexcept;
[[nodiscard]] std::string describe_reset_reasons(ResetReason reasons);

// History-specific invalidation policy. The masks are deliberately different:
//  * a progressive reference accumulation is invalid after ANY change to the rendered function;
//  * a reprojecting temporal history survives smooth camera/object motion (handled through motion
//    vectors and rejection tests) but not cuts, resolution changes, reloads or setting changes;
//  * exposure is applied after all linear-radiance histories, so it invalidates none of them.
[[nodiscard]] ResetReason accumulation_invalidating_reasons() noexcept;
[[nodiscard]] ResetReason temporal_history_invalidating_reasons() noexcept;
[[nodiscard]] ResetReason upscaler_history_invalidating_reasons() noexcept;

[[nodiscard]] bool invalidates_accumulation(ResetReason reasons) noexcept;
[[nodiscard]] bool invalidates_temporal_history(ResetReason reasons) noexcept;
[[nodiscard]] bool invalidates_upscaler_history(ResetReason reasons) noexcept;

// Monotonic revision counters. A renderer bumps the relevant revision when the corresponding
// input changes; RevisionTracker compares consecutive snapshots and emits reasons.
struct RenderRevisions
{
    std::uint64_t scene = 0;
    std::uint64_t transforms = 0;
    std::uint64_t materials = 0;
    std::uint64_t lights = 0;
    std::uint64_t environment = 0;
    std::uint64_t camera = 0;
    std::uint64_t camera_cut = 0;
    std::uint64_t exposure = 0;
    std::uint64_t settings = 0;
    std::uint64_t seed = 0;
    std::uint64_t mode = 0;
    std::uint64_t render_resolution = 0;
    std::uint64_t user_reset = 0;
};

class RevisionTracker final
{
public:
    // Returns the reasons that apply to the next frame. The first call always reports `initial`.
    [[nodiscard]] ResetReason observe(const RenderRevisions& current) noexcept;
    void force(ResetReason reasons) noexcept;

    [[nodiscard]] std::uint64_t count(std::size_t bit_index) const noexcept;
    [[nodiscard]] const std::array<std::uint64_t, kResetReasonCount>& counts() const noexcept;
    [[nodiscard]] std::string counts_json() const;

private:
    bool initialized_ = false;
    RenderRevisions previous_{};
    ResetReason forced_ = ResetReason::none;
    std::array<std::uint64_t, kResetReasonCount> counts_{};
};
}
