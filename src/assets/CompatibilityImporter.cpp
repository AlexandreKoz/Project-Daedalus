#include "assets/CompatibilityImporter.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#ifdef DAEDALUS_HAS_ASSIMP
#include "assets/ImageDecoder.h"
#include "core/Sha256.h"
#include "assets/TextureResolver.h"

#include <assimp/DefaultIOSystem.h>
#include <assimp/IOStream.hpp>
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/metadata.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/version.h>

#include <array>
#include <cmath>
#include <initializer_list>
#include <fstream>
#include <map>
#include <new>
#include <numbers>
#include <span>
#include <stdexcept>
#include <system_error>
#include <tuple>
#endif

namespace daedalus
{
namespace
{
[[nodiscard]] std::string lower_extension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

[[nodiscard]] std::string utf8(const std::filesystem::path& path)
{
    const std::u8string value = path.u8string();
    return std::string(value.begin(), value.end());
}

[[nodiscard]] std::string format_from_extension(const std::filesystem::path& path)
{
    const std::string extension = lower_extension(path);
    return extension.empty() ? std::string("unknown") : extension.substr(1);
}

}

const std::vector<std::string>& compatibility_source_extensions()
{
    static const std::vector<std::string> extensions{".3ds", ".fbx", ".obj"};
    return extensions;
}

bool is_compatibility_source(const std::filesystem::path& path)
{
    const std::string extension = lower_extension(path);
    const auto& extensions = compatibility_source_extensions();
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

std::optional<SourceUpAxis> parse_source_up_axis(std::string_view text) noexcept
{
    if (text == "auto" || text == "automatic") return SourceUpAxis::automatic;
    if (text == "y" || text == "y-up" || text == "y_up") return SourceUpAxis::y_up;
    if (text == "z" || text == "z-up" || text == "z_up") return SourceUpAxis::z_up;
    return std::nullopt;
}

std::string_view to_string(SourceUpAxis axis) noexcept
{
    switch (axis)
    {
    case SourceUpAxis::automatic: return "auto";
    case SourceUpAxis::y_up: return "y-up";
    case SourceUpAxis::z_up: return "z-up";
    }
    return "auto";
}

std::string CompatibilityImportSettings::deterministic_description() const
{
    std::ostringstream output;
    output << "compatibility;adapter=" << compatibility_importer_identity()
           << ";max_source=" << limits.maximum_source_bytes
           << ";max_dependency=" << limits.maximum_dependency_bytes
           << ";max_total=" << limits.maximum_total_decoded_bytes
           << ";max_peak=" << limits.maximum_peak_bytes
           << ";up_axis=" << to_string(up_axis)
           << ";metres_per_unit=";
    if (metres_per_unit.has_value()) output << *metres_per_unit;
    else output << "auto";
    output << ";smoothing_angle=" << smoothing_angle_degrees
           << ";uv=flip-v;tangents=source-or-generated-gltf;images=png,jpeg,embedded-raw"
           << ";materials=pbr-keys-or-phong-approximation;light_cones=full-angle-sources"
           << ";search_roots=" << texture_search_roots.size();
    return output.str();
}

#ifndef DAEDALUS_HAS_ASSIMP

bool compatibility_importer_available() noexcept
{
    return false;
}

std::string compatibility_importer_identity()
{
    return "unavailable";
}

CompatibilityImportResult CompatibilityImporter::import_file(const std::filesystem::path& source,
                                                             const CompatibilityImportSettings& settings) const
{
    CompatibilityImportResult result;
    CanonicalScene& scene = result.import.scene;
    scene.source.display_name = utf8(source.filename());
    scene.source.format = format_from_extension(source);
    std::vector<Diagnostic> diagnostics;
    diagnostics.push_back({DiagnosticSeverity::error, DiagnosticCode::compatibility_importer_unavailable, DiagnosticDisposition::rejected,
                           "import",
                           "this build has no compatibility importer: reconfigure with -DDAEDALUS_ENABLE_ASSIMP=ON against an "
                           "installed Assimp package, or convert the asset to glTF 2.0 first",
                           "DAEDALUS_ENABLE_ASSIMP=ON", "DAEDALUS_ENABLE_ASSIMP=OFF"});
    result.import.report = make_import_report(scene, ImportStatus::unsupported_feature, std::move(diagnostics),
                                              settings.deterministic_description());
    result.import.report.provenance["adapter"] = "unavailable";
    result.import.report.provenance["blocked_reason"] = "Assimp backend not compiled in (DAEDALUS_ENABLE_ASSIMP=OFF)";
    return result;
}

#else

namespace
{
[[nodiscard]] ImportStatus status_from(const std::vector<Diagnostic>& diagnostics)
{
    bool warning = false;
    bool repair = false;
    for (const Diagnostic& diagnostic : diagnostics)
    {
        warning |= diagnostic.severity == DiagnosticSeverity::warning;
        repair |= diagnostic.disposition == DiagnosticDisposition::repaired || diagnostic.disposition == DiagnosticDisposition::defaulted;
    }
    if (repair) return ImportStatus::success_with_repairs;
    if (warning) return ImportStatus::success_with_warnings;
    return ImportStatus::success;
}

[[nodiscard]] std::string compose_key(const CanonicalScene& scene, std::string_view settings)
{
    Sha256 hash;
    hash.update(CanonicalScene::schema_version);
    hash.update("\ncompatibility\n");
    hash.update(settings);
    hash.update("\nsource:");
    hash.update(scene.source.source_sha256);
    std::vector<DependencyRecord> dependencies = scene.source.dependencies;
    std::sort(dependencies.begin(), dependencies.end(),
              [](const auto& left, const auto& right) { return left.normalized_relative_path < right.normalized_relative_path; });
    for (const DependencyRecord& dependency : dependencies)
    {
        hash.update("\ndep:");
        hash.update(dependency.normalized_relative_path);
        hash.update(":");
        hash.update(dependency.sha256);
    }
    const auto digest = hash.finish();
    constexpr char hex[] = "0123456789abcdef";
    std::string output(64, '0');
    for (std::size_t index = 0; index < digest.size(); ++index)
    {
        const unsigned value = static_cast<unsigned>(digest[index]);
        output[index * 2U] = hex[value >> 4U];
        output[index * 2U + 1U] = hex[value & 0x0FU];
    }
    return output;
}

class ImportAbort final : public std::runtime_error
{
public:
    ImportAbort(ImportStatus status, Diagnostic diagnostic)
        : std::runtime_error(diagnostic.message), status_(status), diagnostic_(std::move(diagnostic))
    {
    }
    [[nodiscard]] ImportStatus status() const noexcept { return status_; }
    [[nodiscard]] const Diagnostic& diagnostic() const noexcept { return diagnostic_; }

private:
    ImportStatus status_;
    Diagnostic diagnostic_;
};

[[noreturn]] void fail(ImportStatus status, DiagnosticCode code, std::string location, std::string message,
                       std::string expected = {}, std::string observed = {})
{
    throw ImportAbort(status, {DiagnosticSeverity::error, code, DiagnosticDisposition::rejected, std::move(location),
                               std::move(message), std::move(expected), std::move(observed)});
}

void note(std::vector<Diagnostic>& diagnostics, DiagnosticSeverity severity, DiagnosticCode code, DiagnosticDisposition disposition,
          std::string location, std::string message, std::string expected = {}, std::string observed = {})
{
    diagnostics.push_back({severity, code, disposition, std::move(location), std::move(message), std::move(expected), std::move(observed)});
}

[[nodiscard]] std::string ai_text(const aiString& value)
{
    return std::string(value.C_Str(), value.length);
}

[[nodiscard]] std::filesystem::path path_from_utf8(const char* text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text)));
}

[[nodiscard]] bool path_is_within(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    auto root_it = root.begin();
    auto candidate_it = candidate.begin();
    for (; root_it != root.end(); ++root_it, ++candidate_it)
    {
        if (candidate_it == candidate.end() || *root_it != *candidate_it)
        {
            // A trailing empty element ("dir/") on the root still matches.
            if (root_it->empty() && std::next(root_it) == root.end()) return true;
            return false;
        }
    }
    return true;
}

[[nodiscard]] Mat4 to_mat4(const aiMatrix4x4& matrix)
{
    Mat4 result;
    for (unsigned row = 0; row < 4U; ++row)
        for (unsigned column = 0; column < 4U; ++column) result.at(row, column) = static_cast<float>(matrix[row][column]);
    return result;
}

[[nodiscard]] Vec3 to_vec3(const aiVector3D& value)
{
    return {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)};
}

[[nodiscard]] bool nearly_identity(const Mat4& matrix)
{
    const Mat4 identity = identity_matrix();
    for (std::size_t index = 0; index < 16U; ++index)
        if (std::abs(matrix.values[index] - identity.values[index]) > 1.0e-6F) return false;
    return true;
}

[[nodiscard]] std::optional<double> metadata_number(const aiMetadata* metadata, std::string_view key)
{
    if (metadata == nullptr) return std::nullopt;
    for (unsigned index = 0; index < metadata->mNumProperties; ++index)
    {
        if (std::string_view(metadata->mKeys[index].C_Str()) != key) continue;
        const aiMetadataEntry& entry = metadata->mValues[index];
        if (entry.mData == nullptr) return std::nullopt;
        switch (entry.mType)
        {
        case AI_BOOL: return *static_cast<const bool*>(entry.mData) ? 1.0 : 0.0;
        case AI_INT32: return static_cast<double>(*static_cast<const std::int32_t*>(entry.mData));
        case AI_UINT64: return static_cast<double>(*static_cast<const std::uint64_t*>(entry.mData));
        case AI_FLOAT: return static_cast<double>(*static_cast<const float*>(entry.mData));
        case AI_DOUBLE: return *static_cast<const double*>(entry.mData);
        default: return std::nullopt;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> metadata_string(const aiMetadata* metadata, std::string_view key)
{
    if (metadata == nullptr) return std::nullopt;
    for (unsigned index = 0; index < metadata->mNumProperties; ++index)
    {
        if (std::string_view(metadata->mKeys[index].C_Str()) != key) continue;
        const aiMetadataEntry& entry = metadata->mValues[index];
        if (entry.mType != AI_AISTRING || entry.mData == nullptr) return std::nullopt;
        return ai_text(*static_cast<const aiString*>(entry.mData));
    }
    return std::nullopt;
}

// Every file Assimp opens goes through this handler: reads are confined to the asset directory and
// the configured search roots, bounded in size, and recorded as cache dependencies.
class TrackingIOSystem final : public Assimp::DefaultIOSystem
{
public:
    TrackingIOSystem(std::vector<std::filesystem::path> roots, std::uint64_t per_file_limit, std::uint64_t total_limit)
        : roots_(std::move(roots)), per_file_limit_(per_file_limit), total_limit_(total_limit)
    {
    }

    Assimp::IOStream* Open(const char* file, const char* mode = "rb") override
    {
        if (file == nullptr || mode == nullptr) return nullptr;
        if (std::string_view(mode).find_first_of("wa+") != std::string_view::npos)
        {
            refusals_.push_back(std::string("write access refused: ") + file);
            return nullptr;
        }
        std::error_code error;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path_from_utf8(file), error);
        if (error) return nullptr;
        const bool allowed = std::any_of(roots_.begin(), roots_.end(),
                                         [&](const std::filesystem::path& root) { return path_is_within(root, canonical); });
        if (!allowed)
        {
            refusals_.push_back("read outside the asset directory and search roots refused: " + utf8(canonical));
            return nullptr;
        }
        const std::uintmax_t size = std::filesystem::file_size(canonical, error);
        if (error) return nullptr;
        if (size > per_file_limit_ || total_ + size > total_limit_)
        {
            refusals_.push_back("read refused by the dependency size budget: " + utf8(canonical));
            return nullptr;
        }
        Assimp::IOStream* stream = Assimp::DefaultIOSystem::Open(file, mode);
        if (stream != nullptr)
        {
            total_ += size;
            if (std::find(opened_.begin(), opened_.end(), canonical) == opened_.end()) opened_.push_back(canonical);
        }
        return stream;
    }

    [[nodiscard]] const std::vector<std::filesystem::path>& opened() const noexcept { return opened_; }
    [[nodiscard]] const std::vector<std::string>& refusals() const noexcept { return refusals_; }

private:
    std::vector<std::filesystem::path> roots_;
    std::uint64_t per_file_limit_ = 0;
    std::uint64_t total_limit_ = 0;
    std::uint64_t total_ = 0;
    std::vector<std::filesystem::path> opened_;
    std::vector<std::string> refusals_;
};

class Budget final
{
public:
    Budget(const ImportSettings& limits, ResourceUsage& usage) : limits_(limits), usage_(usage) {}

    void add_source(std::uint64_t bytes) { usage_.source_payload_bytes += bytes; update("source"); }
    void add_geometry(std::uint64_t bytes, const std::string& location) { usage_.canonical_geometry_bytes += bytes; update(location); }
    void add_encoded(std::uint64_t bytes, const std::string& location) { usage_.encoded_image_bytes += bytes; update(location); }
    void add_decoded(std::uint64_t bytes, const std::string& location) { usage_.decoded_image_bytes += bytes; update(location); }
    void scratch(std::uint64_t bytes, const std::string& location)
    {
        largest_scratch_ = std::max(largest_scratch_, bytes);
        update(location);
    }

private:
    void update(const std::string& location)
    {
        usage_.retained_bytes = usage_.source_payload_bytes + usage_.encoded_image_bytes + usage_.canonical_geometry_bytes +
                                usage_.decoded_image_bytes;
        usage_.conservative_peak_bytes = std::max(usage_.conservative_peak_bytes, usage_.retained_bytes + largest_scratch_);
        if (usage_.retained_bytes > limits_.maximum_total_decoded_bytes)
            fail(ImportStatus::resource_limit, DiagnosticCode::resource_budget_exceeded, location,
                 "retained import data exceeds the configured budget", std::to_string(limits_.maximum_total_decoded_bytes),
                 std::to_string(usage_.retained_bytes));
        if (usage_.conservative_peak_bytes > limits_.maximum_peak_bytes)
            fail(ImportStatus::resource_limit, DiagnosticCode::resource_budget_exceeded, location,
                 "conservative peak import memory exceeds the configured budget", std::to_string(limits_.maximum_peak_bytes),
                 std::to_string(usage_.conservative_peak_bytes));
    }

    const ImportSettings& limits_;
    ResourceUsage& usage_;
    std::uint64_t largest_scratch_ = 0;
};

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path& path, std::uint64_t limit, const std::string& location)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) fail(ImportStatus::io_failure, DiagnosticCode::io_open_failed, location, "cannot stat file", {}, error.message());
    if (size > limit)
        fail(ImportStatus::resource_limit, DiagnosticCode::source_too_large, location, "file exceeds the configured size limit",
             std::to_string(limit), std::to_string(size));
    std::ifstream input(path, std::ios::binary);
    if (!input) fail(ImportStatus::io_failure, DiagnosticCode::io_open_failed, location, "cannot open file", {}, utf8(path));
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (size != 0U) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!input) fail(ImportStatus::io_failure, DiagnosticCode::io_open_failed, location, "cannot read file", {}, utf8(path));
    return bytes;
}

[[nodiscard]] std::string sniff_mime(std::span<const std::byte> bytes)
{
    const auto at = [&](std::size_t index) { return static_cast<unsigned>(bytes[index]); };
    if (bytes.size() >= 8U && at(0) == 0x89U && at(1) == 0x50U && at(2) == 0x4EU && at(3) == 0x47U) return "image/png";
    if (bytes.size() >= 3U && at(0) == 0xFFU && at(1) == 0xD8U && at(2) == 0xFFU) return "image/jpeg";
    return {};
}

[[nodiscard]] WrapMode wrap_mode(aiTextureMapMode mode)
{
    switch (mode)
    {
    case aiTextureMapMode_Clamp: return WrapMode::clamp_to_edge;
    case aiTextureMapMode_Mirror: return WrapMode::mirrored_repeat;
    case aiTextureMapMode_Decal: return WrapMode::clamp_to_edge;
    default: return WrapMode::repeat;
    }
}

class TextureLoader final
{
public:
    TextureLoader(CanonicalScene& scene, const aiScene& source, TextureResolveOptions options, const ImportSettings& limits,
                  Budget& budget, std::vector<Diagnostic>& diagnostics, std::vector<std::filesystem::path>& resolved)
        : scene_(scene), source_(source), options_(std::move(options)), limits_(limits), budget_(budget), diagnostics_(diagnostics),
          resolved_(resolved)
    {
        std::error_code error;
        canonical_asset_directory_ = std::filesystem::weakly_canonical(options_.asset_directory, error);
        for (const std::filesystem::path& root : options_.search_roots)
        {
            std::error_code root_error;
            canonical_search_roots_.push_back(std::filesystem::weakly_canonical(root, root_error));
        }
    }

    [[nodiscard]] std::optional<TextureReference> load(const aiMaterial& material, aiTextureType type, ColorSpaceIntent color_space,
                                                       const std::string& location)
    {
        if (material.GetTextureCount(type) == 0U) return std::nullopt;
        aiString path;
        aiTextureMapping mapping = aiTextureMapping_UV;
        unsigned uv_index = 0;
        std::array<aiTextureMapMode, 3> modes{aiTextureMapMode_Wrap, aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};
        if (material.GetTexture(type, 0, &path, &mapping, &uv_index, nullptr, nullptr, modes.data()) != AI_SUCCESS) return std::nullopt;
        if (material.GetTextureCount(type) > 1U)
            note(diagnostics_, DiagnosticSeverity::warning, DiagnosticCode::feature_discarded, DiagnosticDisposition::ignored, location,
                 "layered textures are not supported; only the first layer is used", "1", std::to_string(material.GetTextureCount(type)));
        if (mapping != aiTextureMapping_UV)
        {
            note(diagnostics_, DiagnosticSeverity::warning, DiagnosticCode::feature_discarded, DiagnosticDisposition::ignored, location,
                 "projected (non-UV) texture mapping is not supported; texture discarded", "uv", std::to_string(static_cast<int>(mapping)));
            return std::nullopt;
        }
        if (uv_index > 1U)
        {
            note(diagnostics_, DiagnosticSeverity::warning, DiagnosticCode::feature_discarded, DiagnosticDisposition::ignored, location,
                 "texture uses a UV set above 1; texture discarded", "0 or 1", std::to_string(uv_index));
            return std::nullopt;
        }
        aiUVTransform transform;
        if (material.Get(AI_MATKEY_UVTRANSFORM(type, 0), transform) == AI_SUCCESS &&
            (std::abs(transform.mTranslation.x) > 1.0e-6F || std::abs(transform.mTranslation.y) > 1.0e-6F ||
             std::abs(transform.mScaling.x - 1.0F) > 1.0e-6F || std::abs(transform.mScaling.y - 1.0F) > 1.0e-6F ||
             std::abs(transform.mRotation) > 1.0e-6F))
            note(diagnostics_, DiagnosticSeverity::warning, DiagnosticCode::texture_transform_unsupported, DiagnosticDisposition::ignored,
                 location, "UV transform is not supported and was ignored");

        const std::optional<ImageId> image = image_for(ai_text(path), location);
        if (!image.has_value()) return std::nullopt;
        if (modes[0] == aiTextureMapMode_Decal || modes[1] == aiTextureMapMode_Decal)
            note(diagnostics_, DiagnosticSeverity::warning, DiagnosticCode::material_approximated, DiagnosticDisposition::converted, location,
                 "decal wrap mode approximated as clamp-to-edge");
        const SamplerId sampler = sampler_for(wrap_mode(modes[0]), wrap_mode(modes[1]));
        const auto key = std::make_tuple(image->value(), static_cast<int>(color_space), sampler.value());
        auto existing = textures_.find(key);
        if (existing == textures_.end())
        {
            Texture texture;
            texture.name = scene_.images[image->value()].name;
            texture.image = *image;
            texture.sampler = sampler;
            texture.color_space = color_space;
            scene_.textures.push_back(std::move(texture));
            existing = textures_.emplace(key, TextureId(static_cast<std::uint32_t>(scene_.textures.size() - 1U))).first;
        }
        TextureReference reference;
        reference.texture = existing->second;
        reference.texcoord_set = uv_index;
        return reference;
    }

    [[nodiscard]] std::string raw_reference(const aiMaterial& material, aiTextureType type) const
    {
        aiString path;
        if (material.GetTextureCount(type) == 0U || material.GetTexture(type, 0, &path) != AI_SUCCESS) return {};
        return ai_text(path);
    }

private:
    [[nodiscard]] SamplerId sampler_for(WrapMode wrap_s, WrapMode wrap_t)
    {
        const auto key = std::make_pair(static_cast<int>(wrap_s), static_cast<int>(wrap_t));
        if (const auto found = samplers_.find(key); found != samplers_.end()) return found->second;
        Sampler sampler;
        sampler.name = "compatibility-sampler";
        sampler.min_filter = FilterMode::linear_mipmap_linear;
        sampler.mag_filter = FilterMode::linear;
        sampler.wrap_s = wrap_s;
        sampler.wrap_t = wrap_t;
        scene_.samplers.push_back(sampler);
        const SamplerId id(static_cast<std::uint32_t>(scene_.samplers.size() - 1U));
        samplers_.emplace(key, id);
        return id;
    }

    [[nodiscard]] std::optional<ImageId> image_for(const std::string& reference, const std::string& location)
    {
        if (const aiTexture* embedded = source_.GetEmbeddedTexture(reference.c_str()); embedded != nullptr)
            return cached("embedded:" + reference, [&] { return embedded_image(*embedded, reference, location); });

        const TextureResolution resolution = resolve_texture_reference(reference, options_);
        switch (resolution.status)
        {
        case TextureResolveStatus::missing:
            note(diagnostics_, DiagnosticSeverity::warning, DiagnosticCode::missing_dependency, DiagnosticDisposition::defaulted, location,
                 "texture dependency not found; texture slot left empty (" + resolution.message + ")", {}, reference);
            return std::nullopt;
        case TextureResolveStatus::rejected:
            note(diagnostics_, DiagnosticSeverity::warning, DiagnosticCode::unsafe_dependency_path, DiagnosticDisposition::rejected, location,
                 "texture reference rejected (" + resolution.message + ")", {}, reference);
            return std::nullopt;
        case TextureResolveStatus::resolved_ambiguous:
            note(diagnostics_, DiagnosticSeverity::warning, DiagnosticCode::ambiguous_dependency, DiagnosticDisposition::observed, location,
                 "several candidate files match this texture; the first in search order was used", {}, utf8(resolution.path));
            break;
        case TextureResolveStatus::resolved:
            if (resolution.strategy != "exact")
                note(diagnostics_, DiagnosticSeverity::information, DiagnosticCode::texture_resolved_by_search, DiagnosticDisposition::observed,
                     location, "texture resolved by " + resolution.strategy + " lookup", reference, utf8(resolution.path));
            break;
        }
        std::error_code error;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(resolution.path, error);
        const std::filesystem::path key_path = error ? resolution.path : canonical;
        return cached("file:" + utf8(key_path), [&] { return file_image(key_path, location); });
    }

    template <typename Factory>
    [[nodiscard]] std::optional<ImageId> cached(const std::string& key, Factory&& factory)
    {
        if (const auto found = images_.find(key); found != images_.end()) return found->second;
        std::optional<ImageId> image = factory();
        images_.emplace(key, image);
        return image;
    }

    [[nodiscard]] std::optional<ImageId> decode_into(std::vector<std::byte> encoded, std::string mime, std::string name,
                                                     std::string identity, const std::string& location)
    {
        if (mime.empty())
        {
            note(diagnostics_, DiagnosticSeverity::warning, DiagnosticCode::unsupported_image_encoding, DiagnosticDisposition::ignored, location,
                 "only PNG and JPEG textures are decoded; texture discarded (convert it to PNG)", "image/png or image/jpeg", identity);
            return std::nullopt;
        }
        budget_.add_encoded(encoded.size(), location);
        DecodedImage decoded;
        try
        {
            decoded = decode_image_rgba8(encoded, mime);
        }
        catch (const ImageDecodeError& error)
        {
            note(diagnostics_, DiagnosticSeverity::warning, DiagnosticCode::invalid_image, DiagnosticDisposition::ignored, location,
                 std::string("texture could not be decoded and was discarded: ") + error.what(), {}, identity);
            return std::nullopt;
        }
        budget_.add_decoded(decoded.rgba8.size(), location);
        Image image;
        image.name = std::move(name);
        image.source_identity = std::move(identity);
        image.mime_type = std::move(mime);
        image.encoded_bytes = std::move(encoded);
        image.width = decoded.width;
        image.height = decoded.height;
        image.components = decoded.source_components;
        image.row_stride = decoded.row_stride;
        image.decoded_rgba8 = std::move(decoded.rgba8);
        scene_.images.push_back(std::move(image));
        return ImageId(static_cast<std::uint32_t>(scene_.images.size() - 1U));
    }

    [[nodiscard]] std::optional<ImageId> embedded_image(const aiTexture& texture, const std::string& reference, const std::string& location)
    {
        const std::string name = texture.mFilename.length != 0U ? ai_text(texture.mFilename) : reference;
        if (texture.mHeight == 0U)
        {
            const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(texture.pcData), texture.mWidth);
            if (bytes.size() > limits_.maximum_dependency_bytes)
                fail(ImportStatus::resource_limit, DiagnosticCode::source_too_large, location, "embedded texture exceeds the dependency limit");
            return decode_into(std::vector<std::byte>(bytes.begin(), bytes.end()), sniff_mime(bytes), name, "embedded:" + reference, location);
        }
        const std::uint64_t texels = static_cast<std::uint64_t>(texture.mWidth) * texture.mHeight;
        if (texels == 0U || texels > (1ULL << 28U))
        {
            note(diagnostics_, DiagnosticSeverity::warning, DiagnosticCode::invalid_image, DiagnosticDisposition::ignored, location,
                 "embedded raw texture has invalid dimensions; discarded");
            return std::nullopt;
        }
        budget_.add_decoded(texels * 4U, location);
        Image image;
        image.name = name;
        image.source_identity = "embedded-raw:" + reference;
        image.mime_type = "application/x-daedalus-rgba8";
        image.width = texture.mWidth;
        image.height = texture.mHeight;
        image.components = 4;
        image.row_stride = static_cast<std::uint64_t>(texture.mWidth) * 4U;
        image.decoded_rgba8.resize(static_cast<std::size_t>(texels * 4U));
        for (std::uint64_t index = 0; index < texels; ++index)
        {
            const aiTexel& texel = texture.pcData[index];
            image.decoded_rgba8[index * 4U + 0U] = static_cast<std::byte>(texel.r);
            image.decoded_rgba8[index * 4U + 1U] = static_cast<std::byte>(texel.g);
            image.decoded_rgba8[index * 4U + 2U] = static_cast<std::byte>(texel.b);
            image.decoded_rgba8[index * 4U + 3U] = static_cast<std::byte>(texel.a);
        }
        scene_.images.push_back(std::move(image));
        return ImageId(static_cast<std::uint32_t>(scene_.images.size() - 1U));
    }

    [[nodiscard]] std::optional<ImageId> file_image(const std::filesystem::path& path, const std::string& location)
    {
        std::vector<std::byte> bytes = read_file(path, limits_.maximum_dependency_bytes, location);
        DependencyRecord dependency;
        dependency.normalized_relative_path = dependency_name(path);
        dependency.sha256 = sha256_hex(std::span<const std::byte>(bytes));
        dependency.byte_size = bytes.size();
        scene_.source.dependencies.push_back(dependency);
        if (std::find(resolved_.begin(), resolved_.end(), path) == resolved_.end()) resolved_.push_back(path);
        std::string mime = sniff_mime(bytes);
        return decode_into(std::move(bytes), std::move(mime), utf8(path.filename()), dependency.normalized_relative_path, location);
    }

    [[nodiscard]] std::string dependency_name(const std::filesystem::path& path) const
    {
        if (path_is_within(canonical_asset_directory_, path)) return path.lexically_relative(canonical_asset_directory_).generic_string();
        for (std::size_t index = 0; index < canonical_search_roots_.size(); ++index)
            if (path_is_within(canonical_search_roots_[index], path))
                return "search-root[" + std::to_string(index) + "]/" + path.lexically_relative(canonical_search_roots_[index]).generic_string();
        return path.filename().generic_string();
    }

    CanonicalScene& scene_;
    const aiScene& source_;
    TextureResolveOptions options_;
    const ImportSettings& limits_;
    Budget& budget_;
    std::vector<Diagnostic>& diagnostics_;
    std::vector<std::filesystem::path>& resolved_;
    std::filesystem::path canonical_asset_directory_;
    std::vector<std::filesystem::path> canonical_search_roots_;
    std::map<std::string, std::optional<ImageId>> images_;
    std::map<std::tuple<std::uint32_t, int, std::uint32_t>, TextureId> textures_;
    std::map<std::pair<int, int>, SamplerId> samplers_;
};

[[nodiscard]] float luminance(const aiColor3D& color)
{
    return 0.2126F * color.r + 0.7152F * color.g + 0.0722F * color.b;
}

[[nodiscard]] Material convert_material(const aiMaterial& source, std::size_t index, TextureLoader& textures,
                                        std::vector<Diagnostic>& diagnostics)
{
    Material material;
    const std::string location = "materials[" + std::to_string(index) + "]";
    aiString name;
    if (source.Get(AI_MATKEY_NAME, name) == AI_SUCCESS) material.name = ai_text(name);

    int shading = 0;
    const bool pbr_shading = source.Get(AI_MATKEY_SHADING_MODEL, shading) == AI_SUCCESS && shading == aiShadingMode_PBR_BRDF;
    float metallic = 0.0F;
    float roughness = 1.0F;
    const bool has_metallic = source.Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS;
    const bool has_roughness = source.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS;
    const bool pbr = pbr_shading || has_metallic || has_roughness;

    aiColor4D base(1.0F, 1.0F, 1.0F, 1.0F);
    if (source.Get(AI_MATKEY_BASE_COLOR, base) != AI_SUCCESS)
    {
        aiColor3D diffuse(1.0F, 1.0F, 1.0F);
        if (source.Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) base = aiColor4D(diffuse.r, diffuse.g, diffuse.b, 1.0F);
    }
    material.base_color_factor = {std::clamp(base.r, 0.0F, 1.0F), std::clamp(base.g, 0.0F, 1.0F), std::clamp(base.b, 0.0F, 1.0F),
                                  std::clamp(base.a, 0.0F, 1.0F)};

    if (pbr)
    {
        material.metallic_factor = std::clamp(metallic, 0.0F, 1.0F);
        material.roughness_factor = std::clamp(roughness, 0.0F, 1.0F);
    }
    else
    {
        // Blinn-Phong exponent n -> Beckmann alpha = sqrt(2 / (n + 2)) -> GGX roughness = sqrt(alpha).
        float shininess = 0.0F;
        source.Get(AI_MATKEY_SHININESS, shininess);
        shininess = std::max(shininess, 0.0F);
        material.metallic_factor = 0.0F;
        material.roughness_factor = std::clamp(std::pow(2.0F / (shininess + 2.0F), 0.25F), 0.045F, 1.0F);
        aiColor3D specular(0.0F, 0.0F, 0.0F);
        source.Get(AI_MATKEY_COLOR_SPECULAR, specular);
        std::ostringstream observed;
        observed << "shininess=" << shininess << " specular_luminance=" << luminance(specular);
        note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::material_approximated, DiagnosticDisposition::converted, location,
             "legacy Phong/Blinn material approximated: base=diffuse, metallic=0, roughness=(2/(n+2))^0.25; specular colour discarded",
             "metallic-roughness PBR", observed.str());
    }

    float opacity = 1.0F;
    if (source.Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS && opacity < 0.9999F)
    {
        if (opacity <= 1.0e-3F)
        {
            note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::material_approximated, DiagnosticDisposition::repaired, location,
                 "opacity 0 is treated as an authoring/exporter error; material kept opaque", "opacity > 0", std::to_string(opacity));
        }
        else
        {
            material.alpha_mode = AlphaMode::blend;
            material.base_color_factor.w *= opacity;
        }
    }

    aiColor3D emissive(0.0F, 0.0F, 0.0F);
    if (source.Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS)
        material.emissive_factor = {std::max(emissive.r, 0.0F), std::max(emissive.g, 0.0F), std::max(emissive.b, 0.0F)};
#ifdef AI_MATKEY_EMISSIVE_INTENSITY
    float emissive_intensity = 1.0F;
    if (source.Get(AI_MATKEY_EMISSIVE_INTENSITY, emissive_intensity) == AI_SUCCESS && emissive_intensity > 0.0F)
        material.emissive_strength = emissive_intensity;
#endif
    int two_sided = 0;
    if (source.Get(AI_MATKEY_TWOSIDED, two_sided) == AI_SUCCESS) material.double_sided = two_sided != 0;

    float ior = 1.5F;
    const bool has_ior = source.Get(AI_MATKEY_REFRACTI, ior) == AI_SUCCESS;
#ifdef AI_MATKEY_TRANSMISSION_FACTOR
    float transmission = 0.0F;
    if (source.Get(AI_MATKEY_TRANSMISSION_FACTOR, transmission) == AI_SUCCESS && transmission > 0.0F)
    {
        material.transmission_factor = std::clamp(transmission, 0.0F, 1.0F);
        if (has_ior && ior >= 1.0F && ior <= 3.0F) material.ior = ior;
    }
#endif
    // Legacy formats default Ni/IOR to 1.0, which would remove all dielectric reflection under the
    // KHR_materials_ior semantics; the IOR is therefore only taken for transmissive materials.
    if (has_ior && material.transmission_factor == 0.0F && std::abs(ior - 1.5F) > 1.0e-3F)
        note(diagnostics, DiagnosticSeverity::information, DiagnosticCode::material_approximated, DiagnosticDisposition::ignored, location,
             "source IOR ignored for a non-transmissive material (dielectric F0 stays at IOR 1.5)", "1.5", std::to_string(ior));

    const std::optional<TextureReference> base_texture = [&]
    {
        if (auto texture = textures.load(source, aiTextureType_BASE_COLOR, ColorSpaceIntent::srgb, location + ".base_color"); texture)
            return texture;
        return textures.load(source, aiTextureType_DIFFUSE, ColorSpaceIntent::srgb, location + ".diffuse");
    }();
    material.base_color_texture = base_texture;
    material.normal_texture = textures.load(source, aiTextureType_NORMALS, ColorSpaceIntent::linear, location + ".normals");
    material.emissive_texture = textures.load(source, aiTextureType_EMISSIVE, ColorSpaceIntent::srgb, location + ".emissive");
    if (!material.emissive_texture.has_value())
        material.emissive_texture = textures.load(source, aiTextureType_EMISSION_COLOR, ColorSpaceIntent::srgb, location + ".emission_color");
    if (material.emissive_texture.has_value() && material.emissive_factor.x == 0.0F && material.emissive_factor.y == 0.0F &&
        material.emissive_factor.z == 0.0F)
        material.emissive_factor = {1.0F, 1.0F, 1.0F};
    material.occlusion_texture = textures.load(source, aiTextureType_AMBIENT_OCCLUSION, ColorSpaceIntent::linear, location + ".occlusion");

    const std::string opacity_reference = textures.raw_reference(source, aiTextureType_OPACITY);
    if (!opacity_reference.empty())
    {
        const std::string base_reference = textures.raw_reference(source, aiTextureType_BASE_COLOR).empty()
            ? textures.raw_reference(source, aiTextureType_DIFFUSE)
            : textures.raw_reference(source, aiTextureType_BASE_COLOR);
        if (opacity_reference == base_reference && base_texture.has_value())
        {
            material.alpha_mode = AlphaMode::mask;
            material.alpha_cutoff = 0.5F;
            note(diagnostics, DiagnosticSeverity::information, DiagnosticCode::material_approximated, DiagnosticDisposition::converted, location,
                 "opacity map equals the base colour map; alpha mask with cutoff 0.5 from its alpha channel");
        }
        else
        {
            note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::feature_discarded, DiagnosticDisposition::ignored, location,
                 "separate opacity map is not supported (alpha must live in the base colour texture); discarded", {}, opacity_reference);
        }
    }
    const std::pair<aiTextureType, const char*> discarded[] = {
        {aiTextureType_HEIGHT, "height/bump map (Daedalus supports tangent-space normal maps only)"},
        {aiTextureType_DISPLACEMENT, "displacement map"},
        {aiTextureType_SPECULAR, "specular map"},
        {aiTextureType_SHININESS, "shininess/glossiness map"},
        {aiTextureType_LIGHTMAP, "light map"},
        {aiTextureType_REFLECTION, "reflection map"},
        {aiTextureType_METALNESS, "separate metalness map (requires a packed glTF metallic-roughness texture)"},
        {aiTextureType_DIFFUSE_ROUGHNESS, "separate roughness map (requires a packed glTF metallic-roughness texture)"},
    };
    for (const auto& [type, description] : discarded)
    {
        const std::string reference = textures.raw_reference(source, type);
        if (!reference.empty())
            note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::feature_discarded, DiagnosticDisposition::ignored, location,
                 std::string(description) + " discarded", {}, reference);
    }
    return material;
}

void generate_tangents(Primitive& primitive)
{
    const std::size_t count = primitive.vertices.size();
    std::vector<Vec3> u_direction(count);
    std::vector<Vec3> v_direction(count);
    for (std::size_t triangle = 0; triangle + 2U < primitive.indices.size(); triangle += 3U)
    {
        const std::uint32_t a = primitive.indices[triangle];
        const std::uint32_t b = primitive.indices[triangle + 1U];
        const std::uint32_t c = primitive.indices[triangle + 2U];
        const Vertex& va = primitive.vertices[a];
        const Vertex& vb = primitive.vertices[b];
        const Vertex& vc = primitive.vertices[c];
        const Vec3 edge1 = vb.position - va.position;
        const Vec3 edge2 = vc.position - va.position;
        const float du1 = vb.texcoord0.x - va.texcoord0.x;
        const float dv1 = vb.texcoord0.y - va.texcoord0.y;
        const float du2 = vc.texcoord0.x - va.texcoord0.x;
        const float dv2 = vc.texcoord0.y - va.texcoord0.y;
        const float determinant = du1 * dv2 - du2 * dv1;
        if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-12F) continue;
        const float inverse = 1.0F / determinant;
        const Vec3 dp_du = (edge1 * dv2 - edge2 * dv1) * inverse;
        const Vec3 dp_dv = (edge2 * du1 - edge1 * du2) * inverse;
        for (const std::uint32_t vertex : {a, b, c})
        {
            u_direction[vertex] = u_direction[vertex] + dp_du;
            v_direction[vertex] = v_direction[vertex] + dp_dv;
        }
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        Vertex& vertex = primitive.vertices[index];
        const Vec3 n = vertex.normal;
        Vec3 t = u_direction[index] - n * dot(n, u_direction[index]);
        if (length(t) < 1.0e-8F)
        {
            const Vec3 axis = std::abs(n.x) < 0.9F ? Vec3{1.0F, 0.0F, 0.0F} : Vec3{0.0F, 1.0F, 0.0F};
            t = cross(axis, n);
        }
        t = normalize(t, {1.0F, 0.0F, 0.0F});
        // glTF/MikkTSpace convention: bitangent = cross(N, T) * w points towards DEcreasing v in the
        // top-left-origin UV space used by the canonical scene.
        const float handedness = dot(cross(n, t), v_direction[index] * -1.0F) < 0.0F ? -1.0F : 1.0F;
        vertex.tangent = {t.x, t.y, t.z, handedness};
    }
    primitive.has_tangents = true;
}

struct MeshConversion
{
    std::vector<std::optional<PrimitiveId>> primitives;
    std::map<std::vector<std::uint32_t>, MeshId> meshes;
};

[[nodiscard]] std::optional<PrimitiveId> convert_primitive(const aiScene& source, unsigned index, bool generated_normals,
                                                           CanonicalScene& scene, Budget& budget, std::vector<Diagnostic>& diagnostics)
{
    const aiMesh& mesh = *source.mMeshes[index];
    const std::string location = "meshes[" + std::to_string(index) + "]('" + ai_text(mesh.mName) + "')";
    if ((mesh.mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0U)
    {
        note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::unsupported_primitive_mode, DiagnosticDisposition::ignored, location,
             "point/line geometry is not supported and was discarded");
        return std::nullopt;
    }
    if (mesh.mNumVertices == 0U || mesh.mNumFaces == 0U || mesh.mVertices == nullptr)
    {
        note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::empty_primitive, DiagnosticDisposition::ignored, location,
             "mesh has no triangles and was discarded");
        return std::nullopt;
    }
    if (mesh.HasBones())
        note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::feature_discarded, DiagnosticDisposition::ignored, location,
             "skinning weights discarded; the bind pose is imported as static geometry", {}, std::to_string(mesh.mNumBones) + " bones");
    if (mesh.mNumAnimMeshes != 0U)
        note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::feature_discarded, DiagnosticDisposition::ignored, location,
             "morph targets discarded", {}, std::to_string(mesh.mNumAnimMeshes));
    for (unsigned set = 2; set < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++set)
        if (mesh.mTextureCoords[set] != nullptr)
        {
            note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::feature_discarded, DiagnosticDisposition::ignored, location,
                 "UV sets above 1 discarded");
            break;
        }

    budget.add_geometry(static_cast<std::uint64_t>(mesh.mNumVertices) * sizeof(Vertex) +
                            static_cast<std::uint64_t>(mesh.mNumFaces) * 3U * sizeof(std::uint32_t),
                        location);
    Primitive primitive;
    primitive.name = ai_text(mesh.mName);
    primitive.vertices.resize(mesh.mNumVertices);
    primitive.has_normals = mesh.mNormals != nullptr;
    primitive.has_texcoord0 = mesh.mTextureCoords[0] != nullptr;
    primitive.has_texcoord1 = mesh.mTextureCoords[1] != nullptr;
    primitive.has_colors = mesh.mColors[0] != nullptr;
    const bool source_tangents = primitive.has_normals && mesh.mTangents != nullptr && mesh.mBitangents != nullptr;
    for (unsigned vertex_index = 0; vertex_index < mesh.mNumVertices; ++vertex_index)
    {
        Vertex& vertex = primitive.vertices[vertex_index];
        vertex.position = to_vec3(mesh.mVertices[vertex_index]);
        if (!finite(vertex.position))
        {
            note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::non_finite_data, DiagnosticDisposition::rejected, location,
                 "non-finite vertex position; mesh discarded", {}, "vertex " + std::to_string(vertex_index));
            return std::nullopt;
        }
        if (primitive.has_normals) vertex.normal = normalize(to_vec3(mesh.mNormals[vertex_index]), {0.0F, 0.0F, 1.0F});
        // Assimp delivers bottom-left-origin UVs; the canonical scene uses glTF's top-left origin.
        if (primitive.has_texcoord0)
            vertex.texcoord0 = {static_cast<float>(mesh.mTextureCoords[0][vertex_index].x), 1.0F - static_cast<float>(mesh.mTextureCoords[0][vertex_index].y)};
        if (primitive.has_texcoord1)
            vertex.texcoord1 = {static_cast<float>(mesh.mTextureCoords[1][vertex_index].x), 1.0F - static_cast<float>(mesh.mTextureCoords[1][vertex_index].y)};
        if (primitive.has_colors)
        {
            const aiColor4D& color = mesh.mColors[0][vertex_index];
            vertex.color = {color.r, color.g, color.b, color.a};
        }
        if (source_tangents)
        {
            const Vec3 n = vertex.normal;
            const Vec3 source_tangent = to_vec3(mesh.mTangents[vertex_index]);
            const Vec3 t = normalize(source_tangent - n * dot(n, source_tangent), {1.0F, 0.0F, 0.0F});
            // Source bitangents point towards +v in the bottom-left convention, i.e. towards
            // decreasing canonical v, which is exactly the glTF bitangent direction.
            const float handedness = dot(cross(n, t), to_vec3(mesh.mBitangents[vertex_index])) < 0.0F ? -1.0F : 1.0F;
            vertex.tangent = {t.x, t.y, t.z, handedness};
        }
        if (!finite(vertex.normal) || !finite(vertex.texcoord0) || !finite(vertex.texcoord1) || !finite(vertex.color) || !finite(vertex.tangent))
        {
            note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::non_finite_data, DiagnosticDisposition::rejected, location,
                 "non-finite vertex attribute; mesh discarded", {}, "vertex " + std::to_string(vertex_index));
            return std::nullopt;
        }
    }
    primitive.has_tangents = source_tangents;

    std::uint64_t skipped_faces = 0;
    primitive.indices.reserve(static_cast<std::size_t>(mesh.mNumFaces) * 3U);
    for (unsigned face_index = 0; face_index < mesh.mNumFaces; ++face_index)
    {
        const aiFace& face = mesh.mFaces[face_index];
        if (face.mNumIndices != 3U)
        {
            ++skipped_faces;
            continue;
        }
        for (unsigned corner = 0; corner < 3U; ++corner)
        {
            if (face.mIndices[corner] >= mesh.mNumVertices)
                fail(ImportStatus::invalid_source, DiagnosticCode::index_out_of_range, location, "face index exceeds the vertex count");
            primitive.indices.push_back(face.mIndices[corner]);
        }
    }
    if (skipped_faces != 0U)
        note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::unsupported_primitive_mode, DiagnosticDisposition::ignored, location,
             "non-triangle faces discarded", "0", std::to_string(skipped_faces));
    if (primitive.indices.empty())
    {
        note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::empty_primitive, DiagnosticDisposition::ignored, location,
             "mesh has no triangles after filtering and was discarded");
        return std::nullopt;
    }
    if (generated_normals)
        note(diagnostics, DiagnosticSeverity::information, DiagnosticCode::normals_generated, DiagnosticDisposition::defaulted, location,
             "missing normals generated (Assimp smooth normals, angle-limited)");
    if (!primitive.has_tangents && primitive.has_normals && primitive.has_texcoord0)
    {
        generate_tangents(primitive);
        note(diagnostics, DiagnosticSeverity::information, DiagnosticCode::tangents_generated, DiagnosticDisposition::defaulted, location,
             "tangents generated from UV0 in the glTF convention");
    }
    if (mesh.mMaterialIndex >= scene.materials.size())
        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location, "material index out of range");
    primitive.material = MaterialId(mesh.mMaterialIndex);
    scene.primitives.push_back(std::move(primitive));
    return PrimitiveId(static_cast<std::uint32_t>(scene.primitives.size() - 1U));
}

// Local frame for an Assimp camera/light placed at `position` looking along `forward` with `up`,
// in canonical (glTF) convention where cameras look down -Z and lights shine along -Z.
[[nodiscard]] std::optional<Mat4> look_frame(Vec3 position, Vec3 forward, Vec3 up)
{
    const Vec3 z = normalize(forward * -1.0F, {0.0F, 0.0F, 0.0F});
    if (length(z) < 0.5F) return std::nullopt;
    Vec3 x = cross(up, z);
    if (length(x) < 1.0e-6F) x = cross(std::abs(z.y) < 0.9F ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F}, z);
    x = normalize(x, {1.0F, 0.0F, 0.0F});
    const Vec3 y = cross(z, x);
    Mat4 frame = identity_matrix();
    const Vec3 columns[] = {x, y, z, position};
    for (std::size_t column = 0; column < 4U; ++column)
    {
        frame.at(0, column) = columns[column].x;
        frame.at(1, column) = columns[column].y;
        frame.at(2, column) = columns[column].z;
    }
    return frame;
}

[[nodiscard]] NodeId add_child_node(CanonicalScene& scene, NodeId parent, std::string name, const Mat4& local)
{
    Node node;
    node.name = std::move(name);
    node.transform_source = TransformSource::matrix;
    node.local_transform = local;
    node.parent = parent;
    scene.nodes.push_back(std::move(node));
    const NodeId id(static_cast<std::uint32_t>(scene.nodes.size() - 1U));
    scene.nodes[parent.value()].children.push_back(id);
    return id;
}

struct AxisUnitPolicy
{
    Mat4 correction = identity_matrix();
    float metres_per_unit = 1.0F;
    std::string axis_description;
    std::string unit_source;
    bool mirrored = false;
};

[[nodiscard]] AxisUnitPolicy derive_axis_unit_policy(const aiScene& source, std::string_view format,
                                                     const CompatibilityImportSettings& settings, std::vector<Diagnostic>& diagnostics)
{
    AxisUnitPolicy policy;
    Mat4 axes = identity_matrix();
    if (settings.up_axis == SourceUpAxis::z_up)
    {
        // Right-handed Z-up -> right-handed Y-up: (x, y, z) -> (x, z, -y).
        axes.values = {};
        axes.at(0, 0) = 1.0F;
        axes.at(1, 2) = 1.0F;
        axes.at(2, 1) = -1.0F;
        axes.at(3, 3) = 1.0F;
        policy.axis_description = "override: +Z up right-handed converted to +Y up";
    }
    else if (settings.up_axis == SourceUpAxis::y_up)
    {
        policy.axis_description = "override: +Y up (no conversion)";
    }
    else if (format == "fbx")
    {
        const auto up = metadata_number(source.mMetaData, "UpAxis");
        const auto up_sign = metadata_number(source.mMetaData, "UpAxisSign");
        const auto front = metadata_number(source.mMetaData, "FrontAxis");
        const auto front_sign = metadata_number(source.mMetaData, "FrontAxisSign");
        const auto coord = metadata_number(source.mMetaData, "CoordAxis");
        const auto coord_sign = metadata_number(source.mMetaData, "CoordAxisSign");
        const auto valid_axis = [](const std::optional<double>& value) { return value.has_value() && *value >= 0.0 && *value <= 2.0; };
        if (valid_axis(up) && valid_axis(front) && valid_axis(coord) && *up != *front && *up != *coord && *front != *coord)
        {
            const auto sign = [](const std::optional<double>& value) { return value.has_value() && *value < 0.0 ? -1.0F : 1.0F; };
            axes.values = {};
            axes.at(0, static_cast<std::size_t>(*coord)) = sign(coord_sign);
            axes.at(1, static_cast<std::size_t>(*up)) = sign(up_sign);
            axes.at(2, static_cast<std::size_t>(*front)) = sign(front_sign);
            axes.at(3, 3) = 1.0F;
            std::ostringstream description;
            description << "fbx GlobalSettings: up=" << *up << '*' << sign(up_sign) << " front=" << *front << '*' << sign(front_sign)
                        << " coord=" << *coord << '*' << sign(coord_sign) << " -> canonical +X right, +Y up, +Z front";
            policy.axis_description = description.str();
        }
        else
        {
            policy.axis_description = "fbx axis metadata missing or inconsistent; +Y up assumed";
            note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::axis_converted, DiagnosticDisposition::defaulted, "metadata",
                 "FBX axis metadata missing or inconsistent; +Y up right-handed assumed");
        }
    }
    else if (format == "3ds")
    {
        policy.axis_description = "3ds: axes as delivered by Assimp (its loader rotates Z-up into Y-up)";
    }
    else
    {
        policy.axis_description = "obj: no axis metadata in the format; +Y up assumed";
    }
    policy.mirrored = determinant3x3(axes) < 0.0F;
    if (!nearly_identity(axes))
        note(diagnostics, DiagnosticSeverity::information, DiagnosticCode::axis_converted, DiagnosticDisposition::converted, "scene",
             "source axes converted to canonical +Y up: " + policy.axis_description);
    if (policy.mirrored)
        note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::axis_converted, DiagnosticDisposition::converted, "scene",
             "source coordinate system is left-handed; conversion mirrors the scene (negative-determinant root)");

    if (settings.metres_per_unit.has_value())
    {
        policy.metres_per_unit = *settings.metres_per_unit;
        policy.unit_source = "override";
    }
    else if (format == "fbx")
    {
        // FBX UnitScaleFactor is centimetres per file unit.
        const auto unit_scale = metadata_number(source.mMetaData, "UnitScaleFactor");
        if (unit_scale.has_value() && *unit_scale > 0.0 && std::isfinite(*unit_scale))
        {
            policy.metres_per_unit = static_cast<float>(*unit_scale * 0.01);
            policy.unit_source = "fbx UnitScaleFactor=" + std::to_string(*unit_scale) + " cm";
        }
        else
        {
            policy.metres_per_unit = 0.01F;
            policy.unit_source = "fbx UnitScaleFactor missing; FBX default centimetres assumed";
            note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::unit_converted, DiagnosticDisposition::defaulted, "metadata",
                 "FBX UnitScaleFactor missing; centimetres assumed");
        }
    }
    else
    {
        policy.metres_per_unit = 1.0F;
        policy.unit_source = "assumed metres (format carries no unit metadata)";
    }
    if (!(policy.metres_per_unit > 0.0F) || !std::isfinite(policy.metres_per_unit))
        fail(ImportStatus::invalid_source, DiagnosticCode::unit_converted, "settings", "metres per unit must be positive and finite");
    if (std::abs(policy.metres_per_unit - 1.0F) > 1.0e-6F)
        note(diagnostics, DiagnosticSeverity::information, DiagnosticCode::unit_converted, DiagnosticDisposition::converted, "scene",
             "source units converted to metres (" + policy.unit_source + ")", "1", std::to_string(policy.metres_per_unit));
    policy.correction = multiply(scale_matrix({policy.metres_per_unit, policy.metres_per_unit, policy.metres_per_unit}), axes);
    return policy;
}

[[nodiscard]] std::string assimp_version()
{
    std::ostringstream output;
    output << aiGetVersionMajor() << '.' << aiGetVersionMinor() << '.' << aiGetVersionPatch() << '+' << std::hex << aiGetVersionRevision();
    return output.str();
}

constexpr unsigned kReadFlags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType | aiProcess_ValidateDataStructure;
constexpr unsigned kNormalFlags = aiProcess_GenSmoothNormals;
}

bool compatibility_importer_available() noexcept
{
    return true;
}

std::string compatibility_importer_identity()
{
    return "assimp/" + assimp_version();
}

CompatibilityImportResult CompatibilityImporter::import_file(const std::filesystem::path& source,
                                                             const CompatibilityImportSettings& settings) const
{
    CompatibilityImportResult result;
    CanonicalScene scene;
    std::vector<Diagnostic> diagnostics;
    ImportStatus final_status = ImportStatus::internal_error;
    const std::string settings_description = settings.deterministic_description();
    const std::string format = format_from_extension(source);
    std::map<std::string, std::string> provenance;
    provenance["adapter"] = compatibility_importer_identity();
    provenance["source_format"] = format;
    provenance["assimp_read_flags"] = std::to_string(kReadFlags);
    provenance["uv_policy"] = "v' = 1 - v (Assimp bottom-left -> canonical top-left)";
    provenance["tangent_policy"] = "source tangents with bitangent-derived handedness, else generated from UV0 (glTF convention)";
    provenance["light_cone_policy"] = "Assimp FBX/3DS spot angles treated as full cone angles and halved";

    try
    {
        scene.source.display_name = utf8(source.filename());
        scene.source.format = format;
        if (!is_compatibility_source(source))
            fail(ImportStatus::unsupported_feature, DiagnosticCode::unsupported_source_format, "source",
                 "file extension is not a declared compatibility format", ".obj, .fbx or .3ds", lower_extension(source));
        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(source, error), error);
        if (error || !std::filesystem::is_regular_file(absolute, error))
            fail(ImportStatus::io_failure, DiagnosticCode::io_open_failed, "source", "source file does not exist or cannot be opened", {}, utf8(source));
        const std::vector<std::byte> source_bytes = read_file(absolute, settings.limits.maximum_source_bytes, "source");
        scene.source.source_sha256 = sha256_hex(std::span<const std::byte>(source_bytes));
        Budget budget(settings.limits, scene.source.resource_usage);
        budget.add_source(source_bytes.size());
        if (settings.limits.scene_selector.has_value())
            note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::default_scene_selected, DiagnosticDisposition::ignored, "settings",
                 "compatibility formats contain one scene; the scene selector was ignored", {}, *settings.limits.scene_selector);

        TextureResolveOptions resolve_options;
        resolve_options.asset_directory = absolute.parent_path();
        resolve_options.search_roots = settings.texture_search_roots;
        std::vector<std::filesystem::path> io_roots{absolute.parent_path()};
        for (const std::filesystem::path& root : settings.texture_search_roots)
        {
            std::error_code root_error;
            io_roots.push_back(std::filesystem::weakly_canonical(root, root_error));
        }

        Assimp::Importer importer;
        // The importer takes ownership of the IO handler.
        auto* io = new TrackingIOSystem(io_roots, settings.limits.maximum_source_bytes,
                                        settings.limits.maximum_source_bytes + settings.limits.maximum_dependency_bytes);
        importer.SetIOHandler(io);
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
        importer.SetPropertyBool(AI_CONFIG_IMPORT_NO_SKELETON_MESHES, true);
#ifdef AI_CONFIG_FBX_CONVERT_TO_M
        importer.SetPropertyBool(AI_CONFIG_FBX_CONVERT_TO_M, false); // Daedalus applies UnitScaleFactor itself
#endif
        importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, settings.smoothing_angle_degrees);
        budget.scratch(source_bytes.size() * 8U, "assimp"); // coarse allowance for Assimp's own parse structures

        const aiScene* imported = importer.ReadFile(utf8(absolute), kReadFlags);
        for (const std::string& refusal : io->refusals())
            note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::unsafe_dependency_path, DiagnosticDisposition::rejected, "assimp-io", refusal);
        if (imported == nullptr)
            fail(ImportStatus::invalid_source, DiagnosticCode::malformed_source, "source", "Assimp could not read the source", {},
                 importer.GetErrorString());
        if ((imported->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0U || imported->mRootNode == nullptr)
            fail(ImportStatus::invalid_source, DiagnosticCode::malformed_source, "source", "Assimp returned an incomplete scene");

        std::vector<bool> lacked_normals(imported->mNumMeshes, false);
        bool any_lacked = false;
        for (unsigned index = 0; index < imported->mNumMeshes; ++index)
        {
            const aiMesh* mesh = imported->mMeshes[index];
            lacked_normals[index] = mesh != nullptr && mesh->mNormals == nullptr && (mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) != 0U;
            any_lacked = any_lacked || lacked_normals[index];
        }
        if (any_lacked)
        {
            imported = importer.ApplyPostProcessing(kNormalFlags);
            if (imported == nullptr)
                fail(ImportStatus::internal_error, DiagnosticCode::internal_invariant, "assimp", "normal generation failed", {}, importer.GetErrorString());
            provenance["assimp_normal_flags"] = std::to_string(kNormalFlags);
        }
        const aiScene& ai = *imported;

        if (const auto generator = metadata_string(ai.mMetaData, "SourceAsset_Generator"); generator.has_value())
            scene.source.generator = *generator;
        if (const auto version = metadata_string(ai.mMetaData, "SourceAsset_FormatVersion"); version.has_value())
            scene.source.version = *version;
        if (ai.mNumAnimations != 0U)
            note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::feature_discarded, DiagnosticDisposition::ignored, "animations",
                 "animations discarded (static import)", {}, std::to_string(ai.mNumAnimations));

        // Materials (1:1 with Assimp; Assimp always provides at least a default material).
        TextureLoader textures(scene, ai, resolve_options, settings.limits, budget, diagnostics, result.resolved_dependencies);
        for (unsigned index = 0; index < ai.mNumMaterials; ++index)
            scene.materials.push_back(convert_material(*ai.mMaterials[index], index, textures, diagnostics));

        // Primitives (lazily per referenced Assimp mesh) and the node hierarchy.
        MeshConversion meshes;
        meshes.primitives.resize(ai.mNumMeshes);
        std::vector<bool> converted(ai.mNumMeshes, false);
        const AxisUnitPolicy axis_policy = derive_axis_unit_policy(ai, format, settings, diagnostics);
        provenance["axis_policy"] = axis_policy.axis_description;
        provenance["metres_per_unit"] = std::to_string(axis_policy.metres_per_unit);
        provenance["unit_source"] = axis_policy.unit_source;

        std::map<std::string, std::vector<NodeId>> nodes_by_name;
        struct Pending
        {
            const aiNode* node;
            NodeId parent;
        };
        std::vector<Pending> stack{{ai.mRootNode, NodeId{}}};
        NodeId root_id;
        std::uint64_t visited = 0;
        while (!stack.empty())
        {
            const Pending pending = stack.back();
            stack.pop_back();
            if (++visited > 10'000'000ULL)
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_node_graph, "nodes", "node graph is unreasonably large or cyclic");
            const aiNode& source_node = *pending.node;
            Node node;
            node.name = ai_text(source_node.mName);
            node.transform_source = TransformSource::matrix;
            node.local_transform = to_mat4(source_node.mTransformation);
            if (!pending.parent.valid()) node.local_transform = multiply(axis_policy.correction, node.local_transform);
            if (!finite(node.local_transform))
                fail(ImportStatus::invalid_source, DiagnosticCode::non_finite_data, "nodes['" + node.name + "']", "non-finite node transform");
            node.parent = pending.parent;
            const NodeId id(static_cast<std::uint32_t>(scene.nodes.size()));
            if (!pending.parent.valid()) root_id = id;
            else scene.nodes[pending.parent.value()].children.push_back(id);

            if (source_node.mNumMeshes != 0U)
            {
                std::vector<std::uint32_t> list(source_node.mMeshes, source_node.mMeshes + source_node.mNumMeshes);
                if (const auto existing = meshes.meshes.find(list); existing != meshes.meshes.end())
                {
                    node.mesh = existing->second; // instancing preserved
                }
                else
                {
                    Mesh mesh;
                    mesh.name = node.name;
                    for (const std::uint32_t mesh_index : list)
                    {
                        if (mesh_index >= ai.mNumMeshes)
                            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, "nodes['" + node.name + "']", "mesh index out of range");
                        if (!converted[mesh_index])
                        {
                            meshes.primitives[mesh_index] = convert_primitive(ai, mesh_index, lacked_normals[mesh_index], scene, budget, diagnostics);
                            converted[mesh_index] = true;
                        }
                        if (meshes.primitives[mesh_index].has_value()) mesh.primitives.push_back(*meshes.primitives[mesh_index]);
                    }
                    if (!mesh.primitives.empty())
                    {
                        scene.meshes.push_back(std::move(mesh));
                        const MeshId mesh_id(static_cast<std::uint32_t>(scene.meshes.size() - 1U));
                        meshes.meshes.emplace(std::move(list), mesh_id);
                        node.mesh = mesh_id;
                    }
                }
            }
            nodes_by_name[node.name].push_back(id);
            scene.nodes.push_back(std::move(node));
            // Push children in reverse so they are visited (and numbered) in source order.
            for (unsigned child = source_node.mNumChildren; child-- > 0U;)
                if (source_node.mChildren[child] != nullptr) stack.push_back({source_node.mChildren[child], id});
        }
        std::uint64_t unreferenced = 0;
        for (unsigned index = 0; index < ai.mNumMeshes; ++index) unreferenced += converted[index] ? 0U : 1U;
        if (unreferenced != 0U)
            note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::feature_discarded, DiagnosticDisposition::ignored, "meshes",
                 "meshes not referenced by any node were discarded", "0", std::to_string(unreferenced));

        const auto attach_node = [&](const std::string& name, const char* kind) -> NodeId
        {
            const auto found = nodes_by_name.find(name);
            if (found == nodes_by_name.end())
            {
                note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::invalid_reference, DiagnosticDisposition::repaired, kind,
                     std::string(kind) + " has no node of the same name; attached to the scene root", {}, name);
                return root_id;
            }
            if (found->second.size() > 1U)
                note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::ambiguous_dependency, DiagnosticDisposition::observed, kind,
                     std::string(kind) + " name matches several nodes; the first in traversal order was used", {}, name);
            return found->second.front();
        };

        for (unsigned index = 0; index < ai.mNumCameras; ++index)
        {
            const aiCamera& source_camera = *ai.mCameras[index];
            const std::string location = "cameras[" + std::to_string(index) + "]";
            Camera camera;
            camera.name = ai_text(source_camera.mName);
            camera.aspect_ratio = source_camera.mAspect > 0.0F ? source_camera.mAspect : 0.0F;
            if (source_camera.mOrthographicWidth > 0.0F)
            {
                camera.type = CameraType::orthographic;
                camera.x_magnification = source_camera.mOrthographicWidth;
                camera.y_magnification = camera.aspect_ratio > 0.0F ? source_camera.mOrthographicWidth / camera.aspect_ratio
                                                                    : source_camera.mOrthographicWidth;
            }
            else
            {
                // Assimp stores HALF the horizontal field of view.
                const float half_horizontal = std::clamp(source_camera.mHorizontalFOV, 1.0e-3F, 1.5F);
                if (camera.aspect_ratio > 0.0F)
                {
                    camera.vertical_fov_radians = 2.0F * std::atan(std::tan(half_horizontal) / camera.aspect_ratio);
                }
                else
                {
                    camera.vertical_fov_radians = 2.0F * half_horizontal;
                    note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::invalid_camera, DiagnosticDisposition::defaulted, location,
                         "camera has no aspect ratio; the horizontal field of view is used as the vertical one");
                }
            }
            camera.near_plane = source_camera.mClipPlaneNear;
            camera.far_plane = source_camera.mClipPlaneFar;
            if (!(camera.near_plane > 0.0F) || !(camera.far_plane > camera.near_plane) || !std::isfinite(camera.far_plane))
            {
                note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::invalid_camera, DiagnosticDisposition::defaulted, location,
                     "invalid clip planes replaced by near=0.01 far=1000");
                camera.near_plane = 0.01F;
                camera.far_plane = 1000.0F;
            }
            scene.cameras.push_back(camera);
            const CameraId camera_id(static_cast<std::uint32_t>(scene.cameras.size() - 1U));
            NodeId node = attach_node(camera.name, "camera");
            const Vec3 position = to_vec3(source_camera.mPosition);
            const Vec3 forward = to_vec3(source_camera.mLookAt);
            const Vec3 up = to_vec3(source_camera.mUp);
            const std::optional<Mat4> frame = look_frame(position, forward, up);
            if (!frame.has_value())
                note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::invalid_camera, DiagnosticDisposition::defaulted, location,
                     "degenerate camera look-at; node orientation used");
            const bool needs_frame = frame.has_value() && !nearly_identity(*frame);
            if (needs_frame || scene.nodes[node.value()].camera.valid())
                node = add_child_node(scene, node, camera.name + "/camera-frame", frame.value_or(identity_matrix()));
            scene.nodes[node.value()].camera = camera_id;
        }

        for (unsigned index = 0; index < ai.mNumLights; ++index)
        {
            const aiLight& source_light = *ai.mLights[index];
            const std::string location = "lights[" + std::to_string(index) + "]";
            Light light;
            light.name = ai_text(source_light.mName);
            switch (source_light.mType)
            {
            case aiLightSource_DIRECTIONAL: light.type = LightType::directional; break;
            case aiLightSource_POINT: light.type = LightType::point; break;
            case aiLightSource_SPOT: light.type = LightType::spot; break;
            default:
                note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::feature_discarded, DiagnosticDisposition::ignored, location,
                     "ambient/area/undefined lights have no punctual equivalent and were discarded", {}, light.name);
                continue;
            }
            const aiColor3D& colour = source_light.mColorDiffuse;
            const float intensity = std::max({colour.r, colour.g, colour.b});
            if (!(intensity > 0.0F) || !std::isfinite(intensity))
            {
                note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::invalid_light, DiagnosticDisposition::ignored, location,
                     "light has no positive finite colour and was discarded", {}, light.name);
                continue;
            }
            light.color = {colour.r / intensity, colour.g / intensity, colour.b / intensity};
            light.intensity = intensity;
            note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::unit_converted, DiagnosticDisposition::converted, location,
                 "legacy light colour*intensity copied as glTF intensity; source photometric units are unspecified");
            if (light.type == LightType::spot)
            {
                const float outer = std::clamp(source_light.mAngleOuterCone * 0.5F, 1.0e-3F, std::numbers::pi_v<float> * 0.5F);
                const float inner = std::clamp(source_light.mAngleInnerCone * 0.5F, 0.0F, outer * 0.999F);
                light.outer_cone_angle = outer;
                light.inner_cone_angle = inner;
            }
            scene.lights.push_back(light);
            const LightId light_id(static_cast<std::uint32_t>(scene.lights.size() - 1U));
            NodeId node = attach_node(light.name, "light");
            const Vec3 position = to_vec3(source_light.mPosition);
            const Vec3 direction = light.type == LightType::point ? Vec3{0.0F, 0.0F, -1.0F} : to_vec3(source_light.mDirection);
            const std::optional<Mat4> frame = look_frame(position, direction, to_vec3(source_light.mUp));
            if (!frame.has_value())
                note(diagnostics, DiagnosticSeverity::warning, DiagnosticCode::invalid_light, DiagnosticDisposition::defaulted, location,
                     "degenerate light direction; node orientation used");
            const bool needs_frame = frame.has_value() && !nearly_identity(*frame);
            if (needs_frame || scene.nodes[node.value()].light.valid())
                node = add_child_node(scene, node, light.name + "/light-frame", frame.value_or(identity_matrix()));
            scene.nodes[node.value()].light = light_id;
        }

        SceneDefinition definition;
        definition.name = scene.source.display_name;
        definition.roots.push_back(root_id);
        scene.scenes.push_back(std::move(definition));
        scene.selected_scene = SceneDefinitionId(0);

        std::string graph_error;
        if (!propagate_world_transforms(scene, &graph_error))
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_node_graph, "nodes", graph_error);
        recompute_bounds(scene);

        // Dependencies Assimp itself opened (e.g. OBJ .mtl files) besides the source.
        for (const std::filesystem::path& opened : io->opened())
        {
            if (opened == absolute) continue;
            if (std::find(result.resolved_dependencies.begin(), result.resolved_dependencies.end(), opened) != result.resolved_dependencies.end())
                continue;
            const std::vector<std::byte> bytes = read_file(opened, settings.limits.maximum_dependency_bytes, "dependency");
            DependencyRecord record;
            record.normalized_relative_path = opened.lexically_relative(absolute.parent_path()).generic_string();
            record.sha256 = sha256_hex(std::span<const std::byte>(bytes));
            record.byte_size = bytes.size();
            scene.source.dependencies.push_back(record);
            result.resolved_dependencies.push_back(opened);
        }
        std::sort(scene.source.dependencies.begin(), scene.source.dependencies.end(),
                  [](const DependencyRecord& left, const DependencyRecord& right)
                  { return std::tie(left.normalized_relative_path, left.sha256) < std::tie(right.normalized_relative_path, right.sha256); });
        scene.source.dependencies.erase(
            std::unique(scene.source.dependencies.begin(), scene.source.dependencies.end(),
                        [](const DependencyRecord& left, const DependencyRecord& right)
                        { return left.normalized_relative_path == right.normalized_relative_path && left.sha256 == right.sha256; }),
            scene.source.dependencies.end());
        if (scene.source.generator.empty()) scene.source.generator = "assimp " + assimp_version();
        scene.source.deterministic_asset_key = compose_key(scene, settings_description);
        final_status = status_from(diagnostics);
    }
    catch (const ImportAbort& error)
    {
        diagnostics.push_back(error.diagnostic());
        final_status = error.status();
    }
    catch (const std::bad_alloc&)
    {
        diagnostics.push_back({DiagnosticSeverity::error, DiagnosticCode::resource_budget_exceeded, DiagnosticDisposition::rejected,
                               "import", "allocation failed while importing asset", {}, {}});
        final_status = ImportStatus::resource_limit;
    }
    catch (const std::exception& error)
    {
        diagnostics.push_back({DiagnosticSeverity::error, DiagnosticCode::internal_invariant, DiagnosticDisposition::rejected,
                               "import", std::string("unexpected compatibility import failure: ") + error.what(), {}, {}});
        final_status = ImportStatus::internal_error;
    }
    if (!is_success(final_status))
    {
        // Never hand out a partially converted scene; keep only the source metadata for the report.
        CanonicalScene empty;
        empty.source = std::move(scene.source);
        scene = std::move(empty);
    }
    result.import.report = make_import_report(scene, final_status, std::move(diagnostics), settings_description);
    result.import.report.provenance = std::move(provenance);
    result.import.scene = std::move(scene);
    return result;
}

#endif
}
