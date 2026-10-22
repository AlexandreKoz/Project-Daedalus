// Campaign F portable temporal-contract tests (source only in the D-H drop; not executed).
#include "TestHarness.h"
#include "rendering/FrameRevision.h"
#include "rendering/TemporalContract.h"
#include "rendering/TemporalMath.h"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>

namespace
{
using namespace daedalus;
using namespace daedalus::tests;

constexpr std::uint32_t kWidth = 640U;
constexpr std::uint32_t kHeight = 360U;

Mat4 test_view_projection(Vec3 eye)
{
    const Mat4 view = look_at_rh(eye, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
    const Mat4 projection = perspective_rh(0.9F, static_cast<float>(kWidth) / static_cast<float>(kHeight), 0.1F, 100.0F);
    return multiply(projection, view);
}

void test_halton_sequence()
{
    require_near(halton(1U, 2U), 0.5F, 1.0e-7F, "halton(1,2)");
    require_near(halton(2U, 2U), 0.25F, 1.0e-7F, "halton(2,2)");
    require_near(halton(3U, 2U), 0.75F, 1.0e-7F, "halton(3,2)");
    require_near(halton(1U, 3U), 1.0F / 3.0F, 1.0e-6F, "halton(1,3)");
    require_near(halton(2U, 3U), 2.0F / 3.0F, 1.0e-6F, "halton(2,3)");
    Vec2 mean{};
    for (std::uint64_t frame = 0; frame < 16U; ++frame)
    {
        const Vec2 jitter = halton_jitter_pixels(frame);
        require(jitter.x > -0.5F && jitter.x < 0.5F && jitter.y > -0.5F && jitter.y < 0.5F, "jitter stays inside the pixel");
        mean.x += jitter.x / 16.0F;
        mean.y += jitter.y / 16.0F;
    }
    require(std::abs(mean.x) < 0.07F && std::abs(mean.y) < 0.07F, "16-frame Halton jitter is approximately zero-mean");
    require(halton_jitter_pixels(3U).x == halton_jitter_pixels(19U).x, "sequence repeats with length 16");
}

void test_projection_jitter_shift()
{
    const Mat4 view = look_at_rh({0.0F, 1.0F, 5.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
    const Mat4 projection = perspective_rh(0.9F, static_cast<float>(kWidth) / static_cast<float>(kHeight), 0.1F, 100.0F);
    const Vec2 jitter{0.25F, -0.375F};
    const Mat4 jittered = apply_projection_jitter(projection, jitter, kWidth, kHeight);
    for (const Vec3 point : {Vec3{0.0F, 0.0F, 0.0F}, Vec3{1.5F, -0.5F, -3.0F}, Vec3{-2.0F, 2.0F, 1.0F}})
    {
        const std::optional<Vec2> base = project_to_pixel(multiply(projection, view), point, kWidth, kHeight);
        const std::optional<Vec2> shifted = project_to_pixel(multiply(jittered, view), point, kWidth, kHeight);
        require(base.has_value() && shifted.has_value(), "test points are in front of the camera");
        require_near(shifted->x - base->x, jitter.x, 1.0e-3F, "jitter shifts x by exactly the requested pixels at any depth");
        require_near(shifted->y - base->y, jitter.y, 1.0e-3F, "jitter shifts y by exactly the requested pixels at any depth");
    }
}

void test_motion_vectors()
{
    const Mat4 still = test_view_projection({0.0F, 1.0F, 5.0F});
    const std::optional<Vec2> zero = motion_vector_pixels(still, still, {0.3F, 0.2F, 0.0F}, {0.3F, 0.2F, 0.0F}, kWidth, kHeight);
    require(zero.has_value(), "static point projects");
    require_near(zero->x, 0.0F, 1.0e-5F, "static camera + static point has zero motion (x)");
    require_near(zero->y, 0.0F, 1.0e-5F, "static camera + static point has zero motion (y)");

    // Rigid object moving +X in world space appears to have come from the left: motion = prev - cur < 0.
    const std::optional<Vec2> object = motion_vector_pixels(still, still, {0.5F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, kWidth, kHeight);
    require(object.has_value() && object->x < 0.0F, "motion is previous minus current pixel position");

    // Camera translation: same world point, different view-projections.
    const Mat4 moved = test_view_projection({0.5F, 1.0F, 5.0F});
    const Vec3 world{0.0F, 0.0F, 0.0F};
    const std::optional<Vec2> camera = motion_vector_pixels(moved, still, world, world, kWidth, kHeight);
    const std::optional<Vec2> current = project_to_pixel(moved, world, kWidth, kHeight);
    const std::optional<Vec2> previous = project_to_pixel(still, world, kWidth, kHeight);
    require(camera.has_value() && current.has_value() && previous.has_value(), "camera-motion projections valid");
    require_near(camera->x, previous->x - current->x, 1.0e-4F, "camera motion matches reprojection (x)");
    require_near(camera->y, previous->y - current->y, 1.0e-4F, "camera motion matches reprojection (y)");
    require(classify_motion(true, false) == MotionClass::camera_only, "camera-only classification");
    require(classify_motion(false, true) == MotionClass::rigid_object, "rigid-object classification");
    require(classify_motion(false, false) == MotionClass::static_surface, "static classification");
}

void test_temporal_frame_rotation()
{
    TemporalFrameData data;
    CameraFrame first;
    first.position = {1.0F, 2.0F, 3.0F};
    data.begin_frame(first, false);
    require(!data.history_valid(), "first frame has no history");
    require_near(data.previous().position.x, 1.0F, 0.0F, "without history previous equals current");
    CameraFrame second;
    second.position = {4.0F, 5.0F, 6.0F};
    data.begin_frame(second, true);
    require(data.history_valid(), "second frame has history");
    require_near(data.previous().position.x, 1.0F, 0.0F, "previous frame rotated");
    require_near(data.current().position.x, 4.0F, 0.0F, "current frame stored");
}

void test_reset_policies()
{
    require(!invalidates_accumulation(ResetReason::exposure_changed), "exposure never invalidates accumulation");
    require(!invalidates_temporal_history(ResetReason::exposure_changed), "exposure never invalidates temporal history");
    require(!invalidates_upscaler_history(ResetReason::exposure_changed), "exposure never invalidates upscaler history");
    require(invalidates_accumulation(ResetReason::camera_moved), "camera motion invalidates progressive accumulation");
    require(!invalidates_temporal_history(ResetReason::camera_moved), "camera motion is reprojected, not reset");
    require(invalidates_temporal_history(ResetReason::camera_cut), "camera cut resets temporal history");
    require(invalidates_upscaler_history(ResetReason::resize), "resize resets upscaler history");
    require(invalidates_accumulation(ResetReason::seed_changed), "seed change restarts accumulation");
    require(describe_reset_reasons(ResetReason::resize | ResetReason::camera_cut).find("resize") != std::string::npos,
            "reasons are human readable");
}

void test_revision_tracker()
{
    RevisionTracker tracker;
    RenderRevisions revisions;
    require(any(tracker.observe(revisions) & ResetReason::initial), "first observation reports initial");
    require(!any(tracker.observe(revisions)), "unchanged revisions report nothing");
    revisions.camera = 1U;
    revisions.exposure = 1U;
    const ResetReason reasons = tracker.observe(revisions);
    require(any(reasons & ResetReason::camera_moved), "camera revision reported");
    require(any(reasons & ResetReason::exposure_changed), "exposure revision reported");
    tracker.force(ResetReason::user_requested);
    require(any(tracker.observe(revisions) & ResetReason::user_requested), "forced reasons are reported once");
    require(!any(tracker.observe(revisions)), "forced reasons do not persist");
}

void test_view_constants_layout()
{
    require(sizeof(GpuViewConstants) == 704U, "GpuViewConstants must stay 704 bytes (HLSL ViewConstantsData mirror)");
    require(sizeof(RasterTemporalDrawConstants) == 80U, "RasterTemporalDrawConstants must stay 80 bytes");
}
}

int main()
{
    return daedalus::tests::run({
        {"Halton sequence and jitter", test_halton_sequence},
        {"projection jitter is an exact pixel shift", test_projection_jitter_shift},
        {"motion vectors", test_motion_vectors},
        {"temporal frame rotation", test_temporal_frame_rotation},
        {"history reset policies", test_reset_policies},
        {"revision tracker", test_revision_tracker},
        {"view constant layout", test_view_constants_layout}});
}
