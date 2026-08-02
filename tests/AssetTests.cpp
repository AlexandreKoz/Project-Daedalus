#include "TestHarness.h"
#include "assets/GltfImporter.h"
#include "core/Json.h"

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
        {"determinism and location independence", test_determinism_and_location_independence},
        {"declared bounds audit", test_declared_bounds_are_audited},
        {"resource limit status", test_resource_limit_status},
        {"report JSON", test_report_json_is_parseable},
        {"invalid fixtures", test_invalid_fixtures}});
}
