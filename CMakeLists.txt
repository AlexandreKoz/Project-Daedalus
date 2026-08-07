cmake_minimum_required(VERSION 3.25)

project(ProjectDaedalus VERSION 0.1.0 LANGUAGES CXX)

option(DAEDALUS_BUILD_APP "Build the Win32 Direct3D 12 application" ON)
option(DAEDALUS_BUILD_TESTS "Build platform-independent CPU tests" ON)
option(DAEDALUS_BUILD_TOOLS "Build portable asset validation tools" ON)
option(DAEDALUS_WARNINGS_AS_ERRORS "Treat project-owned C++ warnings as errors" OFF)

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
    src/rendering/DiagnosticPreparation.cpp
    src/rendering/DiagnosticPreparation.h
    src/rendering/OrbitCamera.cpp
    src/rendering/OrbitCamera.h)
target_link_libraries(daedalus_rendering PUBLIC daedalus_scene)
target_include_directories(daedalus_rendering PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(daedalus_rendering PUBLIC cxx_std_20)
daedalus_apply_project_warnings(daedalus_rendering)

add_library(daedalus_assets STATIC
    src/assets/Diagnostics.cpp
    src/assets/Diagnostics.h
    src/assets/GltfImporter.cpp
    src/assets/GltfImporter.h
    src/assets/ImageDecoder.cpp
    src/assets/ImageDecoder.h
    src/assets/ImportReport.cpp
    src/assets/ImportReport.h)
target_link_libraries(daedalus_assets PUBLIC daedalus_core daedalus_scene)
if(WIN32)
    target_link_libraries(daedalus_assets PRIVATE windowscodecs ole32)
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
    daedalus_compile_shader_pair(
        DaedalusDiagnosticShaders
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Diagnostic.hlsl"
        "${DAEDALUS_SHADER_OUTPUT_DIRECTORY}"
        "DiagnosticVS.dxil"
        "DiagnosticPS.dxil")

    add_executable(Daedalus
        src/Application.cpp
        src/Application.h
        src/Main.cpp
        src/core/Log.cpp
        src/core/Log.h
        src/graphics/D3D12Context.cpp
        src/graphics/D3D12Context.h
        src/graphics/DiagnosticSceneRenderer.cpp
        src/graphics/DiagnosticSceneRenderer.h
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
    add_dependencies(Daedalus DaedalusDiagnosticShaders)

    add_custom_command(TARGET Daedalus POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:Daedalus>/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${DaedalusDiagnosticShaders_VERTEX_SHADER}"
            "$<TARGET_FILE_DIR:Daedalus>/shaders/DiagnosticVS.dxil"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${DaedalusDiagnosticShaders_PIXEL_SHADER}"
            "$<TARGET_FILE_DIR:Daedalus>/shaders/DiagnosticPS.dxil"
        COMMENT "Copying compiled diagnostic shaders beside Daedalus")
endif()

include(CTest)
if(BUILD_TESTING AND DAEDALUS_BUILD_TESTS)
    add_subdirectory(tests)
endif()
