#pragma once

#include "assets/Diagnostics.h"
#include "scene/Scene.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace daedalus
{
// Content-addressed canonical-scene cache (Campaign H).
//
// Cache key = SHA-256 over: cache format version, canonical schema version, scene serialization
// version, adapter/converter identity (incl. versions), the deterministic import-settings string,
// the ordered texture search roots, and the SHA-256 of the source bytes. Dependency content is
// verified at lookup time: every entry records each resolved dependency (path + SHA-256 + size)
// and a lookup re-hashes them; any change or missing file marks the entry stale.
//
// File format (<key>.dcache):
//   magic "DAEDCACH" | u32 format version | u64 payload size | 32-byte SHA-256(payload) | payload
// payload = u64 report size | report JSON | u64 dependency count | {path, sha256, size}... |
//           serialized CanonicalScene (SceneSerialization).
// Writes go to a unique temporary file in the cache directory followed by an atomic rename;
// readers never trust a partial file (size/magic/version/checksum are all validated), and a
// corrupt entry is deleted and reported rather than used.
inline constexpr std::uint32_t kAssetCacheFormatVersion = 1U;
inline constexpr std::uint64_t kAssetCacheMaximumBytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;

struct CacheDependency
{
    std::filesystem::path resolved_path;
    std::string sha256;
    std::uint64_t byte_size = 0;
};

struct CacheKeyInputs
{
    std::string source_sha256;
    std::string adapter_identity;
    std::string import_settings;
    std::vector<std::filesystem::path> texture_search_roots;
};

struct CachedAsset
{
    CanonicalScene scene;
    std::string report_json;
    std::vector<CacheDependency> dependencies;
};

[[nodiscard]] std::string compute_asset_cache_key(const CacheKeyInputs& inputs);
[[nodiscard]] std::string sha256_file_hex(const std::filesystem::path& path, std::uint64_t maximum_bytes);
[[nodiscard]] std::vector<std::byte> encode_cache_entry(const CachedAsset& asset);
// Throws std::runtime_error describing the exact corruption.
[[nodiscard]] CachedAsset decode_cache_entry(const std::vector<std::byte>& file_bytes);

class AssetCache final
{
public:
    explicit AssetCache(std::filesystem::path directory);

    // Returns the cached asset when present, intact and not stale. Diagnostics describe hits,
    // corruption (entry deleted) and staleness (entry deleted).
    [[nodiscard]] std::optional<CachedAsset> load(const std::string& key, std::vector<Diagnostic>& diagnostics) const;
    void store(const std::string& key, const CachedAsset& asset) const;
    [[nodiscard]] std::filesystem::path entry_path(const std::string& key) const;
    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }

private:
    std::filesystem::path directory_;
};
}
