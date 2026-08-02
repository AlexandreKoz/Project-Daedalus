#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace daedalus
{
enum class ImportStatus
{
    success,
    success_with_warnings,
    success_with_repairs,
    unsupported_feature,
    invalid_source,
    missing_dependency,
    io_failure,
    resource_limit,
    internal_error
};

enum class DiagnosticSeverity
{
    information,
    warning,
    error
};

enum class DiagnosticDisposition
{
    rejected,
    ignored,
    defaulted,
    converted,
    repaired,
    observed
};

enum class DiagnosticCode : std::uint32_t
{
    none = 0,
    io_open_failed,
    source_too_large,
    malformed_json,
    invalid_glb_header,
    invalid_glb_chunk,
    unsupported_version,
    unsupported_required_extension,
    unsupported_optional_extension,
    invalid_reference,
    invalid_buffer_range,
    invalid_accessor,
    invalid_stride,
    unsupported_sparse_accessor,
    unsupported_component_type,
    unsupported_accessor_shape,
    unsupported_primitive_mode,
    missing_position_attribute,
    missing_optional_attribute,
    non_finite_data,
    index_out_of_range,
    empty_primitive,
    degenerate_triangle,
    invalid_node_graph,
    singular_transform,
    invalid_camera,
    invalid_light,
    missing_dependency,
    unsafe_dependency_path,
    invalid_image,
    unsupported_image_encoding,
    texture_transform_unsupported,
    default_scene_selected,
    implicit_scene_created,
    bounds_recomputed,
    internal_invariant
};

struct Diagnostic
{
    DiagnosticSeverity severity = DiagnosticSeverity::information;
    DiagnosticCode code = DiagnosticCode::none;
    DiagnosticDisposition disposition = DiagnosticDisposition::observed;
    std::string location;
    std::string message;
    std::string expected;
    std::string observed;
};

[[nodiscard]] std::string_view to_string(ImportStatus value) noexcept;
[[nodiscard]] std::string_view to_string(DiagnosticSeverity value) noexcept;
[[nodiscard]] std::string_view to_string(DiagnosticDisposition value) noexcept;
[[nodiscard]] std::string_view to_string(DiagnosticCode value) noexcept;
[[nodiscard]] bool is_success(ImportStatus value) noexcept;
void sort_diagnostics(std::vector<Diagnostic>& diagnostics);
}
