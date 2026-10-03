#include "assets/ImageDecoder.h"
#include "assets/ImageEncoder.h"
#include "rendering/CampaignC2Math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>
#endif

namespace
{
std::vector<std::byte> read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("failed to open image: " + path.string());
    const std::streamoff end = input.tellg();
    if (end <= 0) throw std::runtime_error("image file is empty: " + path.string());
    input.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.read(reinterpret_cast<char*>(bytes.data()), end);
    if (!input) throw std::runtime_error("failed to read image: " + path.string());
    return bytes;
}

void write_text(const std::filesystem::path& path, const std::string& text)
{
    const auto parent = path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("failed to open output: " + path.string());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("failed to write output: " + path.string());
}

float parse_float(const char* value, const char* option)
{
    try
    {
        std::size_t used = 0;
        const float parsed = std::stof(value, &used);
        if (used != std::string(value).size() || !std::isfinite(parsed) || parsed < 0.0F)
            throw std::runtime_error(std::string(option) + " requires a finite non-negative number");
        return parsed;
    }
    catch (const std::exception&)
    {
        throw std::runtime_error(std::string(option) + " requires a finite non-negative number");
    }
}

std::string json_escape(std::string_view value)
{
    std::string result;
    for (const char c : value)
    {
        if (c == '\\') result += "\\\\";
        else if (c == '"') result += "\\\"";
        else if (c == '\n') result += "\\n";
        else result += c;
    }
    return result;
}
}

int main(int argc, char** argv)
{
    try
    {
#if defined(_WIN32)
        struct ComGuard final
        {
            bool initialized = false;
            ComGuard()
            {
                const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                if (FAILED(result) && result != RPC_E_CHANGED_MODE)
                    throw std::runtime_error("COM initialization failed for image comparison tool");
                initialized = SUCCEEDED(result);
            }
            ~ComGuard() { if (initialized) CoUninitialize(); }
        } com_guard;
#endif
        if (argc < 5)
        {
            std::cerr << "Usage: DaedalusImageCompare <reference.png> <candidate.png> <diff.png> <report.json> "
                         "[--mae v] [--rmse v] [--max v] [--pixel-threshold v] [--fraction v] "
                         "[--reference-metadata path] [--candidate-metadata path]\n";
            return 2;
        }
        daedalus::ImageComparisonThresholds thresholds;
        std::string reference_metadata;
        std::string candidate_metadata;
        for (int i = 5; i < argc; ++i)
        {
            const std::string option = argv[i];
            if (i + 1 >= argc) throw std::runtime_error("missing value after " + option);
            const char* raw_value = argv[++i];
            if (option == "--reference-metadata") reference_metadata = raw_value;
            else if (option == "--candidate-metadata") candidate_metadata = raw_value;
            else
            {
                const float value = parse_float(raw_value, option.c_str());
                if (option == "--mae") thresholds.mean_absolute_error = value;
                else if (option == "--rmse") thresholds.root_mean_square_error = value;
                else if (option == "--max") thresholds.maximum_absolute_error = value;
                else if (option == "--pixel-threshold") thresholds.per_channel_threshold = value;
                else if (option == "--fraction") thresholds.maximum_fraction_over_threshold = value;
                else throw std::runtime_error("unknown option: " + option);
            }
        }

        const auto reference_bytes = read_file(argv[1]);
        const auto candidate_bytes = read_file(argv[2]);
        const auto reference = daedalus::decode_image_rgba8(reference_bytes, "image/png");
        const auto candidate = daedalus::decode_image_rgba8(candidate_bytes, "image/png");
        if (reference.width != candidate.width || reference.height != candidate.height)
            throw std::runtime_error("reference and candidate dimensions differ");

        const auto reference_linear = daedalus::rgba8_srgb_to_linear_rgb(reference.rgba8);
        const auto candidate_linear = daedalus::rgba8_srgb_to_linear_rgb(candidate.rgba8);
        const auto metrics = daedalus::compare_linear_rgb(reference_linear, candidate_linear, thresholds);

        std::vector<std::byte> diff(reference.rgba8.size(), std::byte{0});
        for (std::size_t pixel = 0; pixel < reference_linear.size(); ++pixel)
        {
            const daedalus::Vec3 delta{
                std::abs(reference_linear[pixel].x - candidate_linear[pixel].x),
                std::abs(reference_linear[pixel].y - candidate_linear[pixel].y),
                std::abs(reference_linear[pixel].z - candidate_linear[pixel].z)};
            const float scale = 4.0F;
            const auto byte = [](float v) -> std::byte {
                const float clamped = std::clamp(v, 0.0F, 1.0F);
                return static_cast<std::byte>(static_cast<unsigned int>(std::lround(clamped * 255.0F)));
            };
            diff[pixel * 4U + 0U] = byte(delta.x * scale);
            diff[pixel * 4U + 1U] = byte(delta.y * scale);
            diff[pixel * 4U + 2U] = byte(delta.z * scale);
            diff[pixel * 4U + 3U] = std::byte{0xff};
        }
        daedalus::encode_png_rgba8(argv[3], reference.width, reference.height, diff);
        const std::string metrics_json = daedalus::image_comparison_metrics_json(metrics, thresholds);
        std::ostringstream report;
        report << "{\n"
               << "  \"schema\": \"daedalus.image-regression/1\",\n"
               << "  \"reference_image\": \"" << json_escape(argv[1]) << "\",\n"
               << "  \"candidate_image\": \"" << json_escape(argv[2]) << "\",\n"
               << "  \"difference_image\": \"" << json_escape(argv[3]) << "\",\n"
               << "  \"reference_metadata\": " << (reference_metadata.empty() ? "null" : "\"" + json_escape(reference_metadata) + "\"") << ",\n"
               << "  \"candidate_metadata\": " << (candidate_metadata.empty() ? "null" : "\"" + json_escape(candidate_metadata) + "\"") << ",\n"
               << "  \"metrics\": " << metrics_json;
        if (!metrics_json.empty() && metrics_json.back() == '\n') {}
        report << "}\n";
        write_text(argv[4], report.str());
        std::cout << (metrics.pass ? "PASS" : "FAIL")
                  << " mae=" << metrics.mean_absolute_error
                  << " rmse=" << metrics.root_mean_square_error
                  << " max=" << metrics.maximum_absolute_error
                  << " fraction=" << metrics.fraction_over_threshold << '\n';
        return metrics.pass ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Image comparison error: " << error.what() << '\n';
        return 2;
    }
}
