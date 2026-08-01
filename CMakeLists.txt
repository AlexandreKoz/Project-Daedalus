cmake_minimum_required(VERSION 3.25)

project(ProjectDaedalus VERSION 0.0.1 LANGUAGES CXX)

option(DAEDALUS_BUILD_APP "Build the Win32 Direct3D 12 application" ON)
option(DAEDALUS_BUILD_TESTS "Build platform-independent CPU tests" ON)
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
    src/core/Version.h
    src/graphics/AdapterPolicy.cpp
    src/graphics/AdapterPolicy.h)

target_include_directories(daedalus_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(daedalus_core PUBLIC cxx_std_20)
daedalus_apply_project_warnings(daedalus_core)

if(DAEDALUS_BUILD_APP)
    if(NOT WIN32)
        message(FATAL_ERROR
            "The Daedalus application requires Windows. For portable CPU validation only, configure with "
            "-DDAEDALUS_BUILD_APP=OFF -DDAEDALUS_BUILD_TESTS=ON.")
    endif()

    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Project Daedalus Campaign A supports Windows x64 only.")
    endif()

    include(cmake/CompileShaders.cmake)
    set(DAEDALUS_SHADER_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
    daedalus_compile_triangle_shaders(
        DaedalusShaders
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Triangle.hlsl"
        "${DAEDALUS_SHADER_OUTPUT_DIRECTORY}")

    add_executable(Daedalus
        src/Application.cpp
        src/Application.h
        src/Main.cpp
        src/core/Log.cpp
        src/core/Log.h
        src/graphics/D3D12Context.cpp
        src/graphics/D3D12Context.h
        src/graphics/TriangleRenderer.cpp
        src/graphics/TriangleRenderer.h
        src/platform/UniqueWin32Handle.h
        src/platform/Win32Window.cpp
        src/platform/Win32Window.h)

    target_link_libraries(Daedalus PRIVATE daedalus_core d3d12 dxgi user32)
    target_include_directories(Daedalus PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(Daedalus PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
        "DAEDALUS_BUILD_TYPE=\"$<CONFIG>\""
        "$<$<CONFIG:Debug>:DAEDALUS_DEBUG_BUILD>")
    daedalus_apply_project_warnings(Daedalus)
    add_dependencies(Daedalus DaedalusShaders)

    add_custom_command(TARGET Daedalus POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:Daedalus>/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${DaedalusShaders_VERTEX_SHADER}"
            "$<TARGET_FILE_DIR:Daedalus>/shaders/TriangleVS.dxil"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${DaedalusShaders_PIXEL_SHADER}"
            "$<TARGET_FILE_DIR:Daedalus>/shaders/TrianglePS.dxil"
        COMMENT "Copying compiled shaders beside Daedalus")
endif()

include(CTest)
if(BUILD_TESTING AND DAEDALUS_BUILD_TESTS)
    add_subdirectory(tests)
endif()
