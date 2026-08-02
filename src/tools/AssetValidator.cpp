#include "assets/GltfImporter.h"
#include "scene/Scene.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
void write_text(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("unable to open report output: " + path.string());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("unable to write report output: " + path.string());
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc < 2)
        {
            std::cerr << "Usage: DaedalusAssetValidator <asset.gltf|asset.glb> [--scene <index-or-name>] [--report <path>] [--dump-scene] [--expect-success|--expect-failure]\n";
            return 2;
        }
        std::filesystem::path source = argv[1];
        std::filesystem::path report_path;
        daedalus::ImportSettings settings;
        bool dump_scene = false;
        bool expect_success = false;
        bool expect_failure = false;
        for (int index = 2; index < argc; ++index)
        {
            const std::string_view argument = argv[index];
            if (argument == "--scene" && index + 1 < argc) settings.scene_selector = argv[++index];
            else if (argument == "--report" && index + 1 < argc) report_path = argv[++index];
            else if (argument == "--dump-scene") dump_scene = true;
            else if (argument == "--expect-success") expect_success = true;
            else if (argument == "--expect-failure") expect_failure = true;
            else throw std::runtime_error("unknown or incomplete argument: " + std::string(argument));
        }

        if (expect_success && expect_failure) throw std::runtime_error("expectation flags are mutually exclusive");

        const daedalus::ImportResult result = daedalus::GltfImporter{}.import_file(source, settings);
        std::cout << result.report.summary();
        for (const daedalus::Diagnostic& diagnostic : result.report.diagnostics)
        {
            std::cout << '[' << daedalus::to_string(diagnostic.severity) << "] "
                      << daedalus::to_string(diagnostic.code) << " at " << diagnostic.location
                      << ": " << diagnostic.message << '\n';
        }
        if (dump_scene && result.succeeded()) std::cout << daedalus::dump_scene_hierarchy(result.scene);
        if (!report_path.empty()) write_text(report_path, result.report.to_json());
        if (expect_success) return result.succeeded() ? 0 : 3;
        if (expect_failure) return result.succeeded() ? 3 : 0;
        return result.succeeded() ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "asset validator failure: " << error.what() << '\n';
        return 2;
    }
}
