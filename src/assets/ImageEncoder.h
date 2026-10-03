#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace daedalus
{
void encode_png_rgba8(const std::filesystem::path& path,
                      std::uint32_t width,
                      std::uint32_t height,
                      std::span<const std::byte> rgba8);
}
