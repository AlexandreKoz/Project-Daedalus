// DaedalusAssetBatch: batch conversion/validation of glTF, OBJ/FBX/3DS and .blend sources through
// the same AssetLoader the application uses (Campaign H). For every input it writes the import
// report (with provenance) and, in "convert" mode, the serialized canonical scene; a batch manifest
// summarises all inputs. Exit codes: 0 all succeeded, 1 at least one import failed, 2 usage error.

#include "assets/AssetLoader.h"
#include "assets/SceneSerialization.h"
#include "core/Json.h"
#include "core/Version.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr std::string_view kManifestSchema = "daedalus.asset-batch/1";

struct BatchOptions
{
    std::string mode;
    std::vector<std::filesystem::path> inputs;
    std::filesystem::path output_directory;
    bool recursive = false;
    bool fail_fast = false;
    daedalus::AssetLoadOptions load;
};

struct BatchInput
{
    std::filesystem::path path;
    std::filesystem::path relative;
};

void print_usage()
{
    std::cerr <<
        "Usage: DaedalusAssetBatch <convert|validate> <input>... --output-dir <dir>\n"
        "         [--recursive] [--fail-fast] [--cache <dir>] [--texture-search-root <dir>]...\n"
        "         [--up-axis auto|y|z] [--metres-per-unit <value>] [--scene <index-or-name>]\n"
        "         [--blender <path>] [--blender-script <path>] [--blender-timeout <seconds>]\n"
        "Inputs may be files or directories (.gltf .glb .obj .fbx .3ds .blend).\n";
}

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string value = path.u8string();
    return std::string(value.begin(), value.end());
}

void write_text(const std::filesystem::path& path, std::string_view text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("unable to open output: " + utf8(path));
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("unable to write output: " + utf8(path));
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("unable to open output: " + utf8(path));
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("unable to write output: " + utf8(path));
}

BatchOptions parse(int argc, char** argv)
{
    if (argc < 2) throw std::invalid_argument("missing mode");
    BatchOptions options;
    options.mode = argv[1];
    if (options.mode != "convert" && options.mode != "validate") throw std::invalid_argument("mode must be convert or validate");
    const std::filesystem::path tool_directory = std::filesystem::absolute(std::filesystem::path(argv[0])).parent_path();
    options.load.blender_script = tool_directory / "scripts" / "blender" / "daedalus_blend_export.py";
    for (int index = 2; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        const auto value = [&]() -> std::string
        {
            if (index + 1 >= argc) throw std::invalid_argument(std::string(argument) + " requires a value");
            return argv[++index];
        };
        if (argument == "--output-dir") options.output_directory = value();
        else if (argument == "--recursive") options.recursive = true;
        else if (argument == "--fail-fast") options.fail_fast = true;
        else if (argument == "--cache") options.load.cache_directory = std::filesystem::path(value());
        else if (argument == "--texture-search-root") options.load.texture_search_roots.emplace_back(value());
        else if (argument == "--scene") options.load.import_settings.scene_selector = value();
        else if (argument == "--blender") options.load.blender_executable = std::filesystem::path(value());
        else if (argument == "--blender-script") options.load.blender_script = value();
        else if (argument == "--up-axis")
        {
            const std::string text = value();
            const auto axis = daedalus::parse_source_up_axis(text);
            if (!axis.has_value()) throw std::invalid_argument("--up-axis must be auto, y or z");
            options.load.up_axis = *axis;
        }
        else if (argument == "--metres-per-unit")
        {
            const std::string text = value();
            std::size_t consumed = 0;
            const float metres = std::stof(text, &consumed);
            if (consumed != text.size() || !(metres > 0.0F)) throw std::invalid_argument("--metres-per-unit must be a positive number");
            options.load.metres_per_unit = metres;
        }
        else if (argument == "--blender-timeout")
        {
            const std::string text = value();
            std::size_t consumed = 0;
            const unsigned long seconds = std::stoul(text, &consumed);
            if (consumed != text.size() || seconds == 0UL || seconds > 86400UL)
                throw std::invalid_argument("--blender-timeout must be 1..86400 seconds");
            options.load.blender_timeout_seconds = static_cast<std::uint32_t>(seconds);
        }
        else if (!argument.empty() && argument.front() == '-') throw std::invalid_argument("unknown option " + std::string(argument));
        else options.inputs.emplace_back(std::string(argument));
    }
    if (options.inputs.empty()) throw std::invalid_argument("no inputs");
    if (options.output_directory.empty()) throw std::invalid_argument("--output-dir is required");
    options.load.temporary_directory = options.output_directory / "conversion-work";
    return options;
}

std::vector<BatchInput> expand_inputs(const BatchOptions& options)
{
    std::vector<BatchInput> inputs;
    for (const std::filesystem::path& input : options.inputs)
    {
        std::error_code error;
        if (std::filesystem::is_directory(input, error))
        {
            std::vector<BatchInput> found;
            const auto consider = [&](const std::filesystem::directory_entry& entry)
            {
                if (entry.is_regular_file() && daedalus::classify_asset_source(entry.path()) != daedalus::AssetSourceKind::unsupported)
                    found.push_back({entry.path(), entry.path().lexically_relative(input)});
            };
            if (options.recursive)
                for (const auto& entry : std::filesystem::recursive_directory_iterator(input)) consider(entry);
            else
                for (const auto& entry : std::filesystem::directory_iterator(input)) consider(entry);
            // Deterministic order independent of the filesystem's enumeration order.
            std::sort(found.begin(), found.end(), [](const BatchInput& left, const BatchInput& right)
                      { return left.relative.generic_string() < right.relative.generic_string(); });
            inputs.insert(inputs.end(), found.begin(), found.end());
        }
        else
        {
            inputs.push_back({input, input.filename()});
        }
    }
    return inputs;
}
}

int main(int argc, char** argv)
{
    BatchOptions options;
    try
    {
        options = parse(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::cerr << "asset batch usage error: " << error.what() << '\n';
        print_usage();
        return 2;
    }

    try
    {
        const std::vector<BatchInput> inputs = expand_inputs(options);
        daedalus::JsonValue::Array entries;
        std::uint64_t failures = 0;
        for (const BatchInput& input : inputs)
        {
            daedalus::AssetLoadOptions load = options.load;
            load.source = input.path;
            const daedalus::AssetLoadResult result = daedalus::load_asset(load);
            const std::filesystem::path base = options.output_directory / input.relative;
            const std::filesystem::path report_path = base.parent_path() / (utf8(base.filename()) + ".report.json");
            write_text(report_path, result.import.report.to_json());

            daedalus::JsonValue::Object entry;
            entry.emplace("source", input.relative.generic_string());
            entry.emplace("kind", std::string(daedalus::to_string(result.kind)));
            entry.emplace("status", std::string(daedalus::to_string(result.import.report.status)));
            entry.emplace("asset_key", result.import.report.deterministic_asset_key);
            entry.emplace("source_sha256", result.import.report.source_sha256);
            entry.emplace("cache_hit", result.cache_hit);
            entry.emplace("cache_key", result.cache_key);
            entry.emplace("report", report_path.lexically_relative(options.output_directory).generic_string());
            if (options.mode == "convert" && result.import.succeeded())
            {
                const std::filesystem::path scene_path = base.parent_path() / (utf8(base.filename()) + ".dscene");
                write_bytes(scene_path, daedalus::serialize_scene(result.import.scene));
                entry.emplace("scene", scene_path.lexically_relative(options.output_directory).generic_string());
            }
            entries.emplace_back(std::move(entry));

            std::cout << result.import.report.summary();
            for (const std::string& note : result.notes) std::cout << "  note: " << note << '\n';
            if (!result.import.succeeded())
            {
                ++failures;
                if (options.fail_fast) break;
            }
        }
        daedalus::JsonValue::Object manifest;
        manifest.emplace("schema", std::string(kManifestSchema));
        manifest.emplace("tool_version", std::string(daedalus::kVersion));
        manifest.emplace("mode", options.mode);
        manifest.emplace("compatibility_importer", daedalus::compatibility_importer_identity());
        manifest.emplace("entries", std::move(entries));
        manifest.emplace("failures", failures);
        write_text(options.output_directory / "batch-manifest.json", daedalus::serialize_json(manifest, true));
        std::cout << "Processed " << inputs.size() << " input(s); failures=" << failures << '\n';
        return failures == 0U ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "asset batch failure: " << error.what() << '\n';
        return 2;
    }
}
