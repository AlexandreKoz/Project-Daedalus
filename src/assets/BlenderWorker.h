#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daedalus
{
// Controlled external Blender conversion (Campaign H). Daedalus NEVER parses .blend files: it runs
// a separately installed Blender in background mode with the versioned, repository-owned script
// scripts/blender/daedalus_blend_export.py, which exports a neutral GLB plus a JSON manifest. The
// GLB then goes through the canonical glTF importer.
//
// Process policy: argument vector only (no shell); Windows command line built with the documented
// CommandLineToArgvW quoting rules; stdout/stderr redirected to a log file inside the conversion
// directory (no pipe deadlocks); wall-clock timeout and optional cooperative cancellation kill the
// whole process tree (Job object on Windows, process group on POSIX); exit code, timeout and output
// size limits are all reported.
inline constexpr std::string_view kBlenderExportScriptVersion = "daedalus-blend-export/1";

struct BlenderConversionRequest
{
    std::filesystem::path blender_executable;   // empty = search PATH for blender / blender.exe
    std::filesystem::path script;               // scripts/blender/daedalus_blend_export.py
    std::filesystem::path source;               // .blend input
    std::filesystem::path output_directory;     // receives converted.glb, manifest.json, blender.log
    std::chrono::seconds timeout{300};
    std::uint64_t maximum_output_bytes = 1024ULL * 1024ULL * 1024ULL;
    const std::atomic<bool>* cancel = nullptr;
};

struct BlenderConversionResult
{
    bool success = false;
    bool timed_out = false;
    bool cancelled = false;
    int exit_code = -1;
    std::filesystem::path executable;
    std::filesystem::path glb_path;
    std::filesystem::path manifest_path;
    std::filesystem::path log_path;
    std::string blender_version;     // from the manifest written by Blender at run time
    std::string script_version;
    std::vector<std::string> warnings;
    std::vector<std::string> unsupported;
    std::vector<std::string> approximations;
    std::string failure;
};

// Windows argument quoting (inverse of CommandLineToArgvW); exposed for portable tests.
[[nodiscard]] std::string quote_windows_argument(std::string_view argument);
[[nodiscard]] std::vector<std::string> blender_command_arguments(const BlenderConversionRequest& request,
                                                                 const std::filesystem::path& executable);
[[nodiscard]] std::optional<std::filesystem::path> locate_blender_executable(const std::filesystem::path& configured);
[[nodiscard]] BlenderConversionResult run_blender_conversion(const BlenderConversionRequest& request);
}
