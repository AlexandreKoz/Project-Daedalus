function(daedalus_find_dxc output_variable)
    set(DXC_PATH "" CACHE STRING "Path to dxc.exe or to the directory containing it")

    set(_explicit_path "${DXC_PATH}")
    if(NOT _explicit_path AND DEFINED ENV{DXC_PATH})
        set(_explicit_path "$ENV{DXC_PATH}")
    endif()

    set(_candidate_paths)
    if(_explicit_path)
        if(IS_DIRECTORY "${_explicit_path}")
            list(APPEND _candidate_paths "${_explicit_path}/dxc.exe" "${_explicit_path}/dxc")
        else()
            list(APPEND _candidate_paths "${_explicit_path}")
        endif()
    endif()

    if(WIN32)
        if(DEFINED ENV{WindowsSdkDir} AND DEFINED ENV{WindowsSDKVersion})
            list(APPEND _candidate_paths "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64/dxc.exe")
        endif()
        file(GLOB _sdk_dxc_candidates LIST_DIRECTORIES false "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxc.exe")
        list(SORT _sdk_dxc_candidates COMPARE NATURAL ORDER DESCENDING)
        list(APPEND _candidate_paths ${_sdk_dxc_candidates})
    endif()

    foreach(_candidate IN LISTS _candidate_paths)
        if(EXISTS "${_candidate}" AND NOT IS_DIRECTORY "${_candidate}")
            set(${output_variable} "${_candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    find_program(_dxc_executable NAMES dxc.exe dxc)
    if(_dxc_executable)
        set(${output_variable} "${_dxc_executable}" PARENT_SCOPE)
        return()
    endif()

    message(FATAL_ERROR
        "DirectX Shader Compiler was not found. Install a recent Windows SDK or official DXC release, "
        "then set -DDXC_PATH=<path-to-dxc.exe> or the DXC_PATH environment variable. FXC fallback is not supported.")
endfunction()

function(daedalus_compile_shader_pair target_name shader_source output_directory vertex_filename pixel_filename)
    daedalus_find_dxc(_dxc)
    message(STATUS "Using DirectX Shader Compiler: ${_dxc}")

    get_filename_component(_shader_include_directory "${shader_source}" DIRECTORY)
    set(_configuration_output_directory "${output_directory}/$<CONFIG>")
    set(_vertex_output "${_configuration_output_directory}/${vertex_filename}")
    set(_pixel_output "${_configuration_output_directory}/${pixel_filename}")

    add_custom_command(
        OUTPUT "${_vertex_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_configuration_output_directory}"
        COMMAND "${_dxc}"
            -nologo -WX -HV 2021 -T vs_6_0 -E VSMain
            -I "${_shader_include_directory}"
            "$<$<CONFIG:Debug>:-Zi>" "$<$<CONFIG:Debug>:-O3>" "$<$<CONFIG:Debug>:-Qembed_debug>"
            "$<$<NOT:$<CONFIG:Debug>>:-O3>"
            -Fo "${_vertex_output}" "${shader_source}"
        DEPENDS "${shader_source}" ${ARGN}
        COMMENT "Compiling vertex shader with DXC"
        COMMAND_EXPAND_LISTS
        VERBATIM)

    add_custom_command(
        OUTPUT "${_pixel_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_configuration_output_directory}"
        COMMAND "${_dxc}"
            -nologo -WX -HV 2021 -T ps_6_0 -E PSMain
            -I "${_shader_include_directory}"
            "$<$<CONFIG:Debug>:-Zi>" "$<$<CONFIG:Debug>:-O3>" "$<$<CONFIG:Debug>:-Qembed_debug>"
            "$<$<NOT:$<CONFIG:Debug>>:-O3>"
            -Fo "${_pixel_output}" "${shader_source}"
        DEPENDS "${shader_source}" ${ARGN}
        COMMENT "Compiling pixel shader with DXC"
        COMMAND_EXPAND_LISTS
        VERBATIM)

    add_custom_target(${target_name} DEPENDS "${_vertex_output}" "${_pixel_output}")
    set(${target_name}_VERTEX_SHADER "${_vertex_output}" PARENT_SCOPE)
    set(${target_name}_PIXEL_SHADER "${_pixel_output}" PARENT_SCOPE)
endfunction()

# Generic single-output DXC rule (Campaigns D-F). Shader sources include each other relative to the
# shaders/ root ("common/X.hlsli", "raytracing/Y.hlsli", "compute/Z.hlsli"), so INCLUDE_ROOT must be
# that directory. Library profiles (lib_6_x) take no ENTRY: every [shader("...")] export is kept.
#
#   daedalus_compile_shader(TARGET <name> SOURCE <file.hlsl> PROFILE <profile> [ENTRY <entry>]
#                           OUTPUT_DIRECTORY <dir> OUTPUT_NAME <file.dxil> INCLUDE_ROOT <dir>
#                           [DEPENDS <includes>...] [EXTRA_FLAGS <flags>...])
# Sets <name>_OUTPUT in the caller's scope to the per-configuration output path.
function(daedalus_compile_shader)
    cmake_parse_arguments(SHADER "" "TARGET;SOURCE;PROFILE;ENTRY;OUTPUT_DIRECTORY;OUTPUT_NAME;INCLUDE_ROOT" "DEPENDS;EXTRA_FLAGS" ${ARGN})
    foreach(_required TARGET SOURCE PROFILE OUTPUT_DIRECTORY OUTPUT_NAME INCLUDE_ROOT)
        if(NOT SHADER_${_required})
            message(FATAL_ERROR "daedalus_compile_shader: ${_required} is required")
        endif()
    endforeach()
    if(NOT SHADER_PROFILE MATCHES "^lib_" AND NOT SHADER_ENTRY)
        message(FATAL_ERROR "daedalus_compile_shader: ENTRY is required for non-library profile ${SHADER_PROFILE}")
    endif()

    daedalus_find_dxc(_dxc)
    set(_configuration_output_directory "${SHADER_OUTPUT_DIRECTORY}/$<CONFIG>")
    set(_output "${_configuration_output_directory}/${SHADER_OUTPUT_NAME}")
    set(_entry_arguments)
    if(SHADER_ENTRY)
        set(_entry_arguments -E "${SHADER_ENTRY}")
    endif()

    add_custom_command(
        OUTPUT "${_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_configuration_output_directory}"
        COMMAND "${_dxc}"
            -nologo -WX -HV 2021 -T "${SHADER_PROFILE}" ${_entry_arguments}
            -I "${SHADER_INCLUDE_ROOT}"
            ${SHADER_EXTRA_FLAGS}
            "$<$<CONFIG:Debug>:-Zi>" "$<$<CONFIG:Debug>:-O3>" "$<$<CONFIG:Debug>:-Qembed_debug>"
            "$<$<NOT:$<CONFIG:Debug>>:-O3>"
            -Fo "${_output}" "${SHADER_SOURCE}"
        DEPENDS "${SHADER_SOURCE}" ${SHADER_DEPENDS}
        COMMENT "Compiling ${SHADER_OUTPUT_NAME} (${SHADER_PROFILE}) with DXC"
        COMMAND_EXPAND_LISTS
        VERBATIM)

    add_custom_target(${SHADER_TARGET} DEPENDS "${_output}")
    set(${SHADER_TARGET}_OUTPUT "${_output}" PARENT_SCOPE)
endfunction()
