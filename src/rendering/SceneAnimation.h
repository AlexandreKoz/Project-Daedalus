#pragma once

// Deterministic, source-driven scene animation for temporal validation (Campaign F). This is not a
// general animation system: it applies a small declared set of rigid per-node motions and an
// emissive pulse so camera-only, rigid-object and emissive-change cases are reproducible from the
// command line. Skinned/morph animation is UNSUPPORTED (and the canonical scene has none).

#include "core/RenderSettings.h"
#include "rendering/RasterPreparation.h"
#include "scene/Scene.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace daedalus
{
struct AnimatedFrame
{
    std::vector<Mat4> draw_world;        // world transform per prepared draw (instance)
    std::vector<Mat4> previous_draw_world;
    bool transforms_changed = false;     // compared with the previous evaluated frame
    float emissive_scale = 1.0F;
    bool emissive_changed = false;
};

class SceneAnimator final
{
public:
    SceneAnimator(const CanonicalScene& scene,
                  std::span<const PreparedRasterDraw> draws,
                  std::vector<NodeAnimation> animations,
                  std::optional<std::uint32_t> emissive_pulse_period_frames);

    // Evaluates the animation state for a 0-based animation frame. Calling with the same frame twice
    // reports no change. `reset_history` makes previous transforms equal to current (no motion).
    const AnimatedFrame& evaluate(std::uint64_t frame, bool reset_history);

    [[nodiscard]] bool animated() const noexcept;
    [[nodiscard]] const AnimatedFrame& current() const noexcept { return frame_; }
    [[nodiscard]] const std::vector<NodeAnimation>& animations() const noexcept { return animations_; }

private:
    [[nodiscard]] std::vector<Mat4> evaluate_world(std::uint64_t frame) const;

    std::vector<Mat4> local_transforms_;
    std::vector<std::int64_t> parents_;
    std::vector<std::uint32_t> topological_order_;
    std::vector<std::uint32_t> draw_nodes_;
    std::vector<NodeAnimation> animations_;
    std::optional<std::uint32_t> emissive_period_;
    float animation_extent_ = 1.0F;
    AnimatedFrame frame_;
    bool evaluated_ = false;
};
}
