#include "assets/AssetCache.h"

#include "assets/SceneSerialization.h"
#include "core/Sha256.h"

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace daedalus
{
namespace
{
constexpr std::array<char, 8> kMagic{'D', 'A', 'E', 'D', 'C', 'A', 'C', 'H'};
constexpr std::size_t kHeaderBytes = 8U + 4U + 8U + 32U;

void put_u32(std::vector<std::byte>& bytes, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}

void put_u64(std::vector<std::byte>& bytes, std::uint64_t value)
{
    for (int shift = 0; shift < 64; shift += 8) bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}

void put_string(std::vector<std::byte>& bytes, const std::string& value)
{
    put_u64(bytes, value.size());
    for (const char c : value) bytes.push_back(static_cast<std::byte>(c));
}

class Cursor final
{
public:
    Cursor(const std::vector<std::byte>& bytes, std::size_t offset) : bytes_(bytes), offset_(offset) {}
    std::uint32_t u32()
    {
        require(4U);
        std::uint32_t value = 0;
        for (int shift = 0; shift < 32; shift += 8) value |= static_cast<std::uint32_t>(bytes_[offset_++]) << shift;
        return value;
    }
    std::uint64_t u64()
    {
        require(8U);
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 8) value |= static_cast<std::uint64_t>(bytes_[offset_++]) << shift;
        return value;
    }
    std::string string()
    {
        const std::uint64_t size = u64();
        require(size);
        std::string value(reinterpret_cast<const char*>(bytes_.data() + offset_), static_cast<std::size_t>(size));
        offset_ += static_cast<std::size_t>(size);
        return value;
    }
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::uint64_t remaining() const noexcept { return bytes_.size() - offset_; }

private:
    void require(std::uint64_t size) const
    {
        if (size > remaining()) throw std::runtime_error("cache entry is truncated");
    }
    const std::vector<std::byte>& bytes_;
    std::size_t offset_ = 0;
};

[[nodiscard]] std::array<std::byte, 32> digest(std::span<const std::byte> bytes)
{
    Sha256 hash;
    hash.update(bytes);
    return hash.finish();
}

[[nodiscard]] std::string hex(const std::array<std::byte, 32>& value)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string output(64, '0');
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        const unsigned byte = static_cast<unsigned>(value[index]);
        output[index * 2U] = digits[byte >> 4U];
        output[index * 2U + 1U] = digits[byte & 0x0FU];
    }
    return output;
}

[[nodiscard]] std::vector<std::byte> read_all(const std::filesystem::path& path, std::uint64_t maximum_bytes)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) throw std::runtime_error("cannot stat " + path.string() + ": " + error.message());
    if (size > maximum_bytes) throw std::runtime_error("file exceeds the configured size limit: " + path.string());
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path.string());
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (size != 0U) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!input) throw std::runtime_error("cannot read " + path.string());
    return bytes;
}
}

std::string compute_asset_cache_key(const CacheKeyInputs& inputs)
{
    if (inputs.source_sha256.size() != 64U) throw std::invalid_argument("cache key requires the source SHA-256");
    Sha256 hash;
    std::ostringstream canonical;
    canonical << "daedalus-asset-cache/" << kAssetCacheFormatVersion << '\n'
              << "canonical-schema=" << CanonicalScene::schema_version << '\n'
              << "scene-serialization=" << kSceneSerializationVersion << '\n'
              << "adapter=" << inputs.adapter_identity << '\n'
              << "settings=" << inputs.import_settings << '\n';
    for (const std::filesystem::path& root : inputs.texture_search_roots) canonical << "search-root=" << root.generic_string() << '\n';
    canonical << "source=" << inputs.source_sha256 << '\n';
    hash.update(canonical.str());
    return hex(hash.finish());
}

std::string sha256_file_hex(const std::filesystem::path& path, std::uint64_t maximum_bytes)
{
    const std::vector<std::byte> bytes = read_all(path, maximum_bytes);
    return hex(digest(bytes));
}

std::vector<std::byte> encode_cache_entry(const CachedAsset& asset)
{
    std::vector<std::byte> payload;
    put_string(payload, asset.report_json);
    put_u64(payload, asset.dependencies.size());
    for (const CacheDependency& dependency : asset.dependencies)
    {
        put_string(payload, dependency.resolved_path.generic_string());
        put_string(payload, dependency.sha256);
        put_u64(payload, dependency.byte_size);
    }
    const std::vector<std::byte> scene = serialize_scene(asset.scene);
    payload.insert(payload.end(), scene.begin(), scene.end());

    std::vector<std::byte> file;
    file.reserve(kHeaderBytes + payload.size());
    for (const char c : kMagic) file.push_back(static_cast<std::byte>(c));
    put_u32(file, kAssetCacheFormatVersion);
    put_u64(file, payload.size());
    const std::array<std::byte, 32> checksum = digest(payload);
    file.insert(file.end(), checksum.begin(), checksum.end());
    file.insert(file.end(), payload.begin(), payload.end());
    return file;
}

CachedAsset decode_cache_entry(const std::vector<std::byte>& file_bytes)
{
    if (file_bytes.size() < kHeaderBytes) throw std::runtime_error("cache entry is smaller than its header");
    for (std::size_t index = 0; index < kMagic.size(); ++index)
        if (file_bytes[index] != static_cast<std::byte>(kMagic[index])) throw std::runtime_error("cache entry has the wrong magic");
    Cursor header(file_bytes, kMagic.size());
    if (header.u32() != kAssetCacheFormatVersion) throw std::runtime_error("cache entry format version differs");
    const std::uint64_t payload_size = header.u64();
    if (payload_size != file_bytes.size() - kHeaderBytes) throw std::runtime_error("cache entry payload size does not match the file size");
    const std::span<const std::byte> payload(file_bytes.data() + kHeaderBytes, static_cast<std::size_t>(payload_size));
    std::array<std::byte, 32> stored{};
    std::memcpy(stored.data(), file_bytes.data() + kMagic.size() + 12U, stored.size());
    if (digest(payload) != stored) throw std::runtime_error("cache entry checksum mismatch");

    Cursor cursor(file_bytes, kHeaderBytes);
    CachedAsset asset;
    asset.report_json = cursor.string();
    const std::uint64_t dependency_count = cursor.u64();
    if (dependency_count > cursor.remaining() / 24U) throw std::runtime_error("cache entry dependency count is corrupt");
    for (std::uint64_t index = 0; index < dependency_count; ++index)
    {
        CacheDependency dependency;
        dependency.resolved_path = std::filesystem::path(cursor.string());
        dependency.sha256 = cursor.string();
        dependency.byte_size = cursor.u64();
        asset.dependencies.push_back(std::move(dependency));
    }
    asset.scene = deserialize_scene(std::span<const std::byte>(file_bytes.data() + cursor.offset(),
                                                               static_cast<std::size_t>(cursor.remaining())));
    return asset;
}

AssetCache::AssetCache(std::filesystem::path directory) : directory_(std::move(directory))
{
    if (directory_.empty()) throw std::invalid_argument("asset cache directory must not be empty");
    std::filesystem::create_directories(directory_);
}

std::filesystem::path AssetCache::entry_path(const std::string& key) const
{
    if (key.size() != 64U || key.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::invalid_argument("asset cache key must be a lowercase SHA-256 hex string");
    return directory_ / (key + ".dcache");
}

std::optional<CachedAsset> AssetCache::load(const std::string& key, std::vector<Diagnostic>& diagnostics) const
{
    const std::filesystem::path path = entry_path(key);
    std::error_code exists_error;
    if (!std::filesystem::exists(path, exists_error)) return std::nullopt;
    auto discard = [&](DiagnosticCode code, const std::string& reason)
    {
        std::error_code remove_error;
        std::filesystem::remove(path, remove_error);
        diagnostics.push_back({DiagnosticSeverity::warning, code, DiagnosticDisposition::repaired, "asset-cache",
                               reason + "; the entry was discarded and the asset re-imported", {}, path.generic_string()});
    };
    CachedAsset asset;
    try
    {
        asset = decode_cache_entry(read_all(path, kAssetCacheMaximumBytes));
    }
    catch (const std::exception& error)
    {
        discard(DiagnosticCode::cache_corrupt, std::string("cache entry is corrupt (") + error.what() + ")");
        return std::nullopt;
    }
    for (const CacheDependency& dependency : asset.dependencies)
    {
        std::string current;
        try
        {
            current = sha256_file_hex(dependency.resolved_path, kAssetCacheMaximumBytes);
        }
        catch (const std::exception& error)
        {
            discard(DiagnosticCode::cache_stale, "cached dependency is missing or unreadable: " + dependency.resolved_path.generic_string() +
                                                     " (" + error.what() + ")");
            return std::nullopt;
        }
        if (current != dependency.sha256)
        {
            discard(DiagnosticCode::cache_stale, "cached dependency changed: " + dependency.resolved_path.generic_string());
            return std::nullopt;
        }
    }
    diagnostics.push_back({DiagnosticSeverity::information, DiagnosticCode::cache_hit, DiagnosticDisposition::observed, "asset-cache",
                           "canonical scene loaded from the content-addressed cache; all dependencies re-verified", {}, key});
    return asset;
}

void AssetCache::store(const std::string& key, const CachedAsset& asset) const
{
    const std::filesystem::path final_path = entry_path(key);
    const std::vector<std::byte> bytes = encode_cache_entry(asset);
    std::random_device device;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count() ^ static_cast<long long>(device());
    const std::filesystem::path temporary = directory_ / (key + ".tmp-" + std::to_string(nonce));
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot create cache temporary file " + temporary.string());
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) throw std::runtime_error("cannot write cache temporary file " + temporary.string());
    }
    std::error_code error;
    // std::filesystem::rename replaces an existing destination atomically on the same volume
    // (MoveFileExW(MOVEFILE_REPLACE_EXISTING) on Windows, rename(2) on POSIX).
    std::filesystem::rename(temporary, final_path, error);
    if (error)
    {
        std::error_code cleanup;
        std::filesystem::remove(temporary, cleanup);
        throw std::runtime_error("cannot publish cache entry " + final_path.string() + ": " + error.message());
    }
}
}
