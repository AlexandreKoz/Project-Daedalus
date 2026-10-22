#pragma once

#include "scene/Math.h"

#include <cstdint>

namespace daedalus
{
struct OrbitInput
{
    float orbit_delta_x = 0.0F;
    float orbit_delta_y = 0.0F;
    float pan_delta_x = 0.0F;
    float pan_delta_y = 0.0F;
    float zoom_delta = 0.0F;
    bool reset = false;
};

class OrbitCamera final
{
public:
    OrbitCamera() = default;
    explicit OrbitCamera(const Aabb& bounds);

    void frame_bounds(const Aabb& bounds);
    void apply_input(const OrbitInput& input, std::uint32_t viewport_width, std::uint32_t viewport_height);
    // Scripted orbit (Campaign F camera paths); pitch is clamped exactly as interactive input is.
    void orbit_by(float yaw_radians, float pitch_radians) noexcept;

    [[nodiscard]] Vec3 eye() const noexcept;
    [[nodiscard]] Vec3 target() const noexcept;
    [[nodiscard]] Mat4 view_matrix() const noexcept;
    [[nodiscard]] Mat4 projection_matrix(float aspect) const noexcept;
    [[nodiscard]] Mat4 view_projection_matrix(float aspect) const noexcept;
    [[nodiscard]] float near_plane() const noexcept;
    [[nodiscard]] float far_plane() const noexcept;
    [[nodiscard]] float vertical_fov_radians() const noexcept;

private:
    Vec3 initial_target_{};
    float initial_radius_ = 3.0F;
    Vec3 target_{};
    float radius_ = 3.0F;
    float yaw_ = 0.0F;
    float pitch_ = 0.2F;
};
}
