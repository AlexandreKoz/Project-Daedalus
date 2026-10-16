#include "assets/AssetLoader.h"

#include "assets/AssetCache.h"
#include "assets/BlenderWorker.h"
#include "core/Json.h"
#include "core/Version.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <system_error>
#include <utility>

namespace daedalus
{
namespace
{
constexpr std::string_view kLoaderIdentity = "daedalus-asset-loader/1";

[[nodiscard]] std::string utf8(const std::filesystem::path& path)
{
    const std::u8string value = path.u8string();
    return std::string(value.begin(), value.end());
}

[[nodiscard]] std::string lower_extension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

[[nodiscard]] ImportResult failed_result(const std::filesystem::path& source, ImportStatus status, DiagnosticCode code,
                                         std::string message, std::string settings, std::string expected = {}, std::string observed = {})
{
    ImportResult result;
    result.scene.source.display_name = utf8(source.filename());
    result.scene.source.format = lower_extension(source).empty() ? std::string("unknown") : lower_extension(source).substr(1);
    std::vector<Diagnostic> diagnostics{{DiagnosticSeverity::error, code, DiagnosticDisposition::rejected, "import", std::move(message),
                                         std::move(expected), std::move(observed)}};
    result.report = make_import_report(result.scene, status, std::move(diagnostics), std::move(settings));
    return result;
}

[[nodiscard]] ImportStatus with_diagnostics(ImportStatus status, const std::vector<Diagnostic>& diagnostics)
{
    if (!is_success(status)) return status;
    bool warning = false;
    bool repair = false;
    for (const Diagnostic& diagnostic : diagnostics)
    {
        warning |= diagnostic.severity == DiagnosticSeverity::warning;
        repair |= diagnostic.disposition == DiagnosticDisposition::repaired || diagnostic.disposition == DiagnosticDisposition::defaulted;
    }
    if (repair || status == ImportStatus::success_with_repairs) return ImportStatus::success_with_repairs;
    if (warning || status == ImportStatus::success_with_warnings) return ImportStatus::success_with_warnings;
    return ImportStatus::success;
}

[[nodiscard]] CompatibilityImportSettings compatibility_settings(const AssetLoadOptions& options)
{
    CompatibilityImportSettings settings;
    settings.limits = options.import_settings;
    settings.texture_search_roots = options.texture_search_roots;
    settings.up_axis = options.up_axis;
    settings.metres_per_unit = options.metres_per_unit;
    return settings;
}

// Identity of the converter executable; Blender's version is only known after it runs, so the key
// uses its resolved path, size and modification time (an in-place upgrade changes at least one).
[[nodiscard]] std::string blender_identity(const AssetLoadOptions& options)
{
    const std::optional<std::filesystem::path> executable =
        locate_blender_executable(options.blender_executable.value_or(std::filesystem::path{}));
    std::ostringstream identity;
    identity << "blender-worker;script=" << kBlenderExportScriptVersion;
    std::error_code error;
    if (!options.blender_script.empty() && std::filesystem::is_regular_file(options.blender_script, error))
        identity << ";script_sha256=" << sha256_file_hex(options.blender_script, 16ULL * 1024ULL * 1024ULL);
    if (executable.has_value())
    {
        identity << ";blender=" << utf8(std::filesystem::weakly_canonical(*executable, error));
        const std::uintmax_t size = std::filesystem::file_size(*executable, error);
        if (!error) identity << ";size=" << size;
        const auto modified = std::filesystem::last_write_time(*executable, error);
        if (!error) identity << ";mtime=" << modified.time_since_epoch().count();
    }
    else
    {
        identity << ";blender=<not-found>";
    }
    return identity.str();
}

[[nodiscard]] std::string settings_for(AssetSourceKind kind, const AssetLoadOptions& options)
{
    switch (kind)
    {
    case AssetSourceKind::compatibility: return compatibility_settings(options).deterministic_description();
    case AssetSourceKind::blender:
        return "blend;timeout=" + std::to_string(options.blender_timeout_seconds) + ";gltf{" + options.import_settings.deterministic_description() + "}";
    default: return options.import_settings.deterministic_description();
    }
}

[[nodiscard]] std::string adapter_identity(AssetSourceKind kind, const AssetLoadOptions& options)
{
    switch (kind)
    {
    case AssetSourceKind::gltf: return "gltf-native/" + std::string(kVersion);
    case AssetSourceKind::compatibility: return "compatibility/" + compatibility_importer_identity() + "/" + std::string(kVersion);
    case AssetSourceKind::blender: return blender_identity(options) + "/" + std::string(kVersion);
    default: return "unsupported";
    }
}

void import_gltf(const AssetLoadOptions& options, AssetLoadResult& result)
{
    result.import = GltfImporter{}.import_file(options.source, options.import_settings);
    const std::filesystem::path directory = options.source.parent_path();
    for (const DependencyRecord& dependency : result.import.scene.source.dependencies)
        result.dependencies.push_back(directory / std::filesystem::path(dependency.normalized_relative_path));
    result.import.report.provenance["adapter"] = "gltf-native";
}

void import_compatibility(const AssetLoadOptions& options, AssetLoadResult& result)
{
    CompatibilityImportResult imported = CompatibilityImporter{}.import_file(options.source, compatibility_settings(options));
    result.import = std::move(imported.import);
    result.dependencies = std::move(imported.resolved_dependencies);
    result.notes.push_back("Compatibility importer: " + compatibility_importer_identity());
}

void import_blender(const AssetLoadOptions& options, const std::string& source_sha256, AssetLoadResult& result)
{
    const std::string settings = settings_for(AssetSourceKind::blender, options);
    if (options.blender_script.empty())
    {
        result.import = failed_result(options.source, ImportStatus::unsupported_feature, DiagnosticCode::external_converter_failed,
                                      "the Blender export script location was not configured", settings);
        return;
    }
    if (options.temporary_directory.empty())
    {
        result.import = failed_result(options.source, ImportStatus::unsupported_feature, DiagnosticCode::external_converter_failed,
                                      "no conversion work directory was configured for .blend conversion", settings);
        return;
    }
    BlenderConversionRequest request;
    request.blender_executable = options.blender_executable.value_or(std::filesystem::path{});
    request.script = options.blender_script;
    request.source = options.source;
    // One work directory per source content hash: repeated conversions of the same file reuse it,
    // different files never collide.
    request.output_directory = options.temporary_directory / (source_sha256.empty() ? std::string("unhashed") : source_sha256.substr(0, 16));
    request.timeout = std::chrono::seconds(options.blender_timeout_seconds);
    request.cancel = options.cancel;
    const BlenderConversionResult conversion = run_blender_conversion(request);
    if (!conversion.success)
    {
        const bool unavailable = conversion.executable.empty();
        result.import = failed_result(options.source, unavailable ? ImportStatus::unsupported_feature : ImportStatus::io_failure,
                                      DiagnosticCode::external_converter_failed, conversion.failure, settings,
                                      unavailable ? "an installed Blender (--blender <path>)" : "exit code 0", conversion.failure);
        result.import.report.provenance["adapter"] = "blender-worker";
        result.import.report.provenance["blender_log"] = utf8(conversion.log_path);
        result.import.report.provenance["blender_timed_out"] = conversion.timed_out ? "true" : "false";
        result.import.report.provenance["blender_exit_code"] = std::to_string(conversion.exit_code);
        for (const std::string& warning : conversion.warnings) result.notes.push_back("Blender: " + warning);
        return;
    }

    ImportResult imported = GltfImporter{}.import_file(conversion.glb_path, options.import_settings);
    std::vector<Diagnostic> diagnostics = imported.report.diagnostics;
    for (const std::string& warning : conversion.warnings)
        diagnostics.push_back({DiagnosticSeverity::warning, DiagnosticCode::external_converter_warning, DiagnosticDisposition::observed,
                               "blender", warning, {}, {}});
    for (const std::string& unsupported : conversion.unsupported)
        diagnostics.push_back({DiagnosticSeverity::warning, DiagnosticCode::feature_discarded, DiagnosticDisposition::ignored,
                               "blender", unsupported, {}, {}});
    for (const std::string& approximation : conversion.approximations)
        diagnostics.push_back({DiagnosticSeverity::warning, DiagnosticCode::material_approximated, DiagnosticDisposition::converted,
                               "blender", approximation, {}, {}});

    // The report describes the .blend the user supplied; the intermediate GLB is provenance.
    imported.scene.source.display_name = utf8(options.source.filename());
    imported.scene.source.format = "blend";
    const std::string glb_sha256 = imported.scene.source.source_sha256;
    imported.scene.source.source_sha256 = source_sha256;
    const ImportStatus status = with_diagnostics(imported.report.status, diagnostics);
    const std::map<std::string, std::string> glb_provenance = imported.report.provenance;
    imported.report = make_import_report(imported.scene, status, std::move(diagnostics), settings);
    imported.report.provenance = glb_provenance;
    imported.report.provenance["adapter"] = "blender-worker";
    imported.report.provenance["blender_executable"] = utf8(conversion.executable);
    imported.report.provenance["blender_version"] = conversion.blender_version;
    imported.report.provenance["blender_script_version"] = conversion.script_version;
    imported.report.provenance["intermediate_glb"] = utf8(conversion.glb_path);
    imported.report.provenance["intermediate_glb_sha256"] = glb_sha256;
    imported.report.provenance["conversion_manifest"] = utf8(conversion.manifest_path);
    result.import = std::move(imported);
    result.notes.push_back("Blender " + conversion.blender_version + " converted " + utf8(options.source.filename()) + " with " +
                           conversion.script_version);

    // External images the .blend referenced are dependencies of the cached result (packed images are
    // part of the .blend bytes already).
    try
    {
        const std::string manifest_text = [&]
        {
            std::ifstream input(conversion.manifest_path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        }();
        const JsonValue manifest = parse_json(manifest_text);
        if (const JsonValue* images = manifest.find("external_images"); images != nullptr && images->is_array())
            for (const JsonValue& image : images->as_array())
                if (image.is_string()) result.dependencies.push_back(std::filesystem::path(std::u8string(
                                           reinterpret_cast<const char8_t*>(image.as_string().data()), image.as_string().size())));
    }
    catch (const std::exception& error)
    {
        result.notes.push_back(std::string("Blender manifest dependency list unavailable: ") + error.what());
    }
    result.dependencies.push_back(options.blender_script);
}
}

AssetSourceKind classify_asset_source(const std::filesystem::path& source)
{
    const std::string extension = lower_extension(source);
    if (extension == ".gltf" || extension == ".glb") return AssetSourceKind::gltf;
    if (extension == ".blend") return AssetSourceKind::blender;
    if (is_compatibility_source(source)) return AssetSourceKind::compatibility;
    return AssetSourceKind::unsupported;
}

std::string_view to_string(AssetSourceKind kind) noexcept
{
    switch (kind)
    {
    case AssetSourceKind::gltf: return "gltf";
    case AssetSourceKind::compatibility: return "compatibility";
    case AssetSourceKind::blender: return "blender";
    case AssetSourceKind::unsupported: return "unsupported";
    }
    return "unsupported";
}

AssetLoadResult load_asset(const AssetLoadOptions& options)
{
    AssetLoadResult result;
    result.kind = classify_asset_source(options.source);
    const std::string settings = settings_for(result.kind, options);
    if (result.kind == AssetSourceKind::unsupported)
    {
        result.import = failed_result(options.source, ImportStatus::unsupported_feature, DiagnosticCode::unsupported_source_format,
                                      "unsupported asset format", settings, ".gltf, .glb, .obj, .fbx, .3ds or .blend",
                                      lower_extension(options.source));
        result.import.report.provenance["loader"] = std::string(kLoaderIdentity);
        return result;
    }

    std::string source_sha256;
    std::optional<AssetCache> cache;
    std::vector<Diagnostic> cache_diagnostics;
    const std::uint64_t hash_limit = result.kind == AssetSourceKind::blender ? kAssetCacheMaximumBytes : options.import_settings.maximum_source_bytes;
    if (options.cache_directory.has_value() || result.kind == AssetSourceKind::blender)
    {
        try
        {
            source_sha256 = sha256_file_hex(options.source, hash_limit);
        }
        catch (const std::exception& error)
        {
            result.notes.push_back(std::string("Source could not be hashed; cache bypassed: ") + error.what());
        }
    }
    if (options.cache_directory.has_value() && !source_sha256.empty())
    {
        try
        {
            cache.emplace(*options.cache_directory);
            result.cache_key = compute_asset_cache_key(
                {source_sha256, adapter_identity(result.kind, options), settings, options.texture_search_roots});
            if (std::optional<CachedAsset> cached = cache->load(result.cache_key, cache_diagnostics); cached.has_value())
            {
                try
                {
                    ImportReport report = import_report_from_json(cached->report_json);
                    report.diagnostics.insert(report.diagnostics.end(), cache_diagnostics.begin(), cache_diagnostics.end());
                    sort_diagnostics(report.diagnostics);
                    report.provenance["cache"] = "hit";
                    report.provenance["cache_key"] = result.cache_key;
                    result.import.scene = std::move(cached->scene);
                    result.import.report = std::move(report);
                    for (const CacheDependency& dependency : cached->dependencies) result.dependencies.push_back(dependency.resolved_path);
                    result.cache_hit = true;
                    result.notes.push_back("Asset cache hit " + result.cache_key.substr(0, 16) + " (dependencies re-verified)");
                    return result;
                }
                catch (const std::exception& error)
                {
                    std::error_code remove_error;
                    std::filesystem::remove(cache->entry_path(result.cache_key), remove_error);
                    cache_diagnostics.push_back({DiagnosticSeverity::warning, DiagnosticCode::cache_corrupt, DiagnosticDisposition::repaired,
                                                 "asset-cache", std::string("cached report unreadable (") + error.what() +
                                                                    "); entry discarded and asset re-imported", {}, result.cache_key});
                }
            }
        }
        catch (const std::exception& error)
        {
            cache.reset();
            result.notes.push_back(std::string("Asset cache disabled for this load: ") + error.what());
        }
    }

    switch (result.kind)
    {
    case AssetSourceKind::gltf: import_gltf(options, result); break;
    case AssetSourceKind::compatibility: import_compatibility(options, result); break;
    case AssetSourceKind::blender: import_blender(options, source_sha256, result); break;
    case AssetSourceKind::unsupported: break;
    }
    ImportReport& report = result.import.report;
    report.provenance["loader"] = std::string(kLoaderIdentity);
    report.provenance["source_kind"] = std::string(to_string(result.kind));
    if (!cache_diagnostics.empty())
    {
        report.diagnostics.insert(report.diagnostics.end(), cache_diagnostics.begin(), cache_diagnostics.end());
        sort_diagnostics(report.diagnostics);
        report.status = with_diagnostics(report.status, report.diagnostics);
    }

    if (!cache.has_value())
    {
        report.provenance["cache"] = options.cache_directory.has_value() ? "unavailable" : "disabled";
        return result;
    }
    report.provenance["cache_key"] = result.cache_key;
    if (!result.import.succeeded())
    {
        report.provenance["cache"] = "not-stored (import failed)";
        return result;
    }
    try
    {
        CachedAsset entry;
        entry.scene = result.import.scene;
        for (const std::filesystem::path& dependency : result.dependencies)
        {
            CacheDependency record;
            record.resolved_path = std::filesystem::absolute(dependency);
            record.sha256 = sha256_file_hex(record.resolved_path, kAssetCacheMaximumBytes);
            std::error_code error;
            record.byte_size = std::filesystem::file_size(record.resolved_path, error);
            entry.dependencies.push_back(std::move(record));
        }
        report.provenance["cache"] = "stored";
        entry.report_json = report.to_json();
        cache->store(result.cache_key, entry);
        result.notes.push_back("Asset cache stored " + result.cache_key.substr(0, 16));
    }
    catch (const std::exception& error)
    {
        report.provenance["cache"] = "store-failed";
        result.notes.push_back(std::string("Asset cache store failed (asset still loaded): ") + error.what());
    }
    return result;
}
}
