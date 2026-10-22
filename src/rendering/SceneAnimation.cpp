#include "rendering/SceneAnimation.h"

#include <cmath>
#include <functional>
#include <utility>
#include <stdexcept>
#include <string>

namespace daedalus
{
namespace
{
constexpr float kTwoPi = 6.28318530717958647692F;

[[nodiscard]] bool same_matrix(const Mat4& left, const Mat4& right) noexcept
{
    return left.values == right.values;
}

[[nodiscard]] Mat4 rotation_y(float radians) noexcept
{
    const float half = radians * 0.5F;
    return rotation_matrix(Quat{0.0F, std::sin(half), 0.0F, std::cos(half)});
}
}

SceneAnimator::SceneAnimator(const CanonicalScene& scene,
                             std::span<const PreparedRasterDraw> draws,
                             std::vector<NodeAnimation> animations,
                             std::optional<std::uint32_t> emissive_pulse_period_frames)
    : animations_(std::move(animations)), emissive_period_(emissive_pulse_period_frames)
{
    local_transforms_.reserve(scene.nodes.size());
    parents_.reserve(scene.nodes.size());
    for (const Node& node : scene.nodes)
    {
        local_transforms_.push_back(node.local_transform);
        parents_.push_back(node.parent.valid() ? static_cast<std::int64_t>(node.parent.value()) : -1);
    }
    for (const NodeAnimation& animation : animations_)
    {
        if (animation.node >= scene.nodes.size())
            throw std::runtime_error("--animate-node references node " + std::to_string(animation.node) +
                                     " but the scene has " + std::to_string(scene.nodes.size()) + " nodes");
        if (!std::isfinite(animation.rate)) throw std::runtime_error("node animation rate must be finite");
    }

    // Parent-before-child order; the canonical importer already rejected cycles.
    std::vector<bool> visited(scene.nodes.size(), false);
    std::function<void(std::uint32_t)> visit = [&](std::uint32_t index)
    {
        if (visited[index]) return;
        visited[index] = true;
        topological_order_.push_back(index);
        for (const NodeId child : scene.nodes[index].children)
            if (child.valid() && child.value() < scene.nodes.size()) visit(child.value());
    };
    for (std::uint32_t index = 0; index < scene.nodes.size(); ++index)
        if (parents_[index] < 0) visit(index);
    if (topological_order_.size() != scene.nodes.size())
        throw std::runtime_error("scene animation requires an acyclic node hierarchy");

    draw_nodes_.reserve(draws.size());
    for (const PreparedRasterDraw& draw : draws)
    {
        if (draw.node_index >= scene.nodes.size()) throw std::runtime_error("prepared draw references an invalid node");
        draw_nodes_.push_back(draw.node_index);
    }
    if (scene.selected_scene.valid() && scene.selected_scene.value() < scene.scenes.size())
    {
        const float diagonal = length(extent(scene.scenes[scene.selected_scene.value()].bounds));
        animation_extent_ = std::isfinite(diagonal) && diagonal > 1.0e-4F ? diagonal * 0.15F : 1.0F;
    }
}

bool SceneAnimator::animated() const noexcept
{
    return !animations_.empty() || emissive_period_.has_value();
}

std::vector<Mat4> SceneAnimator::evaluate_world(std::uint64_t frame) const
{
    std::vector<Mat4> local = local_transforms_;
    const auto time = static_cast<float>(frame);
    for (const NodeAnimation& animation : animations_)
    {
        Mat4& transform = local[animation.node];
        switch (animation.kind)
        {
        case NodeAnimationKind::rotate_y:
            transform = multiply(transform, rotation_y(animation.rate * time));
            break;
        case NodeAnimationKind::translate_x:
            transform = multiply(translation_matrix({std::sin(animation.rate * time) * animation_extent_, 0.0F, 0.0F}), transform);
            break;
        case NodeAnimationKind::bob_y:
            transform = multiply(translation_matrix({0.0F, std::sin(animation.rate * time) * animation_extent_, 0.0F}), transform);
            break;
        }
    }
    std::vector<Mat4> world(local.size(), identity_matrix());
    for (const std::uint32_t index : topological_order_)
        world[index] = parents_[index] < 0 ? local[index] : multiply(world[static_cast<std::size_t>(parents_[index])], local[index]);
    return world;
}

const AnimatedFrame& SceneAnimator::evaluate(std::uint64_t frame, bool reset_history)
{
    std::vector<Mat4> draw_world(draw_nodes_.size());
    if (animations_.empty() && evaluated_)
    {
        draw_world = frame_.draw_world;
    }
    else
    {
        const std::vector<Mat4> world = evaluate_world(frame);
        for (std::size_t index = 0; index < draw_nodes_.size(); ++index) draw_world[index] = world[draw_nodes_[index]];
    }

    bool changed = !evaluated_;
    if (evaluated_)
        for (std::size_t index = 0; index < draw_world.size() && !changed; ++index)
            changed = !same_matrix(draw_world[index], frame_.draw_world[index]);

    float emissive_scale = 1.0F;
    if (emissive_period_.has_value())
        emissive_scale = 0.5F + 0.5F * std::cos(kTwoPi * static_cast<float>(frame % *emissive_period_) /
                                                static_cast<float>(*emissive_period_));

    frame_.previous_draw_world = (evaluated_ && !reset_history) ? frame_.draw_world : draw_world;
    frame_.transforms_changed = evaluated_ && changed;
    frame_.emissive_changed = evaluated_ && emissive_scale != frame_.emissive_scale;
    frame_.emissive_scale = emissive_scale;
    frame_.draw_world = std::move(draw_world);
    evaluated_ = true;
    return frame_;
}
}
