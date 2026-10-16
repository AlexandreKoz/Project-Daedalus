#pragma once

#include "scene/Math.h"

#include <cstdint>
#include <optional>

namespace daedalus
{
// Renderer-wide temporal coordinate contract (Campaign F)
// -------------------------------------------------------
// Pixel coordinates: x to the right, y downward, origin at the top-left corner of the top-left
// pixel; pixel centres are at integer + 0.5. Units are INTERNAL RENDER pixels.
//
// Jitter: a sub-pixel offset (jx, jy) in render pixels, with the same axes as pixel coordinates,
// applied ONLY to the projection matrix (never to the view/camera transform). The rasterized
// sample for pixel p therefore lies at p + 0.5 + jitter.
//
// Motion vectors: motion = previous_pixel - current_pixel, both obtained by projecting the same
// surface point with the UNJITTERED current and previous view-projections (and the previous
// object transform for moving instances). Motion vectors are therefore jitter-free: jitter is
// accounted for exactly once, in the projection used for rasterization/primary rays.
// To fetch history for pixel p: history_uv = (p + 0.5 + motion) / render_size.

struct CameraFrame
{
    Mat4 view = identity_matrix();
    Mat4 projection = identity_matrix();            // unjittered
    Mat4 projection_jittered = identity_matrix();
    Mat4 view_projection = identity_matrix();       // unjittered
    Mat4 view_projection_jittered = identity_matrix();
    Vec3 position{};
    Vec2 jitter_pixels{};
    float near_plane = 0.01F;
    float far_plane = 1000.0F;
};

// Radical inverse in the given prime base; index must be >= 1 for a non-zero result.
[[nodiscard]] float halton(std::uint32_t index, std::uint32_t base) noexcept;
// Deterministic Halton(2,3) jitter in (-0.5, 0.5) render pixels for 0-based frame index.
[[nodiscard]] Vec2 halton_jitter_pixels(std::uint64_t frame_index, std::uint32_t sequence_length = 16U) noexcept;
// Returns P' = T_ndc(jitter) * P so the NDC position of every point shifts by exactly
// (2*jx/width, -2*jy/height); valid for any projection because the shift is scaled by clip w.
[[nodiscard]] Mat4 apply_projection_jitter(const Mat4& projection, Vec2 jitter_pixels,
                                           std::uint32_t width, std::uint32_t height) noexcept;
// Projects a world point to continuous render-pixel coordinates; std::nullopt if behind the eye.
[[nodiscard]] std::optional<Vec2> project_to_pixel(const Mat4& view_projection, Vec3 world,
                                                   std::uint32_t width, std::uint32_t height) noexcept;
// motion = previous_pixel - current_pixel for a surface point (see contract above).
[[nodiscard]] std::optional<Vec2> motion_vector_pixels(const Mat4& current_view_projection,
                                                       const Mat4& previous_view_projection,
                                                       Vec3 current_world,
                                                       Vec3 previous_world,
                                                       std::uint32_t width,
                                                       std::uint32_t height) noexcept;

enum class MotionClass
{
    static_surface,
    camera_only,
    rigid_object
};

[[nodiscard]] MotionClass classify_motion(bool camera_changed, bool object_transform_changed) noexcept;

// Tracks current/previous camera frames. `begin_frame` rotates current into previous; when no
// valid previous frame exists (first frame, cut, resize) previous is set equal to current so
// that motion is zero and the history-valid flag carries the reset.
class TemporalFrameData final
{
public:
    void begin_frame(const CameraFrame& current, bool history_valid) noexcept;

    [[nodiscard]] const CameraFrame& current() const noexcept { return current_; }
    [[nodiscard]] const CameraFrame& previous() const noexcept { return previous_; }
    [[nodiscard]] bool history_valid() const noexcept { return history_valid_; }
    [[nodiscard]] std::uint64_t frame_index() const noexcept { return frame_index_; }

private:
    CameraFrame current_{};
    CameraFrame previous_{};
    bool has_current_ = false;
    bool history_valid_ = false;
    std::uint64_t frame_index_ = 0;
};
}
