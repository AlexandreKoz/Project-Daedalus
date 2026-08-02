#include "rendering/OrbitCamera.h"

#include <algorithm>
#include <cmath>

namespace daedalus
{
namespace
{
constexpr float kPi = 3.14159265358979323846F;
}

OrbitCamera::OrbitCamera(const Aabb& bounds)
{
    frame_bounds(bounds);
}

void OrbitCamera::frame_bounds(const Aabb& bounds)
{
    initial_target_ = bounds.empty() ? Vec3{} : center(bounds);
    const Vec3 dimensions = extent(bounds);
    const float diagonal = length(dimensions);
    initial_radius_ = std::isfinite(diagonal) && diagonal > 1.0e-4F ? std::max(diagonal * 1.25F, 0.25F) : 3.0F;
    target_ = initial_target_;
    radius_ = initial_radius_;
    yaw_ = 0.0F;
    pitch_ = 0.2F;
}

void OrbitCamera::apply_input(const OrbitInput& input, std::uint32_t viewport_width, std::uint32_t viewport_height)
{
    if (input.reset)
    {
        target_ = initial_target_;
        radius_ = initial_radius_;
        yaw_ = 0.0F;
        pitch_ = 0.2F;
    }

    yaw_ += input.orbit_delta_x * 0.008F;
    pitch_ = std::clamp(pitch_ + input.orbit_delta_y * 0.008F, -kPi * 0.49F, kPi * 0.49F);
    radius_ *= std::exp(-input.zoom_delta * 0.12F);
    radius_ = std::clamp(radius_, initial_radius_ * 0.02F, initial_radius_ * 100.0F);

    const Vec3 current_eye = eye();
    const Vec3 forward = normalize(target_ - current_eye, {0.0F, 0.0F, -1.0F});
    const Vec3 right = normalize(cross(forward, {0.0F, 1.0F, 0.0F}), {1.0F, 0.0F, 0.0F});
    const Vec3 up = normalize(cross(right, forward), {0.0F, 1.0F, 0.0F});
    const float viewport_scale = radius_ / static_cast<float>(std::max<std::uint32_t>(1U, std::min(viewport_width, viewport_height)));
    target_ = target_ + right * (-input.pan_delta_x * viewport_scale * 2.0F) + up * (input.pan_delta_y * viewport_scale * 2.0F);
}

Vec3 OrbitCamera::eye() const noexcept
{
    const float cosine_pitch = std::cos(pitch_);
    return target_ + Vec3{
        radius_ * cosine_pitch * std::sin(yaw_),
        radius_ * std::sin(pitch_),
        radius_ * cosine_pitch * std::cos(yaw_)};
}

Vec3 OrbitCamera::target() const noexcept
{
    return target_;
}

Mat4 OrbitCamera::view_matrix() const noexcept
{
    return look_at_rh(eye(), target_, {0.0F, 1.0F, 0.0F});
}

Mat4 OrbitCamera::projection_matrix(float aspect) const noexcept
{
    const float near_plane = std::max(radius_ * 0.001F, 0.001F);
    const float far_plane = std::max(radius_ * 100.0F, near_plane + 1.0F);
    return perspective_rh(50.0F * (kPi / 180.0F), aspect, near_plane, far_plane);
}

Mat4 OrbitCamera::view_projection_matrix(float aspect) const noexcept
{
    return multiply(projection_matrix(aspect), view_matrix());
}
}
