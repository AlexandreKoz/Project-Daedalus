#pragma once

#include "assets/Diagnostics.h"
#include "scene/Math.h"
#include "scene/Scene.h"

#include <cstdint>
#include <string>
#include <vector>

namespace daedalus
{
struct ImportCounts
{
    std::uint64_t scenes = 0;
    std::uint64_t nodes = 0;
    std::uint64_t roots = 0;
    std::uint64_t meshes = 0;
    std::uint64_t primitives = 0;
    std::uint64_t vertices = 0;
    std::uint64_t indices = 0;
    std::uint64_t materials = 0;
    std::uint64_t textures = 0;
    std::uint64_t images = 0;
    std::uint64_t samplers = 0;
    std::uint64_t cameras = 0;
    std::uint64_t lights = 0;
};

struct ImportReport
{
    static constexpr std::string_view schema_version = "daedalus.import-report/1";
    std::string tool_version;
    std::string canonical_schema_version;
    std::string source_display_name;
    std::string source_format;
    std::string source_sha256;
    std::string deterministic_asset_key;
    std::string selected_scene;
    std::string import_settings;
    ImportCounts counts;
    Aabb local_bounds;
    Aabb world_bounds;
    std::vector<DependencyRecord> dependencies;
    std::vector<std::string> extensions_used;
    std::vector<std::string> extensions_required;
    std::vector<std::string> extensions_supported;
    std::vector<std::string> extensions_ignored;
    std::vector<std::string> extensions_rejected;
    std::vector<Diagnostic> diagnostics;
    ImportStatus status = ImportStatus::internal_error;

    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] std::string summary() const;
};

[[nodiscard]] ImportReport make_import_report(const CanonicalScene& scene,
                                              ImportStatus status,
                                              std::vector<Diagnostic> diagnostics,
                                              std::string import_settings);
}
