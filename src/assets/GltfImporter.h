#pragma once

#include "assets/ImportReport.h"
#include "scene/Scene.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace daedalus
{
struct ImportSettings
{
    std::optional<std::string> scene_selector;
    std::uint64_t maximum_source_bytes = 128ULL * 1024ULL * 1024ULL;
    std::uint64_t maximum_dependency_bytes = 512ULL * 1024ULL * 1024ULL;
    // Cumulative retained bytes for source, buffer payloads, encoded images, canonical geometry, and decoded RGBA8 pixels.
    std::uint64_t maximum_total_decoded_bytes = 1024ULL * 1024ULL * 1024ULL;
    // Conservative peak = retained bytes plus the largest simultaneously-live decode/accessor scratch allocation.
    std::uint64_t maximum_peak_bytes = 1536ULL * 1024ULL * 1024ULL;
    bool reject_path_traversal = true;

    [[nodiscard]] std::string deterministic_description() const;
};

struct ImportResult
{
    CanonicalScene scene;
    ImportReport report;

    [[nodiscard]] bool succeeded() const noexcept;
};

class GltfImporter final
{
public:
    [[nodiscard]] ImportResult import_file(const std::filesystem::path& source,
                                           const ImportSettings& settings = {}) const;
};
}
