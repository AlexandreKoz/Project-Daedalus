#include "assets/FloatImage.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace daedalus
{
namespace
{
[[nodiscard]] std::uint32_t byte_swap32(std::uint32_t value) noexcept
{
    return ((value & 0x000000FFU) << 24U) | ((value & 0x0000FF00U) << 8U) | ((value & 0x00FF0000U) >> 8U) |
           ((value & 0xFF000000U) >> 24U);
}

[[nodiscard]] std::string read_token(std::istream& input)
{
    std::string token;
    char character = 0;
    while (input.get(character))
    {
        if (character == ' ' || character == '\n' || character == '\r' || character == '\t')
        {
            if (!token.empty()) return token;
            continue;
        }
        token.push_back(character);
        if (token.size() > 64U) throw std::runtime_error("PFM header token is too long");
    }
    if (token.empty()) throw std::runtime_error("PFM header is truncated");
    return token;
}
}

float half_to_float(std::uint16_t bits) noexcept
{
    const std::uint32_t sign = (static_cast<std::uint32_t>(bits) & 0x8000U) << 16U;
    const std::uint32_t exponent = (bits >> 10U) & 0x1FU;
    std::uint32_t mantissa = bits & 0x03FFU;
    std::uint32_t result = 0;
    if (exponent == 0U)
    {
        if (mantissa == 0U)
        {
            result = sign;
        }
        else
        {
            int shift = 0;
            while ((mantissa & 0x0400U) == 0U)
            {
                mantissa <<= 1U;
                ++shift;
            }
            mantissa &= 0x03FFU;
            result = sign | (static_cast<std::uint32_t>(127 - 15 - shift + 1) << 23U) | (mantissa << 13U);
        }
    }
    else if (exponent == 0x1FU)
    {
        result = sign | 0x7F800000U | (mantissa << 13U);
    }
    else
    {
        result = sign | ((exponent + (127U - 15U)) << 23U) | (mantissa << 13U);
    }
    return std::bit_cast<float>(result);
}

void write_pfm(const std::filesystem::path& path, const FloatImageRgb& image)
{
    if (image.width == 0U || image.height == 0U) throw std::invalid_argument("PFM image dimensions must be nonzero");
    const std::uint64_t expected = static_cast<std::uint64_t>(image.width) * image.height * 3U;
    if (image.rgb.size() != expected) throw std::invalid_argument("PFM image payload does not match its dimensions");
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("failed to open PFM output: " + path.string());
    std::ostringstream header;
    header << "PF\n" << image.width << ' ' << image.height << "\n-1.0\n";
    const std::string text = header.str();
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    const bool little_endian = std::endian::native == std::endian::little;
    std::vector<float> row(static_cast<std::size_t>(image.width) * 3U);
    for (std::uint32_t y = image.height; y-- > 0U;)
    {
        std::memcpy(row.data(), image.rgb.data() + static_cast<std::size_t>(y) * row.size(), row.size() * sizeof(float));
        if (!little_endian)
            for (float& value : row) value = std::bit_cast<float>(byte_swap32(std::bit_cast<std::uint32_t>(value)));
        output.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(float)));
    }
    if (!output) throw std::runtime_error("failed to write PFM output: " + path.string());
}

FloatImageRgb read_pfm(const std::filesystem::path& path, std::uint64_t maximum_pixels)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("failed to open PFM input: " + path.string());
    if (read_token(input) != "PF") throw std::runtime_error("only colour PFM ('PF') images are supported: " + path.string());
    const std::string width_token = read_token(input);
    const std::string height_token = read_token(input);
    const std::string scale_token = read_token(input);
    const unsigned long long width = std::stoull(width_token);
    const unsigned long long height = std::stoull(height_token);
    const double scale = std::stod(scale_token);
    if (width == 0U || height == 0U || width > 0xFFFFFFFFULL || height > 0xFFFFFFFFULL ||
        width > maximum_pixels / height || !std::isfinite(scale) || scale == 0.0)
        throw std::runtime_error("PFM header has invalid dimensions or scale: " + path.string());
    const bool file_little_endian = scale < 0.0;
    const bool swap = file_little_endian != (std::endian::native == std::endian::little);
    FloatImageRgb image;
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.rgb.resize(static_cast<std::size_t>(width * height * 3U));
    const std::size_t row_floats = static_cast<std::size_t>(width) * 3U;
    for (std::uint32_t y = image.height; y-- > 0U;)
    {
        float* row = image.rgb.data() + static_cast<std::size_t>(y) * row_floats;
        input.read(reinterpret_cast<char*>(row), static_cast<std::streamsize>(row_floats * sizeof(float)));
        if (!input) throw std::runtime_error("PFM payload is truncated: " + path.string());
        if (swap)
            for (std::size_t index = 0; index < row_floats; ++index)
                row[index] = std::bit_cast<float>(byte_swap32(std::bit_cast<std::uint32_t>(row[index])));
    }
    return image;
}
}
