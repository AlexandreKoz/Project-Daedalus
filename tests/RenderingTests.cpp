#include "TestHarness.h"
#include "rendering/CampaignC2Math.h"
#include "rendering/PbrMath.h"
#include "rendering/RasterPreparation.h"
#include "rendering/RasterShaderContract.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
using namespace daedalus;
using namespace daedalus::tests;

void require_vec3_finite_nonnegative(Vec3 value, std::string_view message)
{
    require(finite(value), message);
    require(value.x >= 0.0F && value.y >= 0.0F && value.z >= 0.0F, message);
}

CanonicalScene make_minimal_scene()
{
    CanonicalScene scene;
    Primitive primitive;
    primitive.vertices = {
        {{-1.0F, -1.0F, 0.0F}},
        {{1.0F, -1.0F, 0.0F}},
        {{0.0F, 1.0F, 0.0F}}};
    primitive.indices = {0, 1, 2};
    primitive.has_normals = true;
    primitive.bounds.minimum = {-1.0F, -1.0F, 0.0F};
    primitive.bounds.maximum = {1.0F, 1.0F, 0.0F};
    scene.primitives.push_back(primitive);
    scene.meshes.resize(1);
    scene.meshes[0].primitives.push_back(PrimitiveId(0));
    scene.nodes.resize(1);
    scene.nodes[0].mesh = MeshId(0);
    scene.scenes.resize(1);
    scene.scenes[0].roots.push_back(NodeId(0));
    scene.selected_scene = SceneDefinitionId(0);
    return scene;
}

void test_srgb_reference_values()
{
    require_near(srgb_to_linear(0.0F), 0.0F, 1.0e-7F, "sRGB black");
    require_near(srgb_to_linear(0.04045F), 0.0031308F, 2.0e-7F, "sRGB linear segment boundary");
    require_near(srgb_to_linear(0.5F), 0.21404114F, 2.0e-6F, "sRGB half reference");
    require_near(srgb_to_linear(1.0F), 1.0F, 1.0e-7F, "sRGB white");
    require_near(linear_to_srgb(0.21404114F), 0.5F, 2.0e-6F, "linear half reference inverse");
}

void test_texture_slot_color_space()
{
    require(texture_slot_color_space(MaterialTextureSlot::base_color) == ColorSpaceIntent::srgb, "base color must be sRGB");
    require(texture_slot_color_space(MaterialTextureSlot::emissive) == ColorSpaceIntent::srgb, "emissive must be sRGB");
    require(texture_slot_color_space(MaterialTextureSlot::metallic_roughness) == ColorSpaceIntent::linear, "metallic-roughness must be linear");
    require(texture_slot_color_space(MaterialTextureSlot::normal) == ColorSpaceIntent::linear, "normal must be linear");
    require(texture_slot_color_space(MaterialTextureSlot::occlusion) == ColorSpaceIntent::linear, "occlusion must be linear");
}

void test_material_factor_application()
{
    const Vec4 base = apply_base_color({0.8F, 0.5F, 0.25F, 0.5F}, {0.5F, 0.8F, 0.4F, 0.8F}, {0.5F, 0.5F, 1.0F, 0.5F});
    require_near(base.x, 0.2F, 1.0e-6F, "base factor R");
    require_near(base.y, 0.2F, 1.0e-6F, "base factor G");
    require_near(base.z, 0.1F, 1.0e-6F, "base factor B");
    require_near(base.w, 0.2F, 1.0e-6F, "base alpha product");

    const MetallicRoughnessSample mr = apply_metallic_roughness({0.1F, 0.25F, 0.75F, 1.0F}, 0.8F, 0.4F);
    require_near(mr.roughness, 0.1F, 1.0e-6F, "roughness must use G times factor");
    require_near(mr.metallic, 0.6F, 1.0e-6F, "metallic must use B times factor");

    const Vec3 emissive = apply_emissive({2.0F, 0.5F, 1.0F}, {0.25F, 0.5F, 0.75F});
    require_near(emissive.x, 0.5F, 1.0e-6F, "emissive factor R");
    require_near(emissive.y, 0.25F, 1.0e-6F, "emissive factor G");
    require_near(emissive.z, 0.75F, 1.0e-6F, "emissive factor B");
    require_near(apply_occlusion(0.25F, 0.5F), 0.625F, 1.0e-6F, "AO strength must lerp from unoccluded to sampled R");
}

void test_alpha_semantics()
{
    require_near(material_alpha(0.5F, 0.8F, 0.5F), 0.2F, 1.0e-6F, "straight alpha factor product");
    require(!alpha_test_passes(AlphaMode::mask, 0.49F, 0.5F), "alpha below cutoff must fail mask");
    require(alpha_test_passes(AlphaMode::mask, 0.5F, 0.5F), "alpha at cutoff must pass mask");
    require(alpha_test_passes(AlphaMode::blend, 0.0F, 0.5F), "blend does not alpha-test");
}

void test_tangent_handedness_and_orthonormalization()
{
    require_near(tangent_handedness(1.0F, false, false), 1.0F, 0.0F, "positive tangent frame");
    require_near(tangent_handedness(-1.0F, false, false), -1.0F, 0.0F, "source tangent w");
    require_near(tangent_handedness(-1.0F, true, false), 1.0F, 0.0F, "negative determinant must flip source handedness");
    require_near(tangent_handedness(1.0F, false, true), -1.0F, 0.0F, "back face must flip tangent frame");

    // A two-sided back face reverses N and the handedness sign, not the authored UV tangent.
    // This keeps B aligned with the same UV derivative direction on both faces.
    const Vec3 front_normal{0.0F, 0.0F, 1.0F};
    const Vec3 back_normal{0.0F, 0.0F, -1.0F};
    const Vec3 uv_tangent{1.0F, 0.0F, 0.0F};
    const Vec3 front_bitangent = cross(front_normal, uv_tangent) * tangent_handedness(1.0F, false, false);
    const Vec3 back_bitangent = cross(back_normal, uv_tangent) * tangent_handedness(1.0F, false, true);
    require_near(back_bitangent.x, front_bitangent.x, 1.0e-6F, "back-face bitangent x must preserve UV derivative");
    require_near(back_bitangent.y, front_bitangent.y, 1.0e-6F, "back-face bitangent y must preserve UV derivative");
    require_near(back_bitangent.z, front_bitangent.z, 1.0e-6F, "back-face bitangent z must preserve UV derivative");

    const auto tangent = orthonormalize_tangent({0.0F, 0.0F, 1.0F}, {2.0F, 0.0F, 1.0F});
    require(tangent.has_value(), "valid projected tangent must survive");
    require_near(dot(*tangent, {0.0F, 0.0F, 1.0F}), 0.0F, 1.0e-6F, "T must be orthogonal to N");
    require_near(length(*tangent), 1.0F, 1.0e-6F, "T must be normalized");
    require(!orthonormalize_tangent({0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 2.0F}).has_value(), "parallel tangent must not invent a basis");
}

void test_brdf_reference_cases()
{
    const Vec3 n{0.0F, 0.0F, 1.0F};
    const Vec3 v = n;
    const Vec3 l = n;
    const BrdfEvaluation dielectric = evaluate_metallic_roughness_brdf({1.0F, 1.0F, 1.0F}, 0.0F, 1.0F, n, v, l);
    require_near(dielectric.diffuse.x, 0.96F / 3.14159265358979323846F, 2.0e-5F, "dielectric diffuse at normal incidence");
    require_near(dielectric.specular.x, 0.01F / 3.14159265358979323846F, 2.0e-5F, "dielectric GGX specular at normal incidence roughness one");
    require_vec3_finite_nonnegative(dielectric.total, "dielectric BRDF must be finite and nonnegative");

    const BrdfEvaluation metal = evaluate_metallic_roughness_brdf({0.8F, 0.2F, 0.1F}, 1.0F, 0.5F, n, v, l);
    require_near(metal.diffuse.x, 0.0F, 1.0e-7F, "pure metal has no diffuse lobe");
    require_vec3_finite_nonnegative(metal.total, "metal BRDF must be finite");

    const BrdfEvaluation mixed = evaluate_metallic_roughness_brdf({0.8F, 0.2F, 0.1F}, 0.5F, 0.2F, n, v, l);
    require_vec3_finite_nonnegative(mixed.total, "mixed metal BRDF must be finite");

    for (const float roughness : std::array{0.0F, 1.0e-8F, 0.045F, 0.5F, 1.0F})
    {
        const BrdfEvaluation value = evaluate_metallic_roughness_brdf({0.5F, 0.6F, 0.7F}, 0.3F, roughness, n, v, l);
        require_vec3_finite_nonnegative(value.total, "roughness extremes must remain finite");
    }

    const Vec3 grazing = normalize({1.0F, 0.0F, 0.01F});
    require_vec3_finite_nonnegative(evaluate_metallic_roughness_brdf({0.5F, 0.5F, 0.5F}, 0.0F, 0.4F, n, grazing, l).total,
                                    "grazing BRDF must remain finite");
    const BrdfEvaluation back_light = evaluate_metallic_roughness_brdf({1.0F, 1.0F, 1.0F}, 0.0F, 0.5F, n, v, {0.0F, 0.0F, -1.0F});
    require_near(back_light.total.x, 0.0F, 0.0F, "negative N.L has zero direct contribution");
}

void test_exposure_and_tone_mapping()
{
    require_near(exposure_multiplier(0.0F), 1.0F, 1.0e-7F, "EV0 multiplier");
    require_near(exposure_multiplier(1.0F), 2.0F, 1.0e-7F, "EV+1 multiplier");
    require_near(exposure_multiplier(-2.0F), 0.25F, 1.0e-7F, "EV-2 multiplier");
    float previous = -1.0F;
    for (int index = 0; index <= 1000; ++index)
    {
        const float x = static_cast<float>(index) * 0.02F;
        const float y = aces_fitted(x);
        require(std::isfinite(y), "tone map must remain finite");
        require(y + 1.0e-7F >= previous, "tone map must be monotonic over positive validation range");
        previous = y;
    }
    require(std::isfinite(aces_fitted(std::numeric_limits<float>::max())), "tone map must remain finite for huge finite input");
    require_near(aces_fitted(std::numeric_limits<float>::infinity()), 1.0F, 0.0F, "positive infinity maps to finite white");
}

void test_light_transformation_and_limits()
{
    CanonicalScene scene;
    scene.lights.resize(1);
    scene.lights[0].type = LightType::spot;
    scene.lights[0].inner_cone_angle = 0.25F;
    scene.lights[0].outer_cone_angle = 0.5F;
    scene.nodes.resize(1);
    scene.nodes[0].light = LightId(0);
    Mat4 world = identity_matrix();
    world.at(0, 0) = 0.0F; world.at(1, 0) = 0.0F; world.at(2, 0) = 1.0F;
    world.at(0, 1) = 0.0F; world.at(1, 1) = 1.0F; world.at(2, 1) = 0.0F;
    world.at(0, 2) = -1.0F; world.at(1, 2) = 0.0F; world.at(2, 2) = 0.0F;
    world.at(0, 3) = 4.0F; world.at(1, 3) = 5.0F; world.at(2, 3) = 6.0F;
    scene.nodes[0].world_transform = world;
    scene.scenes.resize(1);
    scene.scenes[0].roots.push_back(NodeId(0));
    scene.selected_scene = SceneDefinitionId(0);

    const auto prepared = prepare_punctual_lights(scene);
    require(prepared.size() == 1, "one light must prepare");
    require_near(prepared[0].position.x, 4.0F, 1.0e-6F, "light world position x");
    require_near(prepared[0].position.y, 5.0F, 1.0e-6F, "light world position y");
    require_near(prepared[0].position.z, 6.0F, 1.0e-6F, "light world position z");
    require_near(prepared[0].direction.x, 1.0F, 1.0e-6F, "glTF local -Z must transform to emitted world direction");
    require_near(length(prepared[0].direction), 1.0F, 1.0e-6F, "prepared light direction must be normalized");

    require_throws<std::invalid_argument>([&] { static_cast<void>(prepare_punctual_lights(scene, 0)); }, "zero light capacity must reject");

    CanonicalScene boundary;
    boundary.scenes.resize(1);
    boundary.selected_scene = SceneDefinitionId(0);
    boundary.lights.resize(kMaximumPunctualLights + 1U);
    boundary.nodes.resize(kMaximumPunctualLights + 1U);
    for (std::size_t index = 0; index < boundary.nodes.size(); ++index)
    {
        boundary.nodes[index].light = LightId(static_cast<std::uint32_t>(index));
        boundary.scenes[0].roots.push_back(NodeId(static_cast<std::uint32_t>(index)));
    }
    boundary.scenes[0].roots.pop_back();
    require(prepare_punctual_lights(boundary).size() == kMaximumPunctualLights,
            "the documented 32-light boundary must be accepted exactly");
    boundary.scenes[0].roots.push_back(NodeId(static_cast<std::uint32_t>(kMaximumPunctualLights)));
    require_throws<std::runtime_error>([&] { static_cast<void>(prepare_punctual_lights(boundary)); },
                                       "the 33rd light must fail rather than be silently discarded");
}

void test_punctual_attenuation()
{
    require_near(point_distance_attenuation(2.0F, std::nullopt), 0.25F, 1.0e-7F, "inverse square at distance two");
    const float with_range = point_distance_attenuation(2.0F, 4.0F);
    require(with_range > 0.0F && with_range < 0.25F, "range window must smoothly reduce in-range attenuation");
    require_near(point_distance_attenuation(4.0F, 4.0F), 0.0F, 0.0F, "range endpoint must be zero");
    require_near(point_distance_attenuation(0.0F, std::nullopt), 0.0F, 0.0F, "zero distance is guarded");

    const float inner = std::cos(0.25F);
    const float outer = std::cos(0.5F);
    require_near(spot_cone_attenuation(1.0F, inner, outer), 1.0F, 0.0F, "spot axis full intensity");
    require_near(spot_cone_attenuation(outer - 0.01F, inner, outer), 0.0F, 0.0F, "outside spot cone zero");
    const float midpoint = spot_cone_attenuation((inner + outer) * 0.5F, inner, outer);
    require(midpoint > 0.0F && midpoint < 1.0F, "spot penumbra must interpolate");
}

void test_shader_contract_layout()
{
    require(sizeof(RasterFrameConstants) == 176, "frame ABI size");
    require(offsetof(RasterFrameConstants, diagnostic_mode) == 160, "frame diagnostic offset");
    require(sizeof(RasterDrawConstants) == 224, "draw ABI size");
    require(offsetof(RasterDrawConstants, flags) == 176, "draw flags offset");
    require(offsetof(RasterDrawConstants, world_handedness) == 208, "draw handedness offset");
    require(sizeof(RasterLightGpu) == 64, "light ABI size");
    require(offsetof(RasterLightGpu, type) == 48, "light type offset");
    require(sizeof(RasterLightConstants) == 64U * kMaximumPunctualLights, "bounded light ABI array");
}

void test_default_material_and_material_id_stability()
{
    require(stable_material_diagnostic_id(MaterialId{}) == kDefaultMaterialDiagnosticId, "default sentinel diagnostic ID");
    require(stable_material_diagnostic_id(MaterialId(0)) == 1U, "source material zero must remain distinct from default material");
    require(stable_material_diagnostic_id(MaterialId(7)) == 8U, "source material ID encoding must be stable");

    CanonicalScene scene = make_minimal_scene();
    scene.default_material.base_color_factor = {0.1F, 0.2F, 0.3F, 1.0F};
    scene.materials.resize(1);
    scene.materials[0].base_color_factor = {0.9F, 0.8F, 0.7F, 1.0F};
    scene.primitives.push_back(scene.primitives[0]);
    scene.primitives[1].material = MaterialId(0);
    scene.meshes[0].primitives.push_back(PrimitiveId(1));

    const auto draws = prepare_raster_draws(scene);
    require(draws.size() == 2, "two draws must prepare");
    require(draws[0].uses_default_material && draws[0].material_diagnostic_id == 0U, "omitted material uses canonical default");
    require(!draws[1].uses_default_material && draws[1].material_diagnostic_id == 1U, "source material 0 preserved");
    require_near(draws[0].base_color_factor.x, 0.1F, 1.0e-6F, "default material factor retained");
    require_near(draws[1].base_color_factor.x, 0.9F, 1.0e-6F, "source material zero factor retained");
}

void test_normal_map_missing_tangent_policy()
{
    CanonicalScene scene = make_minimal_scene();
    scene.images.resize(1);
    scene.textures.resize(1);
    scene.textures[0].image = ImageId(0);
    scene.materials.resize(1);
    scene.materials[0].normal_texture = TextureReference{TextureId(0), 0, 1.0F};
    scene.primitives[0].material = MaterialId(0);
    scene.primitives[0].has_texcoord0 = true;
    scene.primitives[0].has_tangents = false;
    const auto draws = prepare_raster_draws(scene);
    require(draws[0].normal.texture.has_value(), "normal slot remains visible for diagnostics");
    require(!draws[0].normal_mapping_enabled, "normal mapping must be explicitly disabled without source tangents");
}

void test_deterministic_draw_partition_and_sort()
{
    std::vector<PreparedRasterDraw> draws(5);
    draws[0].alpha_mode = AlphaMode::opaque; draws[0].primitive_index = 10;
    draws[1].alpha_mode = AlphaMode::blend; draws[1].world_bounds_center = {0.0F, 0.0F, 2.0F}; draws[1].material_diagnostic_id = 2; draws[1].primitive_index = 11;
    draws[2].alpha_mode = AlphaMode::mask; draws[2].primitive_index = 12;
    draws[3].alpha_mode = AlphaMode::blend; draws[3].world_bounds_center = {0.0F, 0.0F, 8.0F}; draws[3].material_diagnostic_id = 1; draws[3].primitive_index = 13;
    draws[4].alpha_mode = AlphaMode::blend; draws[4].world_bounds_center = {0.0F, 0.0F, 8.0F}; draws[4].material_diagnostic_id = 3; draws[4].primitive_index = 14;
    const auto order = build_raster_draw_order(draws, {});
    const std::vector<std::size_t> expected{0, 2, 3, 4, 1};
    require(order == expected, "opaque/mask first and transparent deterministic back-to-front order");
    require(build_raster_draw_order(draws, {}) == order, "sort must be deterministic across calls");
}
}


void test_c2_environment_contracts()
{
    require_near(roughness_to_environment_lod(0.0F, 6U), 0.0F, 1.0e-6F, "roughness zero lod");
    require_near(roughness_to_environment_lod(1.0F, 6U), 5.0F, 1.0e-6F, "roughness one lod");
    require_near(roughness_to_environment_lod(0.5F, 6U), 2.5F, 1.0e-6F, "roughness half lod");
    const Vec3 sky = procedural_environment_radiance({0.0F, 1.0F, 0.0F});
    const Vec3 ground = procedural_environment_radiance({0.0F, -1.0F, 0.0F});
    require_vec3_finite_nonnegative(sky, "procedural sky finite");
    require_vec3_finite_nonnegative(ground, "procedural ground finite");
    require(sky.z > ground.z, "sky must carry more blue radiance than ground");
    const Vec3 diffuse = procedural_diffuse_irradiance({0.0F, 1.0F, 0.0F});
    const Vec3 sharp = procedural_prefiltered_specular({0.0F, 1.0F, 0.0F}, 0.0F);
    const Vec3 rough = procedural_prefiltered_specular({0.0F, 1.0F, 0.0F}, 1.0F);
    require_vec3_finite_nonnegative(diffuse, "diffuse irradiance finite");
    require_vec3_finite_nonnegative(sharp, "sharp environment finite");
    require_vec3_finite_nonnegative(rough, "rough environment finite");
    const Vec2 brdf = environment_brdf_approximation(0.5F, 0.5F);
    require(std::isfinite(brdf.x) && std::isfinite(brdf.y), "environment BRDF approximation finite");
}

void test_c2_shadow_projection()
{
    Aabb bounds;
    expand(bounds, Vec3{-2.0F, -1.0F, -3.0F});
    expand(bounds, Vec3{2.0F, 3.0F, 1.0F});
    PreparedPunctualLight point;
    point.type = LightType::point;
    PreparedPunctualLight directional;
    directional.type = LightType::directional;
    directional.direction = normalize(Vec3{0.2F, -1.0F, -0.3F});
    const std::array lights{point, directional};
    const ShadowProjection projection = build_shadow_projection(bounds, lights);
    require(projection.enabled, "directional shadow projection should be selected");
    require(projection.light_index == 1U, "first supported shadow light must be deterministic");
    require(projection.light_type == LightType::directional, "directional shadow type");
    require(finite(projection.view_projection), "directional shadow matrix finite");

    PreparedPunctualLight spot;
    spot.type = LightType::spot;
    spot.position = {0.0F, 2.0F, 4.0F};
    spot.direction = normalize(Vec3{0.0F, -0.25F, -1.0F});
    spot.range = 10.0F;
    spot.outer_cone_cos = std::cos(0.55F);
    const std::array spot_only{spot};
    const ShadowProjection spot_projection = build_shadow_projection(bounds, spot_only);
    require(spot_projection.enabled, "spot shadow projection should be selected");
    require(finite(spot_projection.view_projection), "spot shadow matrix finite");
}

void test_c2_image_metrics()
{
    const std::array<Vec3, 2> reference{{{0.0F, 0.5F, 1.0F}, {0.25F, 0.25F, 0.25F}}};
    const auto identical = compare_linear_rgb(reference, reference, {});
    require(identical.pass, "identical images pass");
    require_near(identical.mean_absolute_error, 0.0F, 1.0e-8F, "identical MAE");

    auto candidate = reference;
    candidate[0].x = 0.2F;
    ImageComparisonThresholds strict;
    strict.mean_absolute_error = 0.001F;
    strict.root_mean_square_error = 0.001F;
    strict.maximum_absolute_error = 0.01F;
    strict.per_channel_threshold = 0.01F;
    strict.maximum_fraction_over_threshold = 0.0F;
    const auto changed = compare_linear_rgb(reference, candidate, strict);
    require(!changed.pass, "changed image must fail strict thresholds");
    require(changed.channels_over_threshold == 1U, "one changed channel counted");

    const std::array<std::byte, 8> rgba{
        std::byte{0x80}, std::byte{0x80}, std::byte{0x80}, std::byte{0xff},
        std::byte{0xff}, std::byte{0x00}, std::byte{0x00}, std::byte{0x7f}};
    const auto linear = rgba8_srgb_to_linear_rgb(rgba);
    require(linear.size() == 2U, "rgba conversion pixel count");
    require_near(linear[0].x, srgb_to_linear(128.0F / 255.0F), 1.0e-6F, "rgba conversion uses sRGB transfer");
    const std::string json = image_comparison_metrics_json(identical, {});
    require(json.find("\"status\": \"PASS\"") != std::string::npos, "metrics JSON status");
    require(json.find("linear-srgb-rgb") != std::string::npos, "metrics JSON domain");
}

int main()
{
    return daedalus::tests::run({
        {"sRGB reference values", test_srgb_reference_values},
        {"texture slot color space", test_texture_slot_color_space},
        {"material factor application", test_material_factor_application},
        {"alpha semantics", test_alpha_semantics},
        {"tangent handedness and TBN", test_tangent_handedness_and_orthonormalization},
        {"BRDF reference cases", test_brdf_reference_cases},
        {"exposure and tone map", test_exposure_and_tone_mapping},
        {"light transform and limits", test_light_transformation_and_limits},
        {"punctual attenuation", test_punctual_attenuation},
        {"shader contract layout", test_shader_contract_layout},
        {"default material ID stability", test_default_material_and_material_id_stability},
        {"normal map missing tangent policy", test_normal_map_missing_tangent_policy},
        {"draw partition and sorting", test_deterministic_draw_partition_and_sort},
        {"Campaign C2 environment contracts", test_c2_environment_contracts},
        {"Campaign C2 shadow projection", test_c2_shadow_projection},
        {"Campaign C2 image metrics", test_c2_image_metrics}});
}
