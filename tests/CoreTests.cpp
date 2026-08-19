#include "TestHarness.h"
#include "core/CommandLine.h"
#include "core/Error.h"
#include "core/Json.h"
#include "core/Sha256.h"
#include "graphics/AdapterPolicy.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace daedalus::tests;

void test_command_line_defaults()
{
    const daedalus::CommandLineOptions options = daedalus::parse_command_line({});
    require(!options.use_warp, "default command line must select hardware");
    require(!options.show_help, "default command line must not request help");
    require(!options.frame_limit.has_value(), "default command line must have no frame limit");
    require(!options.asset_path.has_value(), "default command line must have no asset");
}

void test_command_line_values()
{
    constexpr std::array arguments{
        std::wstring_view(L"--warp"), std::wstring_view(L"--frames"), std::wstring_view(L"120"),
        std::wstring_view(L"--asset"), std::wstring_view(L"scene.glb"), std::wstring_view(L"--scene"),
        std::wstring_view(L"Main"), std::wstring_view(L"--dump-scene"), std::wstring_view(L"--diagnostic"),
        std::wstring_view(L"normals"), std::wstring_view(L"--import-report"), std::wstring_view(L"report.json")};
    const daedalus::CommandLineOptions options = daedalus::parse_command_line(arguments);
    require(options.use_warp, "--warp must be recognized");
    require(options.frame_limit == 120, "--frames value must be parsed");
    require(options.asset_path == std::filesystem::path(L"scene.glb"), "--asset must be parsed");
    require(options.scene_selector == "Main", "--scene must be parsed");
    require(options.dump_scene, "--dump-scene must be parsed");
    require(options.diagnostic_mode == daedalus::DiagnosticMode::normals, "--diagnostic must be parsed");
    require(options.import_report_path == std::filesystem::path(L"report.json"), "--import-report must be parsed");
}

void test_command_line_rejections()
{
    constexpr std::array zero_frames{std::wstring_view(L"--frames"), std::wstring_view(L"0")};
    require_throws<daedalus::CommandLineError>([&] { static_cast<void>(daedalus::parse_command_line(zero_frames)); }, "zero frame count must be rejected");
    constexpr std::array duplicate{std::wstring_view(L"--asset"), std::wstring_view(L"a.glb"), std::wstring_view(L"--asset"), std::wstring_view(L"b.glb")};
    require_throws<daedalus::CommandLineError>([&] { static_cast<void>(daedalus::parse_command_line(duplicate)); }, "duplicate assets must be rejected");
    constexpr std::array bad_mode{std::wstring_view(L"--diagnostic"), std::wstring_view(L"pbr")};
    require_throws<daedalus::CommandLineError>([&] { static_cast<void>(daedalus::parse_command_line(bad_mode)); }, "unknown diagnostic mode must be rejected");
}


void test_tangent_diagnostic_mode()
{
    constexpr std::array arguments{std::wstring_view(L"--diagnostic"), std::wstring_view(L"tangents")};
    const daedalus::CommandLineOptions options = daedalus::parse_command_line(arguments);
    require(options.diagnostic_mode == daedalus::DiagnosticMode::tangents, "tangent diagnostic mode must parse");
    require(daedalus::to_string(options.diagnostic_mode) == "tangents", "tangent diagnostic mode must format deterministically");
    require(daedalus::usage_text().find("tangents") != std::string::npos, "help must document tangent diagnostics");
}

void test_stress_command_line()
{
    constexpr std::array arguments{
        std::wstring_view(L"--asset"), std::wstring_view(L"first.glb"),
        std::wstring_view(L"--stress-reloads"), std::wstring_view(L"12"),
        std::wstring_view(L"--stress-alternate-asset"), std::wstring_view(L"second.gltf"),
        std::wstring_view(L"--stress-resize"), std::wstring_view(L"--report-live-objects"),
        std::wstring_view(L"--no-error-dialog")};
    const daedalus::CommandLineOptions options = daedalus::parse_command_line(arguments);
    require(options.stress_reload_count == 12, "stress reload count must parse");
    require(options.stress_alternate_asset_path == std::filesystem::path(L"second.gltf"),
            "alternate stress asset must parse");
    require(options.stress_resize, "resize stress flag must parse");
    require(options.report_live_objects, "live-object flag must parse");
    require(options.suppress_error_dialog, "automation-safe error-dialog suppression must parse");

    constexpr std::array alternate_without_asset{
        std::wstring_view(L"--stress-reloads"), std::wstring_view(L"2"),
        std::wstring_view(L"--stress-alternate-asset"), std::wstring_view(L"second.gltf")};
    require_throws<daedalus::CommandLineError>(
        [&] { static_cast<void>(daedalus::parse_command_line(alternate_without_asset)); },
        "alternate stress asset without primary asset must be rejected");

    constexpr std::array alternate_without_reload{
        std::wstring_view(L"--asset"), std::wstring_view(L"first.glb"),
        std::wstring_view(L"--stress-alternate-asset"), std::wstring_view(L"second.gltf")};
    require_throws<daedalus::CommandLineError>(
        [&] { static_cast<void>(daedalus::parse_command_line(alternate_without_reload)); },
        "alternate stress asset without reload count must be rejected");

    const std::string help = daedalus::usage_text();
    require(help.find("--stress-reloads") != std::string::npos, "help must document reload stress");
    require(help.find("--stress-resize") != std::string::npos, "help must document resize stress");
    require(help.find("--report-live-objects") != std::string::npos, "help must document live-object reporting");
    require(help.find("--no-error-dialog") != std::string::npos, "help must document automation-safe error handling");
}

void test_result_formatting()
{
    require(daedalus::format_result_code(static_cast<daedalus::ResultCode>(0x80004005U)).starts_with("0x80004005"), "result code formatting must preserve hexadecimal digits");
    require(daedalus::format_failure_message(-1, "synthetic operation").find("0xFFFFFFFF") != std::string::npos, "failure message must include result code");
}

void test_adapter_policy()
{
    constexpr std::array candidates{
        daedalus::AdapterCandidate{true, false, 2ULL * 1024 * 1024 * 1024, 0},
        daedalus::AdapterCandidate{true, false, 8ULL * 1024 * 1024 * 1024, 1},
        daedalus::AdapterCandidate{true, true, 64ULL * 1024 * 1024 * 1024, 2}};
    require(daedalus::choose_adapter_index(candidates) == 1, "largest suitable hardware adapter must win");
}

void test_json_parser_and_stable_serialization()
{
    const daedalus::JsonValue value = daedalus::parse_json(R"({"z":1,"a":[true,null,"x"]})");
    const std::string serialized = daedalus::serialize_json(value, false);
    require(serialized == R"({"a":[true,null,"x"],"z":1})", "JSON object serialization must be key ordered");
    require_throws<daedalus::JsonError>([] { static_cast<void>(daedalus::parse_json(R"({"a":1,"a":2})")); }, "duplicate keys must be rejected");
}

void test_sha256()
{
    require(daedalus::sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 known vector must match");
}
}

int main()
{
    return daedalus::tests::run({
        {"command line defaults", test_command_line_defaults},
        {"command line values", test_command_line_values},
        {"command line rejections", test_command_line_rejections},
        {"tangent diagnostic mode", test_tangent_diagnostic_mode},
        {"stress command line", test_stress_command_line},
        {"result formatting", test_result_formatting},
        {"adapter policy", test_adapter_policy},
        {"JSON parser", test_json_parser_and_stable_serialization},
        {"SHA-256", test_sha256}});
}
