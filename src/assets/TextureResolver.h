#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace daedalus
{
// Deterministic texture dependency resolution for compatibility formats (OBJ/FBX/3DS, converted
// .blend packages). Resolution order (first match wins, every step in stable order):
//   1. reference relative to the asset directory, exact case;
//   2. same, case-insensitive component matching (Windows-authored paths on any filesystem);
//   3. each configured search root in the given order: exact, then case-insensitive;
//   4. basename-only search in the asset directory, then each search root (non-recursive).
// Absolute references and drive-qualified paths from the authoring machine are NEVER opened
// directly; only their basename participates in step 4. URI schemes (http:, file:, ...) are
// rejected. Every resolved path must canonicalize inside the asset directory or a search root.
struct TextureResolveOptions
{
    std::filesystem::path asset_directory;
    std::vector<std::filesystem::path> search_roots;
};

enum class TextureResolveStatus
{
    resolved,
    resolved_ambiguous, // several distinct basename candidates; the first in search order was used
    missing,
    rejected
};

struct TextureResolution
{
    TextureResolveStatus status = TextureResolveStatus::missing;
    std::filesystem::path path;
    std::string normalized_reference;  // forward slashes, no leading "./"
    std::string strategy;              // exact, case-insensitive, search-root, search-root-case-insensitive, basename
    std::vector<std::filesystem::path> candidates;
    std::string message;
};

[[nodiscard]] std::string normalize_texture_reference(std::string_view reference);
[[nodiscard]] bool texture_reference_has_uri_scheme(std::string_view reference) noexcept;
[[nodiscard]] bool texture_reference_is_absolute(std::string_view reference) noexcept;
[[nodiscard]] TextureResolution resolve_texture_reference(std::string_view reference, const TextureResolveOptions& options);
[[nodiscard]] std::string to_string(TextureResolveStatus status);
}
