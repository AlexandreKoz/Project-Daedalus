// Campaign H asset-pipeline tests (source only in the D-H drop; not executed). They never start
// Blender and never require Assimp: the Assimp-dependent case adapts to compatibility_importer_available().
#include "TestHarness.h"
#include "assets/AssetCache.h"
#include "assets/AssetLoader.h"
#include "assets/BlenderWorker.h"
#include "assets/CompatibilityImporter.h"
#include "assets/FloatImage.h"
#include "assets/ImportReport.h"
#include "assets/SceneSerialization.h"
#include "assets/TextureResolver.h"
#include "scene/Scene.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace
{
using namespace daedalus;
using namespace daedalus::tests;

class TemporaryDirectory final
{
public:
    TemporaryDirectory()
    {
        std::random_device device;
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count() ^ static_cast<long long>(device());
        path_ = std::filesystem::temp_directory_path() / ("daedalus-asset-tests-" + std::to_string(nonce));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, std::string_view contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    require(static_cast<bool>(output), "test file write");
}

bool has_code(const std::vector<Diagnostic>& diagnostics, DiagnosticCode code)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const Diagnostic& diagnostic) { return diagnostic.code == code; });
}

void test_texture_resolver()
{
    TemporaryDirectory directory;
    const std::filesystem::path asset = directory.path() / "asset";
    const std::filesystem::path library = directory.path() / "library";
    write_file(asset / "textures" / "Wood.png", "wood");
    write_file(library / "extra" / "stone.png", "stone");
    write_file(asset / "dup.png", "a");
    write_file(library / "dup.png", "b");
    write_file(directory.path() / "outside.png", "outside");

    TextureResolveOptions options;
    options.asset_directory = asset;
    options.search_roots = {library, library / "extra"};

    const TextureResolution mixed_case = resolve_texture_reference("textures\\wood.PNG", options);
    require(mixed_case.status == TextureResolveStatus::resolved, "Windows-style mixed-case reference resolves");
    require(mixed_case.strategy == "exact" || mixed_case.strategy == "case-insensitive", "resolved in the asset directory");

    const TextureResolution authoring = resolve_texture_reference("C:\\Users\\artist\\maps\\Wood.png", options);
    require(authoring.status == TextureResolveStatus::resolved && authoring.strategy == "basename",
            "absolute authoring paths resolve by basename only");

    const TextureResolution search = resolve_texture_reference("stone.png", options);
    require(search.status == TextureResolveStatus::resolved, "search roots are consulted");
    require(search.strategy == "search-root" || search.strategy == "search-root-case-insensitive" || search.strategy == "basename",
            "search-root strategy reported");

    const TextureResolution ambiguous = resolve_texture_reference("D:\\elsewhere\\DUP.png", options);
    require(ambiguous.status == TextureResolveStatus::resolved_ambiguous, "duplicate basenames are reported as ambiguous");
    require(ambiguous.candidates.size() == 2U, "both candidates listed");
    require(ambiguous.path.parent_path() == asset, "asset directory wins over search roots");

    require(resolve_texture_reference("https://example.com/a.png", options).status == TextureResolveStatus::rejected, "URIs rejected");
    require(resolve_texture_reference("file:///etc/passwd", options).status == TextureResolveStatus::rejected, "file URIs rejected");
    require(resolve_texture_reference("../outside.png", options).status == TextureResolveStatus::rejected, "escaping references rejected");
    require(resolve_texture_reference("missing.png", options).status == TextureResolveStatus::missing, "missing reported");
}

void test_cache_key_inputs()
{
    const CacheKeyInputs base{std::string(64, 'a'), "adapter/1", "settings=1", {}};
    const std::string key = compute_asset_cache_key(base);
    require(key.size() == 64U, "cache key is a SHA-256 hex digest");
    require(key == compute_asset_cache_key(base), "cache key is deterministic");
    CacheKeyInputs changed = base;
    changed.adapter_identity = "adapter/2";
    require(key != compute_asset_cache_key(changed), "adapter version changes the key");
    changed = base;
    changed.import_settings = "settings=2";
    require(key != compute_asset_cache_key(changed), "import settings change the key");
    changed = base;
    changed.texture_search_roots = {"textures"};
    require(key != compute_asset_cache_key(changed), "search roots change the key");
    changed = base;
    changed.source_sha256 = std::string(64, 'b');
    require(key != compute_asset_cache_key(changed), "source bytes change the key");
}

void test_cache_hit_corruption_and_staleness()
{
    TemporaryDirectory directory;
    const AssetCache cache(directory.path() / "cache");
    const std::filesystem::path dependency = directory.path() / "texture.png";
    write_file(dependency, "original");
    CachedAsset asset;
    asset.scene = make_builtin_cornell_box_scene();
    asset.report_json = "{}";
    asset.dependencies.push_back({dependency, sha256_file_hex(dependency, 1024U), 8U});
    const std::string key = compute_asset_cache_key({std::string(64, 'c'), "test", "settings", {}});

    cache.store(key, asset);
    std::vector<Diagnostic> diagnostics;
    const std::optional<CachedAsset> hit = cache.load(key, diagnostics);
    require(hit.has_value(), "fresh entry is a hit");
    require(hit->scene.primitives.size() == asset.scene.primitives.size(), "cached scene round-trips");
    require(has_code(diagnostics, DiagnosticCode::cache_hit), "hit is reported");

    // Corruption: flip one payload byte; checksum must reject and delete the entry.
    {
        std::fstream file(cache.entry_path(key), std::ios::binary | std::ios::in | std::ios::out);
        file.seekg(-1, std::ios::end);
        char last = 0;
        file.read(&last, 1);
        file.seekp(-1, std::ios::end);
        last = static_cast<char>(last ^ 0x5A);
        file.write(&last, 1);
    }
    diagnostics.clear();
    require(!cache.load(key, diagnostics).has_value(), "corrupt entry is not used");
    require(has_code(diagnostics, DiagnosticCode::cache_corrupt), "corruption is reported");
    require(!std::filesystem::exists(cache.entry_path(key)), "corrupt entry is deleted");

    // Truncation.
    cache.store(key, asset);
    std::filesystem::resize_file(cache.entry_path(key), 20U);
    diagnostics.clear();
    require(!cache.load(key, diagnostics).has_value() && has_code(diagnostics, DiagnosticCode::cache_corrupt), "truncated entry rejected");

    // Staleness: dependency content changes.
    cache.store(key, asset);
    write_file(dependency, "modified");
    diagnostics.clear();
    require(!cache.load(key, diagnostics).has_value(), "stale entry is not used");
    require(has_code(diagnostics, DiagnosticCode::cache_stale), "staleness is reported");

    // Missing dependency.
    write_file(dependency, "original");
    cache.store(key, asset);
    std::filesystem::remove(dependency);
    diagnostics.clear();
    require(!cache.load(key, diagnostics).has_value() && has_code(diagnostics, DiagnosticCode::cache_stale), "missing dependency is stale");
    require_throws<std::invalid_argument>([&] { (void)cache.entry_path("../../etc/passwd"); }, "non-hex keys cannot escape the cache");
}

void test_scene_serialization_round_trip()
{
    for (const CanonicalScene& scene : {make_builtin_cornell_box_scene(), make_builtin_alpha_instancing_scene()})
    {
        const std::vector<std::byte> bytes = serialize_scene(scene);
        const CanonicalScene copy = deserialize_scene(bytes);
        require(copy.nodes.size() == scene.nodes.size() && copy.primitives.size() == scene.primitives.size() &&
                    copy.materials.size() == scene.materials.size() && copy.images.size() == scene.images.size() &&
                    copy.lights.size() == scene.lights.size(),
                "serialized scene keeps its object counts");
        require(serialize_scene(copy) == bytes, "serialization is deterministic and lossless");
        std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 2U));
        require_throws<std::runtime_error>([&] { (void)deserialize_scene(truncated); }, "truncated scene payload rejected");
    }
}

void test_windows_argument_quoting()
{
    require(quote_windows_argument("simple") == "simple", "plain argument unchanged");
    require(quote_windows_argument("") == "\"\"", "empty argument quoted");
    require(quote_windows_argument("with space") == "\"with space\"", "spaces quoted");
    require(quote_windows_argument("a\"b") == "\"a\\\"b\"", "embedded quote escaped");
    require(quote_windows_argument("C:\\dir with space\\") == "\"C:\\dir with space\\\\\"", "trailing backslash doubled before closing quote");
    require(quote_windows_argument("C:\\plain\\path") == "C:\\plain\\path", "backslashes without quoting untouched");

    BlenderConversionRequest request;
    request.script = "script.py";
    request.source = "scene.blend";
    request.output_directory = "out";
    const std::vector<std::string> arguments = blender_command_arguments(request, "blender");
    require(arguments.size() == 11U, "argument vector size");
    require(arguments[1] == "--background" && arguments[2] == "--factory-startup", "background, factory settings");
    require(arguments[3] == "--python-exit-code" && arguments[5] == "--python", "script failures surface as exit codes");
    require(arguments[7] == "--", "script arguments separated");
}

void test_missing_blender_is_reported()
{
    TemporaryDirectory directory;
    write_file(directory.path() / "scene.blend", "BLENDER-v300");
    write_file(directory.path() / "script.py", "# not executed");
    BlenderConversionRequest request;
    request.blender_executable = directory.path() / "no-such-blender.exe";
    request.script = directory.path() / "script.py";
    request.source = directory.path() / "scene.blend";
    request.output_directory = directory.path() / "work";
    require(!locate_blender_executable(request.blender_executable).has_value(), "missing configured executable not located");
    const BlenderConversionResult result = run_blender_conversion(request);
    require(!result.success && result.executable.empty(), "conversion fails before starting anything");
    require(result.failure.find("not found") != std::string::npos, "failure names the missing executable");

    AssetLoadOptions load;
    load.source = request.source;
    load.blender_executable = request.blender_executable;
    load.blender_script = request.script;
    load.temporary_directory = directory.path() / "conversion";
    const AssetLoadResult loaded = load_asset(load);
    require(loaded.kind == AssetSourceKind::blender, ".blend classified");
    require(!loaded.import.succeeded(), "missing Blender fails the import");
    require(has_code(loaded.import.report.diagnostics, DiagnosticCode::external_converter_failed), "converter failure diagnostic");
}

void test_loader_dispatch_and_compatibility_availability()
{
    require(classify_asset_source("a.GLB") == AssetSourceKind::gltf, "glb dispatch (case-insensitive)");
    require(classify_asset_source("a.Obj") == AssetSourceKind::compatibility, "obj dispatch");
    require(classify_asset_source("a.FBX") == AssetSourceKind::compatibility, "fbx dispatch");
    require(classify_asset_source("a.3ds") == AssetSourceKind::compatibility, "3ds dispatch");
    require(classify_asset_source("a.blend") == AssetSourceKind::blender, "blend dispatch");
    require(classify_asset_source("a.usd") == AssetSourceKind::unsupported, "undeclared formats unsupported");

    TemporaryDirectory directory;
    AssetLoadOptions load;
    load.source = directory.path() / "model.usd";
    const AssetLoadResult unsupported = load_asset(load);
    require(!unsupported.import.succeeded() && has_code(unsupported.import.report.diagnostics, DiagnosticCode::unsupported_source_format),
            "unsupported formats are rejected with a diagnostic");

    write_file(directory.path() / "tri.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    load.source = directory.path() / "tri.obj";
    const AssetLoadResult obj = load_asset(load);
    if (compatibility_importer_available())
    {
        require(obj.import.succeeded(), obj.import.report.summary());
        require(obj.import.report.counts.primitives == 1U && obj.import.report.counts.vertices == 3U, "OBJ triangle imported");
        require(obj.import.report.provenance.count("adapter") == 1U, "adapter provenance recorded");
        require(obj.import.scene.primitives.front().has_normals, "missing OBJ normals generated");

        write_file(directory.path() / "bad_index.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\n");
        load.source = directory.path() / "bad_index.obj";
        const AssetLoadResult bad = load_asset(load);
        require(!bad.import.succeeded(), "out-of-range OBJ face index must not import silently");
    }
    else
    {
        require(!obj.import.succeeded() && has_code(obj.import.report.diagnostics, DiagnosticCode::compatibility_importer_unavailable),
                "without Assimp OBJ import is explicitly unavailable");
    }
}

#ifdef DAEDALUS_COMPAT_FIXTURES_GENERATED
void test_obj_gltf_equivalence()
{
#ifdef DAEDALUS_FIXTURE_ROOT
    const std::filesystem::path root = std::filesystem::path(DAEDALUS_FIXTURE_ROOT) / "compat";
    require(compatibility_importer_available(), "equivalence test is only built with DAEDALUS_ENABLE_ASSIMP=ON");
    require(std::filesystem::exists(root / "textures" / "checker.png") && std::filesystem::exists(root / "quad_equivalent.gltf"),
            "compat fixtures missing: run tests/assets/generate_fixtures.py");
    AssetLoadOptions load;
    load.source = root / "quad.obj";
    const AssetLoadResult obj = load_asset(load);
    load.source = root / "quad_equivalent.gltf";
    const AssetLoadResult gltf = load_asset(load);
    require(obj.import.succeeded() && gltf.import.succeeded(), "both encodings import");
    require(has_code(obj.import.report.diagnostics, DiagnosticCode::texture_resolved_by_search) ||
                obj.import.scene.textures.size() == 1U,
            "Windows-style mixed-case texture reference resolves");
    require(obj.import.scene.textures.size() == gltf.import.scene.textures.size(), "same texture count");
    const Primitive& a = obj.import.scene.primitives.at(0);
    const Primitive& b = gltf.import.scene.primitives.at(0);
    require(a.indices.size() == b.indices.size(), "same triangle count");
    // Compare per-corner attributes (vertex order may differ after Assimp's vertex joining).
    const auto corners = [](const Primitive& primitive)
    {
        std::vector<std::array<float, 5>> values;
        for (const std::uint32_t index : primitive.indices)
        {
            const Vertex& vertex = primitive.vertices.at(index);
            values.push_back({vertex.position.x, vertex.position.y, vertex.position.z, vertex.texcoord0.x, vertex.texcoord0.y});
        }
        std::sort(values.begin(), values.end());
        return values;
    };
    const auto left = corners(a);
    const auto right = corners(b);
    for (std::size_t index = 0; index < left.size(); ++index)
        for (std::size_t component = 0; component < 5U; ++component)
            require_near(left[index][component], right[index][component], 1.0e-6F, "OBJ and glTF corners match (UV flip applied)");
#endif
}
#endif

void test_import_report_round_trip()
{
    const CanonicalScene scene = make_builtin_cornell_box_scene();
    ImportReport report = make_import_report(scene, ImportStatus::success_with_warnings,
                                             {{DiagnosticSeverity::warning, DiagnosticCode::material_approximated, DiagnosticDisposition::converted,
                                               "materials[0]", "approximated", "pbr", "phong"}},
                                             "settings");
    report.provenance["adapter"] = "test";
    const ImportReport parsed = import_report_from_json(report.to_json());
    require(parsed.to_json() == report.to_json(), "import report JSON round-trips exactly");
    require_throws<std::runtime_error>([] { (void)import_report_from_json("{\"report_schema_version\":\"other\"}"); }, "foreign schema rejected");
}

void test_pfm_round_trip()
{
    TemporaryDirectory directory;
    FloatImageRgb image;
    image.width = 3U;
    image.height = 2U;
    image.rgb = {0.0F, 1.0F, 2.0F, 3.5F, -1.0F, 1.0e6F, 0.25F, 0.5F, 0.75F, 9.0F, 8.0F, 7.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
    write_pfm(directory.path() / "image.pfm", image);
    const FloatImageRgb copy = read_pfm(directory.path() / "image.pfm");
    require(copy.width == image.width && copy.height == image.height, "PFM dimensions");
    require(copy.rgb == image.rgb, "PFM pixels round-trip with top-left origin");
    require_near(half_to_float(0x3C00U), 1.0F, 0.0F, "half 1.0");
    require_near(half_to_float(0xC000U), -2.0F, 0.0F, "half -2.0");
}
}

int main()
{
    return daedalus::tests::run({
        {"texture resolver", test_texture_resolver},
        {"cache key inputs", test_cache_key_inputs},
        {"cache hit, corruption and staleness", test_cache_hit_corruption_and_staleness},
        {"scene serialization round trip", test_scene_serialization_round_trip},
        {"Windows argument quoting", test_windows_argument_quoting},
        {"missing Blender is reported", test_missing_blender_is_reported},
        {"loader dispatch and compatibility availability", test_loader_dispatch_and_compatibility_availability},
#ifdef DAEDALUS_COMPAT_FIXTURES_GENERATED
        {"OBJ/glTF equivalence", test_obj_gltf_equivalence},
#endif
        {"import report round trip", test_import_report_round_trip},
        {"PFM round trip", test_pfm_round_trip}});
}
