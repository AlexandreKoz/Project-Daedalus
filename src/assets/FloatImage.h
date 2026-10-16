#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace daedalus
{
// Linear float32 RGB image with top-left origin, tightly packed (width * 3 floats per row).
struct FloatImageRgb
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> rgb;
};

// Portable FloatMap (PFM, "PF" colour variant) writer/reader. Files are written little-endian
// (negative scale) with rows bottom-to-top as the format requires; the reader accepts either
// endianness, validates dimensions against `maximum_pixels`, and never trusts header sizes.
void write_pfm(const std::filesystem::path& path, const FloatImageRgb& image);
[[nodiscard]] FloatImageRgb read_pfm(const std::filesystem::path& path, std::uint64_t maximum_pixels = 64ULL * 1024ULL * 1024ULL);

// Converts IEEE half floats (R16G16B16A16_FLOAT rows) to float.
[[nodiscard]] float half_to_float(std::uint16_t bits) noexcept;
}
