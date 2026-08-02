#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace daedalus
{
class Sha256 final
{
public:
    Sha256();
    void update(std::span<const std::byte> data);
    void update(std::string_view text);
    [[nodiscard]] std::array<std::byte, 32> finish();

private:
    void transform(const std::byte* block);

    std::array<std::uint32_t, 8> state_{};
    std::array<std::byte, 64> buffer_{};
    std::uint64_t total_bytes_ = 0;
    std::size_t buffered_bytes_ = 0;
    bool finished_ = false;
};

[[nodiscard]] std::string sha256_hex(std::span<const std::byte> data);
[[nodiscard]] std::string sha256_hex(std::string_view text);
}
