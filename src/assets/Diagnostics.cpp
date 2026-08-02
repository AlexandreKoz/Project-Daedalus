#include "assets/Diagnostics.h"

#include <algorithm>
#include <tuple>

namespace daedalus
{
std::string_view to_string(ImportStatus value) noexcept
{
    switch (value)
    {
    case ImportStatus::success: return "success";
    case ImportStatus::success_with_warnings: return "success_with_warnings";
    case ImportStatus::success_with_repairs: return "success_with_repairs";
    case ImportStatus::unsupported_feature: return "unsupported_feature";
    case ImportStatus::invalid_source: return "invalid_source";
    case ImportStatus::missing_dependency: return "missing_dependency";
    case ImportStatus::io_failure: return "io_failure";
    case ImportStatus::resource_limit: return "resource_limit";
    case ImportStatus::internal_error: return "internal_error";
    }
    return "internal_error";
}

std::string_view to_string(DiagnosticSeverity value) noexcept
{
    switch (value)
    {
    case DiagnosticSeverity::information: return "information";
    case DiagnosticSeverity::warning: return "warning";
    case DiagnosticSeverity::error: return "error";
    }
    return "error";
}

std::string_view to_string(DiagnosticDisposition value) noexcept
{
    switch (value)
    {
    case DiagnosticDisposition::rejected: return "rejected";
    case DiagnosticDisposition::ignored: return "ignored";
    case DiagnosticDisposition::defaulted: return "defaulted";
    case DiagnosticDisposition::converted: return "converted";
    case DiagnosticDisposition::repaired: return "repaired";
    case DiagnosticDisposition::observed: return "observed";
    }
    return "observed";
}

std::string_view to_string(DiagnosticCode value) noexcept
{
    switch (value)
    {
    case DiagnosticCode::none: return "none";
    case DiagnosticCode::io_open_failed: return "io_open_failed";
    case DiagnosticCode::source_too_large: return "source_too_large";
    case DiagnosticCode::malformed_json: return "malformed_json";
    case DiagnosticCode::invalid_glb_header: return "invalid_glb_header";
    case DiagnosticCode::invalid_glb_chunk: return "invalid_glb_chunk";
    case DiagnosticCode::unsupported_version: return "unsupported_version";
    case DiagnosticCode::unsupported_required_extension: return "unsupported_required_extension";
    case DiagnosticCode::unsupported_optional_extension: return "unsupported_optional_extension";
    case DiagnosticCode::invalid_reference: return "invalid_reference";
    case DiagnosticCode::invalid_buffer_range: return "invalid_buffer_range";
    case DiagnosticCode::invalid_accessor: return "invalid_accessor";
    case DiagnosticCode::invalid_stride: return "invalid_stride";
    case DiagnosticCode::unsupported_sparse_accessor: return "unsupported_sparse_accessor";
    case DiagnosticCode::unsupported_component_type: return "unsupported_component_type";
    case DiagnosticCode::unsupported_accessor_shape: return "unsupported_accessor_shape";
    case DiagnosticCode::unsupported_primitive_mode: return "unsupported_primitive_mode";
    case DiagnosticCode::missing_position_attribute: return "missing_position_attribute";
    case DiagnosticCode::missing_optional_attribute: return "missing_optional_attribute";
    case DiagnosticCode::non_finite_data: return "non_finite_data";
    case DiagnosticCode::index_out_of_range: return "index_out_of_range";
    case DiagnosticCode::empty_primitive: return "empty_primitive";
    case DiagnosticCode::degenerate_triangle: return "degenerate_triangle";
    case DiagnosticCode::invalid_node_graph: return "invalid_node_graph";
    case DiagnosticCode::singular_transform: return "singular_transform";
    case DiagnosticCode::invalid_camera: return "invalid_camera";
    case DiagnosticCode::invalid_light: return "invalid_light";
    case DiagnosticCode::missing_dependency: return "missing_dependency";
    case DiagnosticCode::unsafe_dependency_path: return "unsafe_dependency_path";
    case DiagnosticCode::invalid_image: return "invalid_image";
    case DiagnosticCode::unsupported_image_encoding: return "unsupported_image_encoding";
    case DiagnosticCode::texture_transform_unsupported: return "texture_transform_unsupported";
    case DiagnosticCode::default_scene_selected: return "default_scene_selected";
    case DiagnosticCode::implicit_scene_created: return "implicit_scene_created";
    case DiagnosticCode::bounds_recomputed: return "bounds_recomputed";
    case DiagnosticCode::internal_invariant: return "internal_invariant";
    }
    return "internal_invariant";
}

bool is_success(ImportStatus value) noexcept
{
    return value == ImportStatus::success || value == ImportStatus::success_with_warnings || value == ImportStatus::success_with_repairs;
}

void sort_diagnostics(std::vector<Diagnostic>& diagnostics)
{
    std::stable_sort(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& left, const Diagnostic& right)
    {
        return std::tie(left.severity, left.code, left.location, left.message, left.expected, left.observed) <
               std::tie(right.severity, right.code, right.location, right.message, right.expected, right.observed);
    });
}
}
