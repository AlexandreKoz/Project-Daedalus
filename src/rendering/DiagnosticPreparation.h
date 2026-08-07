#pragma once

#include "scene/Scene.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace daedalus
{
struct PreparedDiagnosticDraw
{
    std::uint32_t primitive_index = 0;
    Mat4 world = identity_matrix();
    Vec4 base_color_factor{1.0F, 1.0F, 1.0F, 1.0F};
    std::optional<TextureId> base_color_texture;
    std::uint32_t texture_coord_set = 0;
    bool uses_default_material = true;
    bool selected_texcoord_available = false;
    bool negative_determinant = false;
};

// Converts canonical scene instances into renderer-neutral draw preparation. This is intentionally
// small: it proves material defaulting, UV-set selection, and mesh instancing without D3D12.
[[nodiscard]] std::vector<PreparedDiagnosticDraw> prepare_diagnostic_draws(const CanonicalScene& scene);
}
