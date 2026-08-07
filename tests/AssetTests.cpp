#include "TestHarness.h"
#include "assets/GltfImporter.h"
#include "core/Json.h"
#include "rendering/DiagnosticPreparation.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef DAEDALUS_FIXTURE_ROOT
#error DAEDALUS_FIXTURE_ROOT must be defined
#endif

namespace
{
using namespace daedalus;
using namespace daedalus::tests;
const std::filesystem::path kRoot = DAEDALUS_FIXTURE_ROOT;

ImportResult load(std::string_view relative, const ImportSettings& settings = {})
{
    return GltfImporter{}.import_file(kRoot / std::filesystem::path(relative), settings);
}

void test_minimal_glb()
{
    const ImportResult result = load("valid/minimal.glb");
    require(result.succeeded(), result.report.summary());
    require(result.report.counts.scenes == 1, "minimal GLB scene count");
    require(result.report.counts.nodes == 1, "minimal GLB node count");
    require(result.report.counts.primitives == 1, "minimal GLB primitive count");
    require(result.report.counts.vertices == 3, "minimal GLB vertex count");
    require(result.report.counts.indices == 3, "minimal GLB index count");
    require(!result.report.world_bounds.empty(), "minimal GLB bounds");
}

void test_external_scene()
{
    const ImportResult result = load("valid/external_scene.gltf");
    require(result.succeeded(), result.report.summary());
    require(result.report.counts.scenes == 2, "external scene count");
    require(result.report.counts.roots == 2, "external roots count");
    require(result.report.counts.primitives == 2, "multi primitive count");
    require(result.report.counts.materials == 2, "material count");
    require(result.report.counts.textures == 1 && result.report.counts.images == 1, "texture and image count");
    require(result.report.counts.cameras == 1 && result.report.counts.lights == 1, "camera and light count");
    require(result.scene.nodes[0].negative_determinant, "negative scale must be preserved");
    require(result.scene.primitives[0].has_normals && result.scene.primitives[0].has_texcoord0, "strided attributes must decode");
    require(result.scene.source.dependencies.size() == 2, "external dependencies must be recorded");
}

void test_scene_selection()
{
    ImportSettings settings;
    settings.scene_selector = "LightOnly";
    const ImportResult result = load("valid/external_scene.gltf", settings);
    require(result.succeeded(), "scene-name selection must succeed");
    require(result.scene.selected_scene.value() == 1, "selected scene ID");
    require(result.report.counts.roots == 1, "selected scene roots");
}

void test_embedded_image()
{
    const ImportResult result = load("valid/embedded_image.glb");
    require(result.succeeded(), result.report.summary());
    require(result.scene.images.size() == 1, "embedded image count");
    require(result.scene.images[0].width == 1 && result.scene.images[0].height == 1, "embedded image dimensions");
    require(result.scene.images[0].mime_type == "image/png", "embedded image MIME");
}


void test_jpeg_image()
{
    const ImportResult result = load("valid/jpeg_image.gltf");
    require(result.succeeded(), result.report.summary());
    require(result.scene.images.size() == 1, "JPEG image count");
    require(result.scene.images[0].width == 1 && result.scene.images[0].height == 1, "JPEG image dimensions");
    require(result.scene.images[0].components == 3, "JPEG source component count");
    require(result.scene.images[0].mime_type == "image/jpeg", "JPEG MIME");
}

void test_data_uri_and_normalized_attributes()
{
    const ImportResult result = load("valid/data_uri_scene.gltf");
    require(result.succeeded(), result.report.summary());
    require(result.scene.source.dependencies.empty(), "data URI fixture must not create external dependencies");
    require(result.scene.primitives.size() == 1, "data URI primitive count");
    const Primitive& primitive = result.scene.primitives[0];
    require(primitive.has_normals && primitive.has_tangents && primitive.has_texcoord0 && primitive.has_colors,
            "normalized attributes must be present");
    require_near(primitive.vertices[0].normal.z, 1.0F, 1.0e-5F, "signed byte normal normalization");
    require_near(primitive.vertices[1].tangent.w, -1.0F, 1.0e-4F, "signed short tangent handedness");
    require_near(primitive.vertices[1].color.y, 1.0F, 1.0e-5F, "unsigned byte RGB color normalization");
    require_near(primitive.vertices[1].color.w, 1.0F, 1.0e-5F, "VEC3 color alpha default");
    require_near(primitive.vertices[2].texcoord0.x, 32768.0F / 65535.0F, 1.0e-5F, "unsigned short UV normalization");
    require(result.scene.materials.size() == 1, "explicit material count");
    const Material& material = result.scene.materials[0];
    require(material.base_color_texture.has_value() && material.normal_texture.has_value() &&
            material.metallic_roughness_texture.has_value() && material.occlusion_texture.has_value() &&
            material.emissive_texture.has_value(), "all supported material texture references must translate");
    require(material.alpha_mode == AlphaMode::mask && material.double_sided, "alpha and double-sided metadata");
    require_near(material.normal_scale, 0.5F, 1.0e-6F, "normal scale");
    require_near(material.occlusion_strength, 0.6F, 1.0e-6F, "occlusion strength");
    require(result.scene.textures.size() == 2 && result.scene.images.size() == 2 && result.scene.samplers.size() == 2,
            "multiple data URI textures, images, and samplers must import");
    require(result.scene.samplers[0].wrap_s == WrapMode::clamp_to_edge &&
            result.scene.samplers[0].wrap_t == WrapMode::mirrored_repeat, "sampler wrap translation");
    require(result.scene.samplers[1].min_filter == FilterMode::linear_mipmap_linear &&
            result.scene.samplers[1].wrap_s == WrapMode::repeat, "second sampler translation");
    require(result.scene.textures[0].color_space == ColorSpaceIntent::srgb &&
            result.scene.textures[1].color_space == ColorSpaceIntent::linear, "per-slot texture colour-space classification");
    require(result.scene.cameras.size() == 1 && result.scene.cameras[0].type == CameraType::orthographic,
            "orthographic camera translation");
    require(std::find(result.report.extensions_required.begin(), result.report.extensions_required.end(), "KHR_mesh_quantization") !=
                result.report.extensions_required.end(),
            "required mesh quantization extension must be inventoried");
}

void test_determinism_and_location_independence()
{
    const ImportResult first = load("valid/external_scene.gltf");
    const ImportResult second = load("valid/external_scene.gltf");
    require(first.report.deterministic_asset_key == second.report.deterministic_asset_key, "same content/settings must produce same key");
    require(first.report.to_json() == second.report.to_json(), "report serialization must be stable");

    const std::filesystem::path temporary = std::filesystem::temp_directory_path() / "daedalus-location-independence";
    std::filesystem::remove_all(temporary);
    std::filesystem::create_directories(temporary);
    for (const char* file : {"external_scene.gltf", "external_scene.bin", "texture.png"})
        std::filesystem::copy_file(kRoot / "valid" / file, temporary / file, std::filesystem::copy_options::overwrite_existing);
    const ImportResult copied = GltfImporter{}.import_file(temporary / "external_scene.gltf");
    require(copied.succeeded(), "copied fixture must import");
    require(first.report.deterministic_asset_key == copied.report.deterministic_asset_key, "checkout location must not alter asset key");

    std::filesystem::copy_file(kRoot / "valid" / "texture_changed.png", temporary / "texture.png",
                               std::filesystem::copy_options::overwrite_existing);
    const ImportResult dependency_changed = GltfImporter{}.import_file(temporary / "external_scene.gltf");
    require(dependency_changed.succeeded(), "dimension-valid changed texture must still import");
    require(first.report.deterministic_asset_key != dependency_changed.report.deterministic_asset_key,
            "dependency content change must alter asset key");

    ImportSettings selected_settings;
    selected_settings.scene_selector = "LightOnly";
    const ImportResult settings_changed = load("valid/external_scene.gltf", selected_settings);
    require(settings_changed.succeeded(), "alternate scene selection must import");
    require(first.report.deterministic_asset_key != settings_changed.report.deterministic_asset_key,
            "relevant import settings must alter asset key");
    std::filesystem::remove_all(temporary);
}


void test_default_material_semantics()
{
    const ImportResult result = load("valid/material_default.gltf");
    require(result.succeeded(), result.report.summary());
    require(result.scene.materials.size() == 1, "source material indices must not be shifted by a synthesized default");
    require(result.scene.primitives.size() == 2, "default-material fixture primitive count");
    require(result.scene.primitives[0].material.valid() && result.scene.primitives[0].material.value() == 0,
            "explicit material 0 must remain material 0");
    require(!result.scene.primitives[1].material.valid(), "omitted material must use the canonical default sentinel");
    require_near(result.scene.materials[0].base_color_factor.x, 1.0F, 1.0e-6F, "explicit red material red channel");
    require_near(result.scene.materials[0].base_color_factor.y, 0.0F, 1.0e-6F, "explicit red material green channel");
    require_near(result.scene.default_material.base_color_factor.x, 1.0F, 1.0e-6F, "glTF default material factor");
    const auto draws = prepare_diagnostic_draws(result.scene);
    require(draws.size() == 2, "both primitives must prepare draws");
    require(!draws[0].uses_default_material && draws[1].uses_default_material,
            "runtime preparation must distinguish source and default materials");
}

void test_uv1_runtime_selection()
{
    const ImportResult result = load("valid/uv1_scene.gltf");
    require(result.succeeded(), result.report.summary());
    require(result.scene.primitives.size() == 1 && result.scene.primitives[0].has_texcoord0 && result.scene.primitives[0].has_texcoord1,
            "UV1 fixture must contain both UV sets");
    require(result.scene.materials[0].base_color_texture.has_value(), "UV1 fixture base texture");
    require(result.scene.materials[0].base_color_texture->texcoord_set == 1, "importer must preserve texCoord 1");
    const auto draws = prepare_diagnostic_draws(result.scene);
    require(draws.size() == 1, "UV1 fixture draw count");
    require(draws[0].texture_coord_set == 1 && draws[0].selected_texcoord_available,
            "runtime preparation must select available UV1 rather than silently substituting UV0");
    require_near(result.scene.primitives[0].vertices[0].texcoord0.x, 0.0F, 1.0e-6F, "UV0 control value");
    require_near(result.scene.primitives[0].vertices[1].texcoord1.x, 1.0F, 1.0e-6F, "UV1 independent value");
}

void test_instancing_and_tangent_preparation()
{
    const ImportResult result = load("valid/instanced_tangents.gltf");
    require(result.succeeded(), result.report.summary());
    require(result.scene.meshes.size() == 1 && result.scene.primitives.size() == 1 && result.scene.nodes.size() == 2,
            "fixture must retain one mesh with two instances");
    const auto draws = prepare_diagnostic_draws(result.scene);
    require(draws.size() == 2, "two mesh-bearing nodes must produce two draws");
    require(draws[0].primitive_index == draws[1].primitive_index, "instances must share the same canonical primitive");
    require_near(draws[0].world.at(0, 3), -1.0F, 1.0e-6F, "left instance translation");
    require_near(draws[1].world.at(0, 3), 1.0F, 1.0e-6F, "right instance translation");
    require(!draws[0].negative_determinant && draws[1].negative_determinant,
            "negative-scale instance must retain handedness");
    require(result.scene.primitives[0].has_tangents, "tangent fixture must import tangents");
    require_near(result.scene.primitives[0].vertices[1].tangent.w, -1.0F, 1.0e-6F,
                 "tangent handedness sign must survive import");
    require_near(result.report.world_bounds.minimum.x, -1.4F, 1.0e-5F, "instance bounds minimum");
    require_near(result.report.world_bounds.maximum.x, 1.4F, 1.0e-5F, "instance bounds maximum");
}

void test_explicit_repairs_and_decoded_images()
{
    const ImportResult repaired = load("valid/repaired_vectors.gltf");
    require(repaired.succeeded(), repaired.report.summary());
    require(repaired.report.status == ImportStatus::success_with_repairs, "slightly non-unit data must be reported as repaired");
    const auto normalized_count = std::count_if(repaired.report.diagnostics.begin(), repaired.report.diagnostics.end(), [](const Diagnostic& diagnostic)
    {
        return diagnostic.code == DiagnosticCode::attribute_normalized || diagnostic.code == DiagnosticCode::rotation_normalized;
    });
    require(normalized_count >= 3, "normal, tangent, and quaternion repairs must be explicit");
    require_near(length(repaired.scene.primitives[0].vertices[0].normal), 1.0F, 1.0e-6F, "repaired normal length");
    require_near(length({repaired.scene.primitives[0].vertices[0].tangent.x,
                         repaired.scene.primitives[0].vertices[0].tangent.y,
                         repaired.scene.primitives[0].vertices[0].tangent.z}), 1.0F, 1.0e-6F, "repaired tangent length");

    const ImportResult decoded = load("valid/data_uri_scene.gltf");
    require(decoded.succeeded(), decoded.report.summary());
    require(!decoded.scene.images.empty() && !decoded.scene.images[0].decoded_rgba8.empty(),
            "supported images must be fully decoded before import succeeds");
    require(decoded.scene.images[0].row_stride == static_cast<std::uint64_t>(decoded.scene.images[0].width) * 4ULL,
            "canonical decoded image row stride");
    require(decoded.report.resource_usage.encoded_image_bytes > 0 && decoded.report.resource_usage.decoded_image_bytes > 0,
            "report must expose encoded and decoded image accounting");
}

void test_cumulative_and_peak_budgets()
{
    const ImportResult baseline = load("valid/data_uri_scene.gltf");
    require(baseline.succeeded(), baseline.report.summary());
    const std::uint64_t retained = baseline.report.resource_usage.retained_bytes;
    const std::uint64_t peak = baseline.report.resource_usage.conservative_peak_bytes;
    require(retained > 1 && peak >= retained, "baseline budget counters must be meaningful");

    ImportSettings equal;
    equal.maximum_total_decoded_bytes = retained;
    equal.maximum_peak_bytes = peak;
    require(load("valid/data_uri_scene.gltf", equal).succeeded(), "budget boundary equal to observed use must pass");

    ImportSettings one_less_total = equal;
    one_less_total.maximum_total_decoded_bytes = retained - 1;
    const ImportResult total_failure = load("valid/data_uri_scene.gltf", one_less_total);
    require(total_failure.report.status == ImportStatus::resource_limit, "one-byte cumulative overage must fail as resource_limit");
    require(std::any_of(total_failure.report.diagnostics.begin(), total_failure.report.diagnostics.end(), [](const Diagnostic& diagnostic)
    {
        return diagnostic.code == DiagnosticCode::resource_budget_exceeded;
    }), "cumulative limit must identify resource_budget_exceeded");

    if (peak > retained)
    {
        ImportSettings one_less_peak = equal;
        one_less_peak.maximum_peak_bytes = peak - 1;
        const ImportResult peak_failure = load("valid/data_uri_scene.gltf", one_less_peak);
        require(peak_failure.report.status == ImportStatus::resource_limit, "one-byte peak overage must fail");
    }
}

void test_budget_categories_and_overflow()
{
    const auto require_budget_failure = [](const ImportResult& result, std::string_view location_fragment,
                                           std::string_view expected_fragment)
    {
        require(result.report.status == ImportStatus::resource_limit, "budget overage must use resource_limit status");
        const auto found = std::find_if(result.report.diagnostics.begin(), result.report.diagnostics.end(),
                                        [&](const Diagnostic& diagnostic)
        {
            return diagnostic.code == DiagnosticCode::resource_budget_exceeded &&
                   diagnostic.location.find(location_fragment) != std::string::npos &&
                   diagnostic.expected.find(expected_fragment) != std::string::npos;
        });
        require(found != result.report.diagnostics.end(),
                "budget diagnostic must identify the exceeded category and source location");
    };

    const ImportResult data_uri = load("valid/data_uri_scene.gltf");
    require(data_uri.succeeded(), data_uri.report.summary());
    ImportSettings buffer_preflight;
    buffer_preflight.maximum_total_decoded_bytes =
        data_uri.report.resource_usage.source_payload_bytes + data_uri.report.resource_usage.buffer_payload_bytes - 1U;
    buffer_preflight.maximum_peak_bytes = data_uri.report.resource_usage.conservative_peak_bytes;
    require_budget_failure(load("valid/data_uri_scene.gltf", buffer_preflight), "json.buffers[0]", "buffer_payload");

    ImportSettings encoded_image_preflight;
    encoded_image_preflight.maximum_total_decoded_bytes =
        data_uri.report.resource_usage.source_payload_bytes +
        data_uri.report.resource_usage.buffer_payload_bytes +
        data_uri.report.resource_usage.encoded_image_bytes - 1U;
    encoded_image_preflight.maximum_peak_bytes = data_uri.report.resource_usage.conservative_peak_bytes;
    require_budget_failure(load("valid/data_uri_scene.gltf", encoded_image_preflight), "json.images[1]", "encoded_image");

    const ImportResult one_image = load("valid/jpeg_image.gltf");
    require(one_image.succeeded(), one_image.report.summary());
    ImportSettings decoded_limit;
    decoded_limit.maximum_total_decoded_bytes =
        one_image.report.resource_usage.source_payload_bytes +
        one_image.report.resource_usage.buffer_payload_bytes +
        one_image.report.resource_usage.encoded_image_bytes;
    decoded_limit.maximum_peak_bytes = one_image.report.resource_usage.conservative_peak_bytes;
    require_budget_failure(load("valid/jpeg_image.gltf", decoded_limit), "json.images[0].decoded", "decoded_image");

    const ImportResult multiple_images = load("valid/data_uri_scene.gltf");
    require(multiple_images.succeeded(), multiple_images.report.summary());
    ImportSettings combined_limit;
    combined_limit.maximum_total_decoded_bytes =
        multiple_images.report.resource_usage.source_payload_bytes +
        multiple_images.report.resource_usage.buffer_payload_bytes +
        multiple_images.report.resource_usage.encoded_image_bytes +
        multiple_images.report.resource_usage.decoded_image_bytes - 1U;
    combined_limit.maximum_peak_bytes = multiple_images.report.resource_usage.conservative_peak_bytes;
    require_budget_failure(load("valid/data_uri_scene.gltf", combined_limit), "json.images[1].decoded", "decoded_image");

    const ImportResult geometry = load("valid/minimal.glb");
    require(geometry.succeeded(), geometry.report.summary());
    ImportSettings geometry_limit;
    geometry_limit.maximum_total_decoded_bytes =
        geometry.report.resource_usage.source_payload_bytes + geometry.report.resource_usage.buffer_payload_bytes;
    geometry_limit.maximum_peak_bytes = geometry.report.resource_usage.conservative_peak_bytes;
    require_budget_failure(load("valid/minimal.glb", geometry_limit), ".vertices", "canonical_geometry");

    const ImportResult overflow = load("invalid/budget_overflow.gltf");
    require(overflow.report.status == ImportStatus::invalid_source,
            "checked arithmetic overflow must reject without attempting allocation");
    require(std::any_of(overflow.report.diagnostics.begin(), overflow.report.diagnostics.end(), [](const Diagnostic& diagnostic)
    {
        return diagnostic.code == DiagnosticCode::invalid_buffer_range &&
               diagnostic.message.find("integer overflow") != std::string::npos;
    }), "overflow fixture must fail through checked arithmetic");
}

void test_declared_bounds_are_audited()
{
    const ImportResult result = load("valid/stale_bounds.gltf");
    require(result.succeeded(), result.report.summary());
    require(result.report.status == ImportStatus::success_with_repairs, "stale source bounds must be classified as a repair");
    const auto diagnostic = std::find_if(result.report.diagnostics.begin(), result.report.diagnostics.end(), [](const Diagnostic& value)
    {
        return value.code == DiagnosticCode::bounds_recomputed &&
               value.severity == DiagnosticSeverity::warning &&
               value.location.find("attributes.POSITION") != std::string::npos;
    });
    require(diagnostic != result.report.diagnostics.end(), "stale POSITION bounds must produce a structured warning");
    require_near(result.scene.primitives[0].bounds.minimum.x, -0.5F, 1.0e-6F, "decoded minimum x");
    require_near(result.scene.primitives[0].bounds.maximum.y, 0.5F, 1.0e-6F, "decoded maximum y");
}

void test_resource_limit_status()
{
    ImportSettings settings;
    settings.maximum_source_bytes = 1;
    const ImportResult result = load("valid/minimal.glb", settings);
    require(!result.succeeded(), "source limit must reject the fixture");
    require(result.report.status == ImportStatus::resource_limit, "source limit must use resource_limit status");
    require(!result.report.diagnostics.empty() && result.report.diagnostics.front().code == DiagnosticCode::source_too_large,
            "source limit must produce source_too_large diagnostic");
}

void test_report_json_is_parseable()
{
    const ImportResult result = load("valid/minimal.glb");
    const JsonValue report = parse_json(result.report.to_json());
    require(report.find("asset_key") != nullptr, "report must contain asset key");
    require(report.find("diagnostics") != nullptr, "report must contain diagnostics");
}

void test_invalid_fixtures()
{
    struct ExpectedFailure
    {
        std::string_view path;
        ImportStatus status;
        DiagnosticCode code;
    };
    const std::vector<ExpectedFailure> cases{
        {"invalid/malformed_json.gltf", ImportStatus::invalid_source, DiagnosticCode::malformed_json},
        {"invalid/corrupted_header.glb", ImportStatus::invalid_source, DiagnosticCode::invalid_glb_header},
        {"invalid/missing_buffer.gltf", ImportStatus::missing_dependency, DiagnosticCode::missing_dependency},
        {"invalid/missing_image.gltf", ImportStatus::missing_dependency, DiagnosticCode::missing_dependency},
        {"invalid/accessor_oob.gltf", ImportStatus::invalid_source, DiagnosticCode::invalid_accessor},
        {"invalid/invalid_stride.gltf", ImportStatus::invalid_source, DiagnosticCode::invalid_stride},
        {"invalid/index_oob.gltf", ImportStatus::invalid_source, DiagnosticCode::index_out_of_range},
        {"invalid/nonfinite_attribute.gltf", ImportStatus::invalid_source, DiagnosticCode::non_finite_data},
        {"invalid/unsupported_required_extension.gltf", ImportStatus::unsupported_feature, DiagnosticCode::unsupported_required_extension},
        {"invalid/unsupported_primitive_mode.gltf", ImportStatus::unsupported_feature, DiagnosticCode::unsupported_primitive_mode},
        {"invalid/invalid_parent.gltf", ImportStatus::invalid_source, DiagnosticCode::invalid_node_graph},
        {"invalid/unsupported_image.gltf", ImportStatus::unsupported_feature, DiagnosticCode::unsupported_image_encoding},
        {"invalid/truncated_png.gltf", ImportStatus::invalid_source, DiagnosticCode::invalid_image},
        {"invalid/invalid_base64.gltf", ImportStatus::invalid_source, DiagnosticCode::malformed_json},
        {"invalid/network_uri.gltf", ImportStatus::unsupported_feature, DiagnosticCode::unsafe_dependency_path},
        {"invalid/path_traversal.gltf", ImportStatus::invalid_source, DiagnosticCode::unsafe_dependency_path},
        {"invalid/material_out_of_range.gltf", ImportStatus::invalid_source, DiagnosticCode::invalid_reference},
        {"invalid/quantization_not_required.gltf", ImportStatus::invalid_source, DiagnosticCode::invalid_reference},
        {"invalid/missing_uv1.gltf", ImportStatus::invalid_source, DiagnosticCode::missing_texture_coordinate},
        {"invalid/corrupt_entropy_png.gltf", ImportStatus::invalid_source, DiagnosticCode::invalid_image},
        {"invalid/corrupt_entropy_jpeg.gltf", ImportStatus::invalid_source, DiagnosticCode::invalid_image},
        {"invalid/zero_normal.gltf", ImportStatus::invalid_source, DiagnosticCode::invalid_unit_vector},
        {"invalid/zero_tangent.gltf", ImportStatus::invalid_source, DiagnosticCode::invalid_unit_vector},
        {"invalid/zero_quaternion.gltf", ImportStatus::invalid_source, DiagnosticCode::invalid_unit_vector},
        {"invalid/budget_overflow.gltf", ImportStatus::invalid_source, DiagnosticCode::invalid_buffer_range},
    };
    for (const ExpectedFailure& expected : cases)
    {
        const ImportResult result = load(expected.path);
        require(!result.succeeded(), std::string(expected.path) + " must fail");
        require(result.report.status == expected.status,
                std::string(expected.path) + " status mismatch: " + std::string(to_string(result.report.status)));
        require(!result.report.diagnostics.empty(), std::string(expected.path) + " must produce diagnostics");
        const auto found = std::find_if(result.report.diagnostics.begin(), result.report.diagnostics.end(),
                                        [&](const Diagnostic& diagnostic) { return diagnostic.code == expected.code; });
        require(found != result.report.diagnostics.end(),
                std::string(expected.path) + " expected diagnostic code " + std::string(to_string(expected.code)));
    }
}
}

int main()
{
    return daedalus::tests::run({
        {"minimal GLB", test_minimal_glb},
        {"external scene", test_external_scene},
        {"scene selection", test_scene_selection},
        {"embedded image", test_embedded_image},
        {"JPEG image", test_jpeg_image},
        {"data URI and normalized attributes", test_data_uri_and_normalized_attributes},
        {"default material semantics", test_default_material_semantics},
        {"UV1 runtime selection", test_uv1_runtime_selection},
        {"instancing and tangent preparation", test_instancing_and_tangent_preparation},
        {"explicit repairs and decoded images", test_explicit_repairs_and_decoded_images},
        {"cumulative and peak budgets", test_cumulative_and_peak_budgets},
        {"budget categories and overflow", test_budget_categories_and_overflow},
        {"determinism and location independence", test_determinism_and_location_independence},
        {"declared bounds audit", test_declared_bounds_are_audited},
        {"resource limit status", test_resource_limit_status},
        {"report JSON", test_report_json_is_parseable},
        {"invalid fixtures", test_invalid_fixtures}});
}
