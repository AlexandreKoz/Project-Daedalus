#pragma once

#include "assets/CompatibilityImporter.h"
#include "assets/GltfImporter.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace daedalus
{
// Single entry point for loading any supported asset into the canonical scene (Campaign H):
//   .gltf/.glb      -> canonical glTF importer (unchanged Campaign B path)
//   .obj/.fbx/.3ds  -> CompatibilityImporter (Assimp, optional build feature)
//   .blend          -> BlenderWorker (external Blender, versioned script) -> GLB -> glTF importer
// with an optional content-addressed cache in front of all three. Every result carries an import
// report whose "provenance" records the adapter, converter versions and cache disposition.
enum class AssetSourceKind
{
    gltf,
    compatibility,
    blender,
    unsupported
};

struct AssetLoadOptions
{
    std::filesystem::path source;
    ImportSettings import_settings;
    std::optional<std::filesystem::path> cache_directory;     // unset = cache disabled
    std::vector<std::filesystem::path> texture_search_roots;  // compatibility formats only
    SourceUpAxis up_axis = SourceUpAxis::automatic;
    std::optional<float> metres_per_unit;
    std::optional<std::filesystem::path> blender_executable;  // unset = search PATH
    std::filesystem::path blender_script;                     // scripts/blender/daedalus_blend_export.py (required for .blend)
    std::uint32_t blender_timeout_seconds = 300;
    std::filesystem::path temporary_directory;                // conversion work area (.blend only)
    const std::atomic<bool>* cancel = nullptr;
};

struct AssetLoadResult
{
    ImportResult import;
    AssetSourceKind kind = AssetSourceKind::unsupported;
    bool cache_hit = false;
    std::string cache_key;
    // Absolute files (besides the source) the import read; recorded with the cache entry.
    std::vector<std::filesystem::path> dependencies;
    // Human-readable lines for logs (adapter, cache, converter outcomes).
    std::vector<std::string> notes;
};

[[nodiscard]] AssetSourceKind classify_asset_source(const std::filesystem::path& source);
[[nodiscard]] std::string_view to_string(AssetSourceKind kind) noexcept;
[[nodiscard]] AssetLoadResult load_asset(const AssetLoadOptions& options);
}
