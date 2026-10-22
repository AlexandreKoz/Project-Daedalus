// DaedalusConvergence: measures path-tracer convergence from linear PFM captures (Campaign E).
//
//   DaedalusConvergence --reference <ref.pfm> --sample <spp>=<capture.pfm>... [--output <summary.json>]
//
// Reports linear-radiance error metrics per capture and the fitted log-log RMSE slope. It reports
// measurements only; it never decides pass/fail thresholds.

#include "assets/FloatImage.h"
#include "rendering/ConvergenceAnalysis.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

int main(int argc, char** argv)
{
    try
    {
        std::filesystem::path reference_path;
        std::filesystem::path output_path;
        std::vector<std::pair<std::uint64_t, std::filesystem::path>> captures;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument = argv[index];
            if (index + 1 >= argc) throw std::invalid_argument(std::string(argument) + " requires a value");
            const std::string value = argv[++index];
            if (argument == "--reference") reference_path = value;
            else if (argument == "--output") output_path = value;
            else if (argument == "--sample")
            {
                const std::size_t separator = value.find('=');
                if (separator == std::string::npos || separator == 0U) throw std::invalid_argument("--sample expects <spp>=<path>");
                std::size_t consumed = 0;
                const unsigned long long spp = std::stoull(value.substr(0, separator), &consumed);
                if (consumed != separator || spp == 0ULL) throw std::invalid_argument("--sample sample count must be a positive integer");
                captures.emplace_back(spp, std::filesystem::path(value.substr(separator + 1U)));
            }
            else throw std::invalid_argument("unknown option " + std::string(argument));
        }
        if (reference_path.empty() || captures.empty())
            throw std::invalid_argument("usage: DaedalusConvergence --reference <ref.pfm> --sample <spp>=<capture.pfm>... [--output <json>]");

        const daedalus::FloatImageRgb reference = daedalus::read_pfm(reference_path);
        std::vector<daedalus::ConvergenceSample> samples;
        for (const auto& [spp, path] : captures)
        {
            const daedalus::FloatImageRgb capture = daedalus::read_pfm(path);
            if (capture.width != reference.width || capture.height != reference.height)
                throw std::runtime_error("capture dimensions differ from the reference: " + path.string());
            samples.push_back({spp, daedalus::compare_linear_float_rgb(reference.rgb, capture.rgb)});
        }
        const daedalus::ConvergenceSummary summary = daedalus::summarize_convergence(std::move(samples));
        const std::string json = daedalus::convergence_summary_json(summary);
        std::cout << json << '\n';
        if (!output_path.empty())
        {
            std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("cannot open " + output_path.string());
            output << json << '\n';
            if (!output) throw std::runtime_error("cannot write " + output_path.string());
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "convergence tool failure: " << error.what() << '\n';
        return 2;
    }
}
