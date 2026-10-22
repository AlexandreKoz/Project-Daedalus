#include "assets/TextureResolver.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace daedalus
{
namespace
{
[[nodiscard]] std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

[[nodiscard]] bool is_regular_file_safe(const std::filesystem::path& path) noexcept
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

[[nodiscard]] bool within(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    std::error_code root_error;
    std::error_code candidate_error;
    const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(root, root_error);
    const std::filesystem::path canonical_candidate = std::filesystem::weakly_canonical(candidate, candidate_error);
    if (root_error || candidate_error) return false;
    auto root_iterator = canonical_root.begin();
    auto candidate_iterator = canonical_candidate.begin();
    for (; root_iterator != canonical_root.end(); ++root_iterator, ++candidate_iterator)
    {
        if (root_iterator->empty()) continue;
        if (candidate_iterator == canonical_candidate.end() || *root_iterator != *candidate_iterator) return false;
    }
    return true;
}

// Walks `relative` component by component below `root`, matching each component case-insensitively.
// Directory entries are visited in sorted order so ties resolve deterministically.
[[nodiscard]] std::filesystem::path case_insensitive_lookup(const std::filesystem::path& root, const std::filesystem::path& relative)
{
    std::filesystem::path current = root;
    for (const std::filesystem::path& component : relative)
    {
        const std::string wanted = lowercase(component.generic_string());
        if (wanted.empty() || wanted == ".") continue;
        std::error_code error;
        if (!std::filesystem::is_directory(current, error) || error) return {};
        std::vector<std::filesystem::path> entries;
        for (std::filesystem::directory_iterator iterator(current, error), end; !error && iterator != end; iterator.increment(error))
            entries.push_back(iterator->path());
        if (error) return {};
        std::sort(entries.begin(), entries.end());
        const auto match = std::find_if(entries.begin(), entries.end(), [&](const std::filesystem::path& entry)
        {
            return lowercase(entry.filename().generic_string()) == wanted;
        });
        if (match == entries.end()) return {};
        current = *match;
    }
    return is_regular_file_safe(current) ? current : std::filesystem::path{};
}

[[nodiscard]] bool has_parent_reference(const std::filesystem::path& path)
{
    for (const auto& component : path)
        if (component == "..") return true;
    return false;
}
}

std::string normalize_texture_reference(std::string_view reference)
{
    std::string result(reference);
    std::replace(result.begin(), result.end(), '\\', '/');
    while (result.starts_with("./")) result.erase(0, 2);
    // Collapse duplicate separators produced by some exporters.
    std::string collapsed;
    collapsed.reserve(result.size());
    for (const char c : result)
    {
        if (c == '/' && !collapsed.empty() && collapsed.back() == '/') continue;
        collapsed.push_back(c);
    }
    return collapsed;
}

bool texture_reference_has_uri_scheme(std::string_view reference) noexcept
{
    const std::size_t colon = reference.find(':');
    if (colon == std::string_view::npos || colon < 2U) return false; // "C:" drive letters are not schemes
    for (std::size_t index = 0; index < colon; ++index)
    {
        const char c = reference[index];
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.')) return false;
    }
    return true;
}

bool texture_reference_is_absolute(std::string_view reference) noexcept
{
    if (reference.empty()) return false;
    if (reference[0] == '/' || reference[0] == '\\') return true;
    return reference.size() >= 2U && std::isalpha(static_cast<unsigned char>(reference[0])) && reference[1] == ':';
}

std::string to_string(TextureResolveStatus status)
{
    switch (status)
    {
    case TextureResolveStatus::resolved: return "resolved";
    case TextureResolveStatus::resolved_ambiguous: return "resolved_ambiguous";
    case TextureResolveStatus::missing: return "missing";
    case TextureResolveStatus::rejected: return "rejected";
    }
    return "missing";
}

TextureResolution resolve_texture_reference(std::string_view reference, const TextureResolveOptions& options)
{
    TextureResolution result;
    result.normalized_reference = normalize_texture_reference(reference);
    if (result.normalized_reference.empty())
    {
        result.status = TextureResolveStatus::rejected;
        result.message = "empty texture reference";
        return result;
    }
    if (result.normalized_reference.find('\0') != std::string::npos)
    {
        result.status = TextureResolveStatus::rejected;
        result.message = "texture reference contains a NUL byte";
        return result;
    }
    if (texture_reference_has_uri_scheme(result.normalized_reference))
    {
        result.status = TextureResolveStatus::rejected;
        result.message = "URI schemes are not resolved (no network or file:// access)";
        return result;
    }

    std::vector<std::filesystem::path> roots;
    roots.push_back(options.asset_directory);
    for (const std::filesystem::path& root : options.search_roots) roots.push_back(root);
    auto allowed = [&](const std::filesystem::path& candidate)
    {
        return std::any_of(roots.begin(), roots.end(), [&](const std::filesystem::path& root) { return within(root, candidate); });
    };
    auto accept = [&](const std::filesystem::path& candidate, std::string strategy)
    {
        result.status = TextureResolveStatus::resolved;
        result.path = candidate;
        result.strategy = std::move(strategy);
        return result;
    };

    const bool absolute = texture_reference_is_absolute(result.normalized_reference);
    const std::filesystem::path relative = std::filesystem::path(result.normalized_reference).lexically_normal();
    if (!absolute)
    {
        for (std::size_t root_index = 0; root_index < roots.size(); ++root_index)
        {
            const std::filesystem::path& root = roots[root_index];
            const std::filesystem::path candidate = root / relative;
            if (is_regular_file_safe(candidate) && allowed(candidate))
                return accept(candidate, root_index == 0U ? "exact" : "search-root");
            if (!has_parent_reference(relative))
            {
                const std::filesystem::path insensitive = case_insensitive_lookup(root, relative);
                if (!insensitive.empty() && allowed(insensitive))
                    return accept(insensitive, root_index == 0U ? "case-insensitive" : "search-root-case-insensitive");
            }
        }
        if (has_parent_reference(relative))
        {
            const std::filesystem::path candidate = options.asset_directory / relative;
            if (is_regular_file_safe(candidate) && !allowed(candidate))
            {
                result.status = TextureResolveStatus::rejected;
                result.message = "relative reference escapes the asset directory and every configured search root";
                return result;
            }
        }
    }

    // Basename fallback (also the only route for authoring-machine absolute paths).
    const std::string basename = relative.filename().generic_string();
    if (basename.empty())
    {
        result.status = TextureResolveStatus::missing;
        result.message = "texture reference has no file name";
        return result;
    }
    for (const std::filesystem::path& root : roots)
    {
        // Prefer a top-level match, then search nested texture directories. Do not
        // follow directory symlinks or allow a candidate outside a configured root.
        std::vector<std::filesystem::path> matches;
        const std::filesystem::path direct = case_insensitive_lookup(root, basename);
        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator(root, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end)
        {
            const auto& entry = *iterator;
            std::error_code entry_error;
            if (lowercase(entry.path().filename().generic_string()) == lowercase(basename) &&
                entry.is_regular_file(entry_error) && !entry_error)
                matches.push_back(entry.path());
            iterator.increment(error);
        }
        std::sort(matches.begin(), matches.end());
        matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
        // A texture next to the asset wins over identically named nested files.
        if (!direct.empty()) matches.insert(matches.begin(), direct);
        for (const auto& candidate : matches)
            if (allowed(candidate) &&
                std::find(result.candidates.begin(), result.candidates.end(), candidate) == result.candidates.end())
                result.candidates.push_back(candidate);
    }
    if (result.candidates.empty())
    {
        result.status = TextureResolveStatus::missing;
        result.message = absolute ? "absolute authoring path not opened; basename not found in the asset directory or search roots"
                                  : "texture not found in the asset directory or search roots";
        return result;
    }
    result.path = result.candidates.front();
    result.strategy = "basename";
    result.status = result.candidates.size() > 1U ? TextureResolveStatus::resolved_ambiguous : TextureResolveStatus::resolved;
    if (result.candidates.size() > 1U) result.message = "several files share this basename; the first in search order was used";
    return result;
}
}
