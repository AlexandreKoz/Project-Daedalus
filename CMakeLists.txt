cmake_minimum_required(VERSION 3.25)

project(ProjectDaedalus VERSION 0.1.0 LANGUAGES CXX)

option(DAEDALUS_BUILD_APP "Build the Win32 Direct3D 12 application" ON)
option(DAEDALUS_BUILD_TESTS "Build platform-independent CPU tests" ON)
option(DAEDALUS_BUILD_TOOLS "Build portable asset validation tools" ON)
option(DAEDALUS_WARNINGS_AS_ERRORS "Treat project-owned C++ warnings as errors" OFF)
# Campaign G: authentic NVIDIA DLSS is strictly opt-in. Daedalus ships NO vendor SDK code; enabling
# this requires a locally installed, licensed SDK plus a user-supplied glue translation unit that
# implements daedalus::create_dlss_vendor_runtime (see src/graphics/dlss/DlssVendorRuntime.h).
option(DAEDALUS_ENABLE_DLSS "Build the DLSS upscaler backend (requires a local vendor SDK and glue source)" OFF)
set(DAEDALUS_DLSS_VENDOR_GLUE_SOURCE "" CACHE FILEPATH "User-supplied .cpp implementing create_dlss_vendor_runtime")
set(DAEDALUS_DLSS_INCLUDE_DIRECTORY "" CACHE PATH "Vendor SDK include directory used by the glue source")
set(DAEDALUS_DLSS_LIBRARIES "" CACHE STRING "Vendor SDK import libraries for the glue source (semicolon list)")
set(DAEDALUS_DLSS_RUNTIME_FILES "" CACHE STRING "Vendor runtime DLLs copied beside Daedalus (semicolon list)")
# Campaign H: OBJ/FBX/3DS compatibility import through an installed Assimp package (never fetched).
option(DAEDALUS_ENABLE_ASSIMP "Build the Assimp-based OBJ/FBX/3DS compatibility importer" OFF)
set(DAEDALUS_ASSIMP_MINIMUM_VERSION "5.3" CACHE STRING "Minimum accepted Assimp package version")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(daedalus_apply_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
        if(DAEDALUS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(DAEDALUS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_library(daedalus_core STATIC
    src/core/CommandLine.cpp
    src/core/CommandLine.h
    src/core/Error.cpp
    src/core/Error.h
    src/core/Json.cpp
    src/core/Json.h
    src/core/RenderSettings.cpp
    src/core/RenderSettings.h
    src/core/Sha256.cpp
    src/core/Sha256.h
    src/core/Version.h
    src/graphics/AdapterPolicy.cpp
    src/graphics/AdapterPolicy.h)
target_include_directories(daedalus_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(daedalus_core PUBLIC cxx_std_20)
daedalus_apply_project_warnings(daedalus_core)

add_library(daedalus_scene STATIC
    src/scene/Math.cpp
    src/scene/Math.h
    src/scene/Scene.cpp
    src/scene/Scene.h)
target_include_directories(daedalus_scene PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(daedalus_scene PUBLIC cxx_std_20)
daedalus_apply_project_warnings(daedalus_scene)

add_library(daedalus_rendering STATIC
    src/rendering/CampaignC2Math.cpp
    src/rendering/CampaignC2Math.h
    src/rendering/ConvergenceAnalysis.cpp
    src/rendering/ConvergenceAnalysis.h
    src/rendering/FrameRevision.cpp
    src/rendering/FrameRevision.h
    src/rendering/GpuSceneLayout.h
    src/rendering/OrbitCamera.cpp
    src/rendering/OrbitCamera.h
    src/rendering/PathTracingMath.cpp
    src/rendering/PathTracingMath.h
    src/rendering/PbrMath.cpp
    src/rendering/PbrMath.h
    src/rendering/RasterPreparation.cpp
    src/rendering/RasterPreparation.h
    src/rendering/RasterShaderContract.h
    src/rendering/RayTracingContract.h
    src/rendering/RayTracingPreparation.cpp
    src/rendering/RayTracingPreparation.h
    src/rendering/SceneAnimation.cpp
    src/rendering/SceneAnimation.h
    src/rendering/TemporalContract.h
    src/rendering/TemporalMath.cpp
    src/rendering/TemporalMath.h)
target_link_libraries(daedalus_rendering PUBLIC daedalus_core daedalus_scene)
target_include_directories(daedalus_rendering PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(daedalus_rendering PUBLIC cxx_std_20)
daedalus_apply_project_warnings(daedalus_rendering)

add_library(daedalus_assets STATIC
    src/assets/AssetCache.cpp
    src/assets/AssetCache.h
    src/assets/AssetLoader.cpp
    src/assets/AssetLoader.h
    src/assets/BlenderWorker.cpp
    src/assets/BlenderWorker.h
    src/assets/CompatibilityImporter.cpp
    src/assets/CompatibilityImporter.h
    src/assets/Diagnostics.cpp
    src/assets/Diagnostics.h
    src/assets/FloatImage.cpp
    src/assets/FloatImage.h
    src/assets/GltfImporter.cpp
    src/assets/GltfImporter.h
    src/assets/ImageDecoder.cpp
    src/assets/ImageDecoder.h
    src/assets/ImageEncoder.cpp
    src/assets/ImageEncoder.h
    src/assets/ImportReport.cpp
    src/assets/ImportReport.h
    src/assets/SceneSerialization.cpp
    src/assets/SceneSerialization.h
    src/assets/TextureResolver.cpp
    src/assets/TextureResolver.h)
target_link_libraries(daedalus_assets PUBLIC daedalus_core daedalus_scene)
if(DAEDALUS_ENABLE_ASSIMP)
    # Installed package only (vcpkg/system/CMAKE_PREFIX_PATH); no FetchContent, no network.
    find_package(assimp ${DAEDALUS_ASSIMP_MINIMUM_VERSION} CONFIG)
    if(NOT assimp_FOUND)
        message(FATAL_ERROR
            "DAEDALUS_ENABLE_ASSIMP=ON but no Assimp >= ${DAEDALUS_ASSIMP_MINIMUM_VERSION} CMake package was found. "
            "Install Assimp locally and point CMAKE_PREFIX_PATH (or assimp_DIR) at it, or configure with "
            "-DDAEDALUS_ENABLE_ASSIMP=OFF (OBJ/FBX/3DS imports then report compatibility_importer_unavailable).")
    endif()
    target_link_libraries(daedalus_assets PRIVATE assimp::assimp)
    target_compile_definitions(daedalus_assets PRIVATE DAEDALUS_HAS_ASSIMP)
    message(STATUS "Daedalus compatibility importer: Assimp ${assimp_VERSION}")
else()
    message(STATUS "Daedalus compatibility importer: disabled (DAEDALUS_ENABLE_ASSIMP=OFF)")
endif()
if(WIN32)
    target_link_libraries(daedalus_assets PRIVATE windowscodecs ole32)
    target_compile_definitions(daedalus_assets PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
else()
    find_package(PNG REQUIRED)
    find_package(JPEG REQUIRED)
    target_link_libraries(daedalus_assets PRIVATE PNG::PNG JPEG::JPEG)
endif()
target_include_directories(daedalus_assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(daedalus_assets PUBLIC cxx_std_20)
daedalus_apply_project_warnings(daedalus_assets)

if(DAEDALUS_BUILD_TOOLS)
    add_executable(DaedalusAssetValidator src/tools/AssetValidator.cpp)
    target_link_libraries(DaedalusAssetValidator PRIVATE daedalus_assets)
    target_include_directories(DaedalusAssetValidator PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    daedalus_apply_project_warnings(DaedalusAssetValidator)

    add_executable(DaedalusImageCompare src/tools/ImageCompare.cpp)
    target_link_libraries(DaedalusImageCompare PRIVATE daedalus_assets daedalus_rendering)
    if(WIN32)
        target_link_libraries(DaedalusImageCompare PRIVATE ole32)
    endif()
    target_include_directories(DaedalusImageCompare PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    daedalus_apply_project_warnings(DaedalusImageCompare)

    add_executable(DaedalusAssetBatch src/tools/AssetBatch.cpp)
    target_link_libraries(DaedalusAssetBatch PRIVATE daedalus_assets)
    target_include_directories(DaedalusAssetBatch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    daedalus_apply_project_warnings(DaedalusAssetBatch)
    add_custom_command(TARGET DaedalusAssetBatch POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:DaedalusAssetBatch>/scripts/blender"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/blender/daedalus_blend_export.py"
            "$<TARGET_FILE_DIR:DaedalusAssetBatch>/scripts/blender/daedalus_blend_export.py"
        COMMENT "Copying the versioned Blender export script beside DaedalusAssetBatch")

    add_executable(DaedalusConvergence src/tools/ConvergenceTool.cpp)
    target_link_libraries(DaedalusConvergence PRIVATE daedalus_assets daedalus_rendering)
    target_include_directories(DaedalusConvergence PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    daedalus_apply_project_warnings(DaedalusConvergence)
endif()

if(DAEDALUS_BUILD_APP)
    if(NOT WIN32)
        message(FATAL_ERROR
            "The Daedalus application requires Windows. For portable CPU validation only, configure with "
            "-DDAEDALUS_BUILD_APP=OFF -DDAEDALUS_BUILD_TESTS=ON -DDAEDALUS_BUILD_TOOLS=ON.")
    endif()

    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Project Daedalus supports Windows x64 only.")
    endif()

    include(cmake/CompileShaders.cmake)
    set(DAEDALUS_SHADER_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
    set(DAEDALUS_SHADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    set(DAEDALUS_SHADER_COMMON_INCLUDES
        "${DAEDALUS_SHADER_ROOT}/common/Color.hlsli"
        "${DAEDALUS_SHADER_ROOT}/common/Environment.hlsli"
        "${DAEDALUS_SHADER_ROOT}/common/FrameContract.hlsli"
        "${DAEDALUS_SHADER_ROOT}/common/PbrLighting.hlsli"
        "${DAEDALUS_SHADER_ROOT}/common/Sampling.hlsli")
    daedalus_compile_shader_pair(
        DaedalusRasterPbrShaders
        "${DAEDALUS_SHADER_ROOT}/RasterPbr.hlsl"
        "${DAEDALUS_SHADER_OUTPUT_DIRECTORY}"
        "RasterPbrVS.dxil"
        "RasterPbrPS.dxil"
        ${DAEDALUS_SHADER_COMMON_INCLUDES})
    daedalus_compile_shader_pair(
        DaedalusToneMapShaders
        "${DAEDALUS_SHADER_ROOT}/ToneMap.hlsl"
        "${DAEDALUS_SHADER_OUTPUT_DIRECTORY}"
        "ToneMapVS.dxil"
        "ToneMapPS.dxil"
        "${DAEDALUS_SHADER_ROOT}/common/Color.hlsli")
    daedalus_compile_shader_pair(
        DaedalusShadowShaders
        "${DAEDALUS_SHADER_ROOT}/Shadow.hlsl"
        "${DAEDALUS_SHADER_OUTPUT_DIRECTORY}"
        "ShadowVS.dxil"
        "ShadowPS.dxil")

    # Campaigns D-F shaders. DXR libraries use lib_6_3 (DXR 1.0, no inline ray queries); compute and
    # the G-buffer pixel shader use the same 6.0 baseline as the existing raster shaders.
    set(DAEDALUS_RT_INCLUDES
        ${DAEDALUS_SHADER_COMMON_INCLUDES}
        "${DAEDALUS_SHADER_ROOT}/raytracing/RayTracingCommon.hlsli"
        "${DAEDALUS_SHADER_ROOT}/raytracing/PathTracingBsdf.hlsli")
    set(DAEDALUS_COMPUTE_INCLUDES
        ${DAEDALUS_SHADER_COMMON_INCLUDES}
        "${DAEDALUS_SHADER_ROOT}/compute/ComputeCommon.hlsli")
    set(DAEDALUS_EXTRA_SHADER_TARGETS)
    macro(daedalus_add_shader target source profile entry output includes)
        daedalus_compile_shader(
            TARGET ${target}
            SOURCE "${DAEDALUS_SHADER_ROOT}/${source}"
            PROFILE ${profile}
            ENTRY "${entry}"
            OUTPUT_DIRECTORY "${DAEDALUS_SHADER_OUTPUT_DIRECTORY}"
            OUTPUT_NAME ${output}
            INCLUDE_ROOT "${DAEDALUS_SHADER_ROOT}"
            DEPENDS ${${includes}})
        list(APPEND DAEDALUS_EXTRA_SHADER_TARGETS ${target})
    endmacro()
    daedalus_add_shader(DaedalusRasterGBufferShader RasterPbr.hlsl ps_6_0 PSMainGBuffer RasterPbrGBufferPS.dxil DAEDALUS_SHADER_COMMON_INCLUDES)
    daedalus_add_shader(DaedalusHybridRayTracingLibrary raytracing/HybridRayTracing.hlsl lib_6_3 "" HybridRayTracing.dxil DAEDALUS_RT_INCLUDES)
    daedalus_add_shader(DaedalusPathTracingLibrary raytracing/PathTracing.hlsl lib_6_3 "" PathTracing.dxil DAEDALUS_RT_INCLUDES)
    daedalus_add_shader(DaedalusHybridCompositeShader compute/HybridComposite.hlsl cs_6_0 CSMain HybridCompositeCS.dxil DAEDALUS_COMPUTE_INCLUDES)
    daedalus_add_shader(DaedalusPathTraceCompositeShader compute/PathTraceComposite.hlsl cs_6_0 CSMain PathTraceCompositeCS.dxil DAEDALUS_COMPUTE_INCLUDES)
    daedalus_add_shader(DaedalusTemporalReprojectShader compute/TemporalReproject.hlsl cs_6_0 CSMain TemporalReprojectCS.dxil DAEDALUS_COMPUTE_INCLUDES)
    daedalus_add_shader(DaedalusAtrousFilterShader compute/AtrousFilter.hlsl cs_6_0 CSMain AtrousFilterCS.dxil DAEDALUS_COMPUTE_INCLUDES)
    daedalus_add_shader(DaedalusTemporalDebugShader compute/TemporalDebug.hlsl cs_6_0 CSMain TemporalDebugCS.dxil DAEDALUS_COMPUTE_INCLUDES)

    add_executable(Daedalus
        src/Application.cpp
        src/Application.h
        src/Main.cpp
        src/core/Log.cpp
        src/core/Log.h
        src/graphics/ComputePass.cpp
        src/graphics/ComputePass.h
        src/graphics/D3D12Context.cpp
        src/graphics/D3D12Context.h
        src/graphics/D3D12Helpers.cpp
        src/graphics/D3D12Helpers.h
        src/graphics/DisplayOutput.cpp
        src/graphics/DisplayOutput.h
        src/graphics/GpuSceneResources.cpp
        src/graphics/GpuSceneResources.h
        src/graphics/GpuTimer.cpp
        src/graphics/GpuTimer.h
        src/graphics/PbrSceneRenderer.cpp
        src/graphics/PbrSceneRenderer.h
        src/graphics/SceneRenderer.cpp
        src/graphics/SceneRenderer.h
        src/graphics/dlss/DlssBackend.cpp
        src/graphics/dlss/DlssBackend.h
        src/graphics/dlss/DlssVendorRuntime.h
        src/graphics/raytracing/AccelerationStructureManager.cpp
        src/graphics/raytracing/AccelerationStructureManager.h
        src/graphics/raytracing/DxrDeviceSupport.cpp
        src/graphics/raytracing/DxrDeviceSupport.h
        src/graphics/raytracing/HybridRayTracer.cpp
        src/graphics/raytracing/HybridRayTracer.h
        src/graphics/raytracing/RayTracingCounters.cpp
        src/graphics/raytracing/RayTracingCounters.h
        src/graphics/raytracing/RaytracingPipeline.cpp
        src/graphics/raytracing/RaytracingPipeline.h
        src/graphics/raytracing/RaytracingScene.cpp
        src/graphics/raytracing/RaytracingScene.h
        src/graphics/raytracing/ReferencePathTracer.cpp
        src/graphics/raytracing/ReferencePathTracer.h
        src/graphics/temporal/TemporalDenoiser.cpp
        src/graphics/temporal/TemporalDenoiser.h
        src/platform/UniqueWin32Handle.h
        src/platform/Win32Window.cpp
        src/platform/Win32Window.h)

    target_link_libraries(Daedalus PRIVATE daedalus_core daedalus_scene daedalus_assets daedalus_rendering d3d12 dxgi dxguid user32 ole32)
    target_include_directories(Daedalus PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(Daedalus PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
        "DAEDALUS_BUILD_TYPE=\"$<CONFIG>\""
        "$<$<CONFIG:Debug>:DAEDALUS_DEBUG_BUILD>")
    daedalus_apply_project_warnings(Daedalus)
    add_dependencies(Daedalus DaedalusRasterPbrShaders DaedalusToneMapShaders DaedalusShadowShaders ${DAEDALUS_EXTRA_SHADER_TARGETS})

    if(DAEDALUS_ENABLE_DLSS)
        # BLOCKED unless the user supplies everything locally; Daedalus never downloads or vendors the SDK.
        if(NOT DAEDALUS_DLSS_VENDOR_GLUE_SOURCE OR NOT EXISTS "${DAEDALUS_DLSS_VENDOR_GLUE_SOURCE}")
            message(FATAL_ERROR
                "DLSS BLOCKED: DAEDALUS_ENABLE_DLSS=ON requires DAEDALUS_DLSS_VENDOR_GLUE_SOURCE=<your glue .cpp> implementing "
                "daedalus::create_dlss_vendor_runtime against a locally installed, licensed NVIDIA SDK. No such source is part "
                "of Project Daedalus. Configure with -DDAEDALUS_ENABLE_DLSS=OFF to use the native/bilinear fallback.")
        endif()
        if(NOT DAEDALUS_DLSS_INCLUDE_DIRECTORY OR NOT IS_DIRECTORY "${DAEDALUS_DLSS_INCLUDE_DIRECTORY}")
            message(FATAL_ERROR "DLSS BLOCKED: DAEDALUS_DLSS_INCLUDE_DIRECTORY must name the local vendor SDK include directory.")
        endif()
        if(NOT DAEDALUS_DLSS_LIBRARIES)
            message(FATAL_ERROR "DLSS BLOCKED: DAEDALUS_DLSS_LIBRARIES must list the local vendor SDK import libraries.")
        endif()
        target_sources(Daedalus PRIVATE "${DAEDALUS_DLSS_VENDOR_GLUE_SOURCE}")
        target_include_directories(Daedalus PRIVATE "${DAEDALUS_DLSS_INCLUDE_DIRECTORY}")
        target_link_libraries(Daedalus PRIVATE ${DAEDALUS_DLSS_LIBRARIES})
        target_compile_definitions(Daedalus PRIVATE DAEDALUS_DLSS_VENDOR_GLUE)
        foreach(_runtime_file IN LISTS DAEDALUS_DLSS_RUNTIME_FILES)
            add_custom_command(TARGET Daedalus POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_runtime_file}" "$<TARGET_FILE_DIR:Daedalus>"
                COMMENT "Copying user-supplied DLSS runtime ${_runtime_file}")
        endforeach()
        message(STATUS "Daedalus DLSS backend: user-supplied vendor glue ${DAEDALUS_DLSS_VENDOR_GLUE_SOURCE}")
    else()
        message(STATUS "Daedalus DLSS backend: disabled (native/bilinear fallback only)")
    endif()

    set(DAEDALUS_SHADER_COPY_COMMANDS)
    foreach(_shader_target IN LISTS DAEDALUS_EXTRA_SHADER_TARGETS)
        get_filename_component(_shader_name "${${_shader_target}_OUTPUT}" NAME)
        list(APPEND DAEDALUS_SHADER_COPY_COMMANDS
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${${_shader_target}_OUTPUT}"
                "$<TARGET_FILE_DIR:Daedalus>/shaders/${_shader_name}")
    endforeach()

    add_custom_command(TARGET Daedalus POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:Daedalus>/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${DaedalusRasterPbrShaders_VERTEX_SHADER}"
            "$<TARGET_FILE_DIR:Daedalus>/shaders/RasterPbrVS.dxil"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${DaedalusRasterPbrShaders_PIXEL_SHADER}"
            "$<TARGET_FILE_DIR:Daedalus>/shaders/RasterPbrPS.dxil"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${DaedalusToneMapShaders_VERTEX_SHADER}"
            "$<TARGET_FILE_DIR:Daedalus>/shaders/ToneMapVS.dxil"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${DaedalusToneMapShaders_PIXEL_SHADER}"
            "$<TARGET_FILE_DIR:Daedalus>/shaders/ToneMapPS.dxil"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${DaedalusShadowShaders_VERTEX_SHADER}"
            "$<TARGET_FILE_DIR:Daedalus>/shaders/ShadowVS.dxil"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${DaedalusShadowShaders_PIXEL_SHADER}"
            "$<TARGET_FILE_DIR:Daedalus>/shaders/ShadowPS.dxil"
        ${DAEDALUS_SHADER_COPY_COMMANDS}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:Daedalus>/scripts/blender"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/blender/daedalus_blend_export.py"
            "$<TARGET_FILE_DIR:Daedalus>/scripts/blender/daedalus_blend_export.py"
        COMMENT "Copying compiled raster, shadow, tone-map, ray-tracing and compute shaders plus the Blender export script beside Daedalus")
endif()

include(CTest)
if(BUILD_TESTING AND DAEDALUS_BUILD_TESTS)
    add_subdirectory(tests)
endif()
