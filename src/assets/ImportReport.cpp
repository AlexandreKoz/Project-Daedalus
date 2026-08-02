#include "assets/ImportReport.h"

#include "core/Json.h"
#include "core/Version.h"

#include <algorithm>
#include <sstream>

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
    report.extensions_supported = {"KHR_lights_punctual", "KHR_mesh_quantization"};
    report.diagnostics = std::move(diagnostics);
    sort_diagnostics(report.diagnostics);

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
