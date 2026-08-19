#include "TestHarness.h"
#include "scene/Math.h"
#include "scene/Scene.h"
#include "rendering/DiagnosticShaderContract.h"
#include "rendering/OrbitCamera.h"

#include <string>

namespace
{
using namespace daedalus;
using namespace daedalus::tests;

void test_matrix_composition()
{
    const Mat4 parent = translation_matrix({10.0F, 0.0F, 0.0F});
    const Mat4 local = scale_matrix({2.0F, 3.0F, 4.0F});
    const Vec3 transformed = transform_point(multiply(parent, local), {1.0F, 1.0F, 1.0F});
    require_near(transformed.x, 12.0F, 1.0e-5F, "parent * local x");
    require_near(transformed.y, 3.0F, 1.0e-5F, "parent * local y");
    require_near(transformed.z, 4.0F, 1.0e-5F, "parent * local z");
}

void test_negative_scale()
{
    require(determinant3x3(scale_matrix({-1.0F, 1.0F, 1.0F})) < 0.0F, "negative scale must flip determinant sign");
}

void test_hierarchy_propagation()
{
    CanonicalScene scene;
    scene.nodes.resize(2);
    scene.nodes[0].local_transform = translation_matrix({2.0F, 0.0F, 0.0F});
    scene.nodes[0].children.push_back(NodeId(1));
    scene.nodes[1].parent = NodeId(0);
    scene.nodes[1].local_transform = translation_matrix({0.0F, 3.0F, 0.0F});
    std::string error;
    require(propagate_world_transforms(scene, &error), "valid hierarchy must propagate");
    const Vec3 origin = transform_point(scene.nodes[1].world_transform, {});
    require_near(origin.x, 2.0F, 1.0e-5F, "world x");
    require_near(origin.y, 3.0F, 1.0e-5F, "world y");
}

void test_cycle_rejection()
{
    CanonicalScene scene;
    scene.nodes.resize(2);
    scene.nodes[0].children.push_back(NodeId(1));
    scene.nodes[0].parent = NodeId(1);
    scene.nodes[1].children.push_back(NodeId(0));
    scene.nodes[1].parent = NodeId(0);
    std::string error;
    require(!propagate_world_transforms(scene, &error), "cycle must be rejected");
}

void test_bounds_transform()
{
    Aabb bounds;
    expand(bounds, Vec3{-1.0F, -2.0F, -3.0F});
    expand(bounds, Vec3{1.0F, 2.0F, 3.0F});
    const Aabb transformed = transform_bounds(bounds, multiply(translation_matrix({5.0F, 0.0F, 0.0F}), scale_matrix({-2.0F, 1.0F, 1.0F})));
    require_near(transformed.minimum.x, 3.0F, 1.0e-5F, "negative scale bounds minimum");
    require_near(transformed.maximum.x, 7.0F, 1.0e-5F, "negative scale bounds maximum");
}


void test_orbit_camera_finite_fallback_and_input()
{
    OrbitCamera camera(Aabb{});
    Mat4 initial = camera.view_projection_matrix(16.0F / 9.0F);
    require(finite(initial), "empty-bounds camera must remain finite");
    OrbitInput input;
    input.orbit_delta_x = 40.0F;
    input.orbit_delta_y = -15.0F;
    input.pan_delta_x = 8.0F;
    input.pan_delta_y = -4.0F;
    input.zoom_delta = 2.0F;
    camera.apply_input(input, 1280, 720);
    require(finite(camera.view_projection_matrix(16.0F / 9.0F)), "camera input must preserve finite matrices");
    input = {};
    input.reset = true;
    camera.apply_input(input, 1280, 720);
    require(finite(camera.view_matrix()), "camera reset must preserve a valid view matrix");
}



void test_diagnostic_shader_contract_layout()
{
    require(sizeof(DiagnosticDrawConstants) == 240, "diagnostic draw constants must remain 240 bytes");
    require(alignof(DiagnosticDrawConstants) == 16, "diagnostic draw constants must remain 16-byte aligned");
    require(offsetof(DiagnosticDrawConstants, diagnostic_mode) == 208, "diagnostic mode ABI offset changed");
    require(offsetof(DiagnosticDrawConstants, texture_coord_set) == 220, "texture coordinate ABI offset changed");
    require(offsetof(DiagnosticDrawConstants, world_handedness) == 224, "handedness ABI offset changed");
    require(offsetof(DiagnosticDrawConstants, padding2) == 236, "explicit tail padding must remain part of the ABI");
}

void test_typed_identifiers()
{
    NodeId node(3);
    require(node.valid() && node.value() == 3, "typed node identifier must preserve value");
    require(!NodeId{}.valid(), "default identifier must be invalid");
}
}

int main()
{
    return daedalus::tests::run({
        {"matrix composition", test_matrix_composition},
        {"negative scale", test_negative_scale},
        {"hierarchy propagation", test_hierarchy_propagation},
        {"cycle rejection", test_cycle_rejection},
        {"bounds transform", test_bounds_transform},
        {"orbit camera", test_orbit_camera_finite_fallback_and_input},
        {"diagnostic shader contract layout", test_diagnostic_shader_contract_layout},
        {"typed identifiers", test_typed_identifiers}});
}
