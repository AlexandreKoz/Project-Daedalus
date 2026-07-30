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
            list(APPEND _candidate_paths
                "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64/dxc.exe")
        endif()

        file(GLOB _sdk_dxc_candidates LIST_DIRECTORIES false
            "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxc.exe")
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

function(daedalus_compile_triangle_shaders target_name shader_source output_directory)
    daedalus_find_dxc(_dxc)
    message(STATUS "Using DirectX Shader Compiler: ${_dxc}")

    set(_configuration_output_directory "${output_directory}/$<CONFIG>")
    set(_vertex_output "${_configuration_output_directory}/TriangleVS.dxil")
    set(_pixel_output "${_configuration_output_directory}/TrianglePS.dxil")

    add_custom_command(
        OUTPUT "${_vertex_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_configuration_output_directory}"
        COMMAND "${_dxc}"
            -nologo -WX -HV 2021 -T vs_6_0 -E VSMain
            "$<$<CONFIG:Debug>:-Zi>" "$<$<CONFIG:Debug>:-Od>" "$<$<CONFIG:Debug>:-Qembed_debug>"
            "$<$<NOT:$<CONFIG:Debug>>:-O3>"
            -Fo "${_vertex_output}" "${shader_source}"
        DEPENDS "${shader_source}"
        COMMENT "Compiling Triangle vertex shader with DXC"
        COMMAND_EXPAND_LISTS
        VERBATIM)

    add_custom_command(
        OUTPUT "${_pixel_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_configuration_output_directory}"
        COMMAND "${_dxc}"
            -nologo -WX -HV 2021 -T ps_6_0 -E PSMain
            "$<$<CONFIG:Debug>:-Zi>" "$<$<CONFIG:Debug>:-Od>" "$<$<CONFIG:Debug>:-Qembed_debug>"
            "$<$<NOT:$<CONFIG:Debug>>:-O3>"
            -Fo "${_pixel_output}" "${shader_source}"
        DEPENDS "${shader_source}"
        COMMENT "Compiling Triangle pixel shader with DXC"
        COMMAND_EXPAND_LISTS
        VERBATIM)

    add_custom_target(${target_name} DEPENDS "${_vertex_output}" "${_pixel_output}")
    set(${target_name}_VERTEX_SHADER "${_vertex_output}" PARENT_SCOPE)
    set(${target_name}_PIXEL_SHADER "${_pixel_output}" PARENT_SCOPE)
endfunction()
