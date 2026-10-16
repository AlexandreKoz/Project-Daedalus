#include "assets/ImportReport.h"

#include "core/Json.h"
#include "core/Version.h"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace daedalus
{
namespace
{
JsonValue vector_json(Vec3 value)
{
    return JsonValue::Array{JsonValue(value.x), JsonValue(value.y), JsonValue(value.z)};
}

JsonValue bounds_json(const Aabb& bounds)
{
    JsonValue::Object object;
    object.emplace("empty", bounds.empty());
    object.emplace("maximum", vector_json(bounds.empty() ? Vec3{} : bounds.maximum));
    object.emplace("minimum", vector_json(bounds.empty() ? Vec3{} : bounds.minimum));
    return object;
}

JsonValue string_array(const std::vector<std::string>& values)
{
    JsonValue::Array result;
    for (const std::string& value : values) result.emplace_back(value);
    return result;
}
}

std::string ImportReport::to_json() const
{
    JsonValue::Object root;
    root.emplace("asset_key", deterministic_asset_key);
    root.emplace("canonical_schema_version", canonical_schema_version);

    JsonValue::Object count_object;
    count_object.emplace("cameras", counts.cameras);
    count_object.emplace("images", counts.images);
    count_object.emplace("indices", counts.indices);
    count_object.emplace("lights", counts.lights);
    count_object.emplace("materials", counts.materials);
    count_object.emplace("meshes", counts.meshes);
    count_object.emplace("nodes", counts.nodes);
    count_object.emplace("primitives", counts.primitives);
    count_object.emplace("roots", counts.roots);
    count_object.emplace("samplers", counts.samplers);
    count_object.emplace("scenes", counts.scenes);
    count_object.emplace("textures", counts.textures);
    count_object.emplace("vertices", counts.vertices);
    root.emplace("counts", std::move(count_object));

    JsonValue::Object resource_usage_object;
    resource_usage_object.emplace("buffer_payload_bytes", resource_usage.buffer_payload_bytes);
    resource_usage_object.emplace("canonical_geometry_bytes", resource_usage.canonical_geometry_bytes);
    resource_usage_object.emplace("conservative_peak_bytes", resource_usage.conservative_peak_bytes);
    resource_usage_object.emplace("decoded_image_bytes", resource_usage.decoded_image_bytes);
    resource_usage_object.emplace("encoded_image_bytes", resource_usage.encoded_image_bytes);
    resource_usage_object.emplace("retained_bytes", resource_usage.retained_bytes);
    resource_usage_object.emplace("source_payload_bytes", resource_usage.source_payload_bytes);
    root.emplace("resource_usage", std::move(resource_usage_object));

    JsonValue::Array dependency_array;
    for (const DependencyRecord& dependency : dependencies)
    {
        JsonValue::Object object;
        object.emplace("byte_size", dependency.byte_size);
        object.emplace("path", dependency.normalized_relative_path);
        object.emplace("sha256", dependency.sha256);
        dependency_array.emplace_back(std::move(object));
    }
    root.emplace("dependencies", std::move(dependency_array));

    JsonValue::Array diagnostic_array;
    for (const Diagnostic& diagnostic : diagnostics)
    {
        JsonValue::Object object;
        object.emplace("code", std::string(to_string(diagnostic.code)));
        object.emplace("disposition", std::string(to_string(diagnostic.disposition)));
        object.emplace("expected", diagnostic.expected);
        object.emplace("location", diagnostic.location);
        object.emplace("message", diagnostic.message);
        object.emplace("observed", diagnostic.observed);
        object.emplace("severity", std::string(to_string(diagnostic.severity)));
        diagnostic_array.emplace_back(std::move(object));
    }
    root.emplace("diagnostics", std::move(diagnostic_array));

    JsonValue::Object extensions;
    extensions.emplace("ignored", string_array(extensions_ignored));
    extensions.emplace("rejected", string_array(extensions_rejected));
    extensions.emplace("required", string_array(extensions_required));
    extensions.emplace("supported", string_array(extensions_supported));
    extensions.emplace("used", string_array(extensions_used));
    root.emplace("extensions", std::move(extensions));
    root.emplace("import_settings", import_settings);
    root.emplace("local_bounds", bounds_json(local_bounds));
    JsonValue::Object provenance_object;
    for (const auto& [key, value] : provenance) provenance_object.emplace(key, value);
    root.emplace("provenance", std::move(provenance_object));
    root.emplace("report_schema_version", std::string(schema_version));
    root.emplace("selected_scene", selected_scene);
    root.emplace("source_display_name", source_display_name);
    root.emplace("source_format", source_format);
    root.emplace("source_sha256", source_sha256);
    root.emplace("status", std::string(to_string(status)));
    root.emplace("tool_version", tool_version);
    root.emplace("world_bounds", bounds_json(world_bounds));
    return serialize_json(root, true);
}

std::string ImportReport::summary() const
{
    std::uint64_t warnings = 0;
    std::uint64_t errors = 0;
    std::uint64_t repairs = 0;
    for (const Diagnostic& diagnostic : diagnostics)
    {
        if (diagnostic.severity == DiagnosticSeverity::warning) ++warnings;
        if (diagnostic.severity == DiagnosticSeverity::error) ++errors;
        if (diagnostic.disposition == DiagnosticDisposition::repaired || diagnostic.disposition == DiagnosticDisposition::defaulted) ++repairs;
    }
    std::ostringstream output;
    output << "Import " << to_string(status) << ": " << source_display_name << '\n'
           << "  scene='" << selected_scene << "' nodes=" << counts.nodes << " meshes=" << counts.meshes
           << " primitives=" << counts.primitives << " vertices=" << counts.vertices << " indices=" << counts.indices << '\n'
           << "  materials=" << counts.materials << " textures=" << counts.textures << " images=" << counts.images
           << " cameras=" << counts.cameras << " lights=" << counts.lights << '\n'
           << "  diagnostics: warnings=" << warnings << " errors=" << errors << " repairs/defaults=" << repairs << '\n'
           << "  source_sha256=" << source_sha256 << '\n'
           << "  asset_key=" << deterministic_asset_key << '\n';
    return output.str();
}

namespace
{
const JsonValue& member(const JsonValue& object, std::string_view key)
{
    const JsonValue* value = object.find(key);
    if (value == nullptr) throw std::runtime_error("import report is missing '" + std::string(key) + "'");
    return *value;
}

std::string text(const JsonValue& object, std::string_view key)
{
    return member(object, key).as_string();
}

std::uint64_t count(const JsonValue& object, std::string_view key)
{
    const double value = member(object, key).as_number();
    if (!(value >= 0.0)) throw std::runtime_error("import report field '" + std::string(key) + "' is negative");
    return static_cast<std::uint64_t>(value);
}

std::vector<std::string> strings(const JsonValue& object, std::string_view key)
{
    std::vector<std::string> result;
    for (const JsonValue& value : member(object, key).as_array()) result.push_back(value.as_string());
    return result;
}

Aabb bounds_from(const JsonValue& value)
{
    Aabb bounds;
    if (member(value, "empty").as_bool()) return bounds;
    const auto vector = [&](std::string_view key)
    {
        const JsonValue::Array& array = member(value, key).as_array();
        if (array.size() != 3U) throw std::runtime_error("import report bounds vector must have three components");
        return Vec3{static_cast<float>(array[0].as_number()), static_cast<float>(array[1].as_number()),
                    static_cast<float>(array[2].as_number())};
    };
    bounds.minimum = vector("minimum");
    bounds.maximum = vector("maximum");
    return bounds;
}

template <typename Enum>
Enum enum_from(std::string_view name, std::uint32_t last, std::string_view what)
{
    for (std::uint32_t index = 0; index <= last; ++index)
        if (to_string(static_cast<Enum>(index)) == name) return static_cast<Enum>(index);
    throw std::runtime_error("import report contains an unknown " + std::string(what) + " '" + std::string(name) + "'");
}
}

ImportReport import_report_from_json(std::string_view json)
{
    const JsonValue root = parse_json(json);
    if (text(root, "report_schema_version") != ImportReport::schema_version)
        throw std::runtime_error("import report schema version differs");
    ImportReport report;
    report.deterministic_asset_key = text(root, "asset_key");
    report.canonical_schema_version = text(root, "canonical_schema_version");
    const JsonValue& counts = member(root, "counts");
    report.counts.cameras = count(counts, "cameras");
    report.counts.images = count(counts, "images");
    report.counts.indices = count(counts, "indices");
    report.counts.lights = count(counts, "lights");
    report.counts.materials = count(counts, "materials");
    report.counts.meshes = count(counts, "meshes");
    report.counts.nodes = count(counts, "nodes");
    report.counts.primitives = count(counts, "primitives");
    report.counts.roots = count(counts, "roots");
    report.counts.samplers = count(counts, "samplers");
    report.counts.scenes = count(counts, "scenes");
    report.counts.textures = count(counts, "textures");
    report.counts.vertices = count(counts, "vertices");
    const JsonValue& usage = member(root, "resource_usage");
    report.resource_usage.buffer_payload_bytes = count(usage, "buffer_payload_bytes");
    report.resource_usage.canonical_geometry_bytes = count(usage, "canonical_geometry_bytes");
    report.resource_usage.conservative_peak_bytes = count(usage, "conservative_peak_bytes");
    report.resource_usage.decoded_image_bytes = count(usage, "decoded_image_bytes");
    report.resource_usage.encoded_image_bytes = count(usage, "encoded_image_bytes");
    report.resource_usage.retained_bytes = count(usage, "retained_bytes");
    report.resource_usage.source_payload_bytes = count(usage, "source_payload_bytes");
    for (const JsonValue& value : member(root, "dependencies").as_array())
        report.dependencies.push_back({text(value, "path"), text(value, "sha256"), count(value, "byte_size")});
    for (const JsonValue& value : member(root, "diagnostics").as_array())
    {
        Diagnostic diagnostic;
        diagnostic.code = enum_from<DiagnosticCode>(text(value, "code"), static_cast<std::uint32_t>(DiagnosticCode::malformed_source), "diagnostic code");
        diagnostic.disposition = enum_from<DiagnosticDisposition>(text(value, "disposition"),
                                                                  static_cast<std::uint32_t>(DiagnosticDisposition::observed), "disposition");
        diagnostic.severity = enum_from<DiagnosticSeverity>(text(value, "severity"), static_cast<std::uint32_t>(DiagnosticSeverity::error), "severity");
        diagnostic.expected = text(value, "expected");
        diagnostic.location = text(value, "location");
        diagnostic.message = text(value, "message");
        diagnostic.observed = text(value, "observed");
        report.diagnostics.push_back(std::move(diagnostic));
    }
    const JsonValue& extensions = member(root, "extensions");
    report.extensions_ignored = strings(extensions, "ignored");
    report.extensions_rejected = strings(extensions, "rejected");
    report.extensions_required = strings(extensions, "required");
    report.extensions_supported = strings(extensions, "supported");
    report.extensions_used = strings(extensions, "used");
    report.import_settings = text(root, "import_settings");
    report.local_bounds = bounds_from(member(root, "local_bounds"));
    for (const auto& [key, value] : member(root, "provenance").as_object()) report.provenance.emplace(key, value.as_string());
    report.selected_scene = text(root, "selected_scene");
    report.source_display_name = text(root, "source_display_name");
    report.source_format = text(root, "source_format");
    report.source_sha256 = text(root, "source_sha256");
    report.status = enum_from<ImportStatus>(text(root, "status"), static_cast<std::uint32_t>(ImportStatus::internal_error), "status");
    report.tool_version = text(root, "tool_version");
    report.world_bounds = bounds_from(member(root, "world_bounds"));
    return report;
}

ImportReport make_import_report(const CanonicalScene& scene,
                                ImportStatus status,
                                std::vector<Diagnostic> diagnostics,
                                std::string import_settings)
{
    ImportReport report;
    report.tool_version = std::string(kVersion);
    report.canonical_schema_version = std::string(CanonicalScene::schema_version);
    report.source_display_name = scene.source.display_name;
    report.source_format = scene.source.format;
    report.source_sha256 = scene.source.source_sha256;
    report.deterministic_asset_key = scene.source.deterministic_asset_key;
    report.import_settings = std::move(import_settings);
    report.status = status;
    report.dependencies = scene.source.dependencies;
    report.extensions_used = scene.source.extensions_used;
    report.extensions_required = scene.source.extensions_required;
    report.extensions_supported = {"KHR_lights_punctual", "KHR_materials_emissive_strength", "KHR_materials_ior",
                                   "KHR_materials_transmission", "KHR_materials_volume", "KHR_mesh_quantization"};
    report.diagnostics = std::move(diagnostics);
    sort_diagnostics(report.diagnostics);

    report.resource_usage = scene.source.resource_usage;
    report.counts.scenes = scene.scenes.size();
    report.counts.nodes = scene.nodes.size();
    report.counts.meshes = scene.meshes.size();
    report.counts.primitives = scene.primitives.size();
    report.counts.materials = scene.materials.size();
    report.counts.textures = scene.textures.size();
    report.counts.images = scene.images.size();
    report.counts.samplers = scene.samplers.size();
    report.counts.cameras = scene.cameras.size();
    report.counts.lights = scene.lights.size();
    for (const Primitive& primitive : scene.primitives)
    {
        report.counts.vertices += primitive.vertices.size();
        report.counts.indices += primitive.indices.size();
        expand(report.local_bounds, primitive.bounds);
    }
    if (scene.selected_scene.valid() && scene.selected_scene.value() < scene.scenes.size())
    {
        const SceneDefinition& selected = scene.scenes[scene.selected_scene.value()];
        report.counts.roots = selected.roots.size();
        report.world_bounds = selected.bounds;
        report.selected_scene = std::to_string(scene.selected_scene.value()) + ":" + selected.name;
    }
    return report;
}
}
