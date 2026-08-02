#pragma once

#ifndef _WIN32
#error WicImageDecoder is available only on Windows.
#endif

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace daedalus
{
struct DecodedImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::byte> rgba8;
};

[[nodiscard]] DecodedImage decode_image_wic(std::span<const std::byte> encoded);
}
