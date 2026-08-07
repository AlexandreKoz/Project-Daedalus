#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace daedalus
{
struct DecodedImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t source_components = 0;
    std::uint64_t row_stride = 0;
    std::vector<std::byte> rgba8;
};

class ImageDecodeError final : public std::runtime_error
{
public:
    explicit ImageDecodeError(std::string message) : std::runtime_error(std::move(message)) {}
};

// Fully decodes the declared Campaign B PNG/JPEG subset to owned top-left-origin RGBA8 pixels.
// The implementation uses WIC on Windows and libpng/libjpeg-turbo on portable builds, behind
// one deterministic assets-layer contract. Callers must enforce their own resource budgets
// before invoking the decoder.
[[nodiscard]] DecodedImage decode_image_rgba8(std::span<const std::byte> encoded,
                                              std::string_view mime_type);
}
