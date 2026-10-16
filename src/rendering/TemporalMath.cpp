#include "rendering/TemporalMath.h"

#include <cmath>

namespace daedalus
{
float halton(std::uint32_t index, std::uint32_t base) noexcept
{
    if (base < 2U) return 0.0F;
    double fraction = 1.0;
    double result = 0.0;
    while (index > 0U)
    {
        fraction /= static_cast<double>(base);
        result += fraction * static_cast<double>(index % base);
        index /= base;
    }
    return static_cast<float>(result);
}

Vec2 halton_jitter_pixels(std::uint64_t frame_index, std::uint32_t sequence_length) noexcept
{
    const std::uint32_t length = sequence_length == 0U ? 1U : sequence_length;
    // 1-based Halton index so the first sample is (0.5, 1/3) - 0.5 rather than the degenerate origin.
    const auto index = static_cast<std::uint32_t>(frame_index % length) + 1U;
    return {halton(index, 2U) - 0.5F, halton(index, 3U) - 0.5F};
}

Mat4 apply_projection_jitter(const Mat4& projection, Vec2 jitter_pixels, std::uint32_t width, std::uint32_t height) noexcept
{
    if (width == 0U || height == 0U) return projection;
    const float ndc_x = 2.0F * jitter_pixels.x / static_cast<float>(width);
    const float ndc_y = -2.0F * jitter_pixels.y / static_cast<float>(height);
    Mat4 result = projection;
    for (std::size_t column = 0; column < 4U; ++column)
    {
        result.at(0, column) += ndc_x * projection.at(3, column);
        result.at(1, column) += ndc_y * projection.at(3, column);
    }
    return result;
}

std::optional<Vec2> project_to_pixel(const Mat4& view_projection, Vec3 world, std::uint32_t width, std::uint32_t height) noexcept
{
    const float x = view_projection.at(0, 0) * world.x + view_projection.at(0, 1) * world.y + view_projection.at(0, 2) * world.z + view_projection.at(0, 3);
    const float y = view_projection.at(1, 0) * world.x + view_projection.at(1, 1) * world.y + view_projection.at(1, 2) * world.z + view_projection.at(1, 3);
    const float w = view_projection.at(3, 0) * world.x + view_projection.at(3, 1) * world.y + view_projection.at(3, 2) * world.z + view_projection.at(3, 3);
    if (!(w > 1.0e-20F) || !std::isfinite(w)) return std::nullopt;
    const float ndc_x = x / w;
    const float ndc_y = y / w;
    return Vec2{(ndc_x * 0.5F + 0.5F) * static_cast<float>(width), (0.5F - ndc_y * 0.5F) * static_cast<float>(height)};
}

std::optional<Vec2> motion_vector_pixels(const Mat4& current_view_projection,
                                         const Mat4& previous_view_projection,
                                         Vec3 current_world,
                                         Vec3 previous_world,
                                         std::uint32_t width,
                                         std::uint32_t height) noexcept
{
    const std::optional<Vec2> current = project_to_pixel(current_view_projection, current_world, width, height);
    const std::optional<Vec2> previous = project_to_pixel(previous_view_projection, previous_world, width, height);
    if (!current.has_value() || !previous.has_value()) return std::nullopt;
    return Vec2{previous->x - current->x, previous->y - current->y};
}

MotionClass classify_motion(bool camera_changed, bool object_transform_changed) noexcept
{
    if (object_transform_changed) return MotionClass::rigid_object;
    if (camera_changed) return MotionClass::camera_only;
    return MotionClass::static_surface;
}

void TemporalFrameData::begin_frame(const CameraFrame& current, bool history_valid) noexcept
{
    previous_ = (has_current_ && history_valid) ? current_ : current;
    current_ = current;
    if (!(has_current_ && history_valid))
    {
        // No usable previous frame: previous jitter equals current so jitter deltas are zero.
        previous_.jitter_pixels = current.jitter_pixels;
    }
    history_valid_ = has_current_ && history_valid;
    has_current_ = true;
    ++frame_index_;
}
}
