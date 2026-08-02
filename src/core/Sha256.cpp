#include "core/Sha256.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace daedalus
{
namespace
{
constexpr std::array<std::uint32_t, 64> kConstants{
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};

constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned count)
{
    return (value >> count) | (value << (32U - count));
}

std::uint32_t read_big_endian(const std::byte* data)
{
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}
}

Sha256::Sha256()
    : state_{0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U}
{
}

void Sha256::update(std::span<const std::byte> data)
{
    if (finished_) throw std::logic_error("cannot update a finished SHA-256 state");
    total_bytes_ += data.size();
    while (!data.empty())
    {
        const std::size_t count = std::min(buffer_.size() - buffered_bytes_, data.size());
        std::memcpy(buffer_.data() + buffered_bytes_, data.data(), count);
        buffered_bytes_ += count;
        data = data.subspan(count);
        if (buffered_bytes_ == buffer_.size())
        {
            transform(buffer_.data());
            buffered_bytes_ = 0;
        }
    }
}

void Sha256::update(std::string_view text)
{
    update(std::as_bytes(std::span(text.data(), text.size())));
}

std::array<std::byte, 32> Sha256::finish()
{
    if (finished_) throw std::logic_error("SHA-256 finish called twice");
    finished_ = true;
    const std::uint64_t bit_count = total_bytes_ * 8U;
    buffer_[buffered_bytes_++] = std::byte{0x80};
    if (buffered_bytes_ > 56)
    {
        std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_bytes_), buffer_.end(), std::byte{0});
        transform(buffer_.data());
        buffered_bytes_ = 0;
    }
    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_bytes_), buffer_.begin() + 56, std::byte{0});
    for (int index = 0; index < 8; ++index)
    {
        buffer_[63 - index] = static_cast<std::byte>((bit_count >> (index * 8)) & 0xFFU);
    }
    transform(buffer_.data());

    std::array<std::byte, 32> digest{};
    for (std::size_t index = 0; index < state_.size(); ++index)
    {
        digest[index * 4] = static_cast<std::byte>((state_[index] >> 24U) & 0xFFU);
        digest[index * 4 + 1] = static_cast<std::byte>((state_[index] >> 16U) & 0xFFU);
        digest[index * 4 + 2] = static_cast<std::byte>((state_[index] >> 8U) & 0xFFU);
        digest[index * 4 + 3] = static_cast<std::byte>(state_[index] & 0xFFU);
    }
    return digest;
}

void Sha256::transform(const std::byte* block)
{
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) words[index] = read_big_endian(block + index * 4);
    for (std::size_t index = 16; index < words.size(); ++index)
    {
        const std::uint32_t s0 = rotate_right(words[index - 15], 7) ^ rotate_right(words[index - 15], 18) ^ (words[index - 15] >> 3U);
        const std::uint32_t s1 = rotate_right(words[index - 2], 17) ^ rotate_right(words[index - 2], 19) ^ (words[index - 2] >> 10U);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }

    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (std::size_t index = 0; index < words.size(); ++index)
    {
        const std::uint32_t sigma1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        const std::uint32_t choose = (e & f) ^ ((~e) & g);
        const std::uint32_t temporary1 = h + sigma1 + choose + kConstants[index] + words[index];
        const std::uint32_t sigma0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temporary2 = sigma0 + majority;
        h = g; g = f; f = e; e = d + temporary1; d = c; c = b; b = a; a = temporary1 + temporary2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

std::string sha256_hex(std::span<const std::byte> data)
{
    Sha256 hash;
    hash.update(data);
    const auto digest = hash.finish();
    constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.resize(64);
    for (std::size_t index = 0; index < digest.size(); ++index)
    {
        const auto value = static_cast<unsigned>(digest[index]);
        output[index * 2] = hex[value >> 4U];
        output[index * 2 + 1] = hex[value & 0x0FU];
    }
    return output;
}

std::string sha256_hex(std::string_view text)
{
    return sha256_hex(std::as_bytes(std::span(text.data(), text.size())));
}
}
