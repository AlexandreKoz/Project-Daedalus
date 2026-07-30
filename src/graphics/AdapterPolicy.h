#pragma once

#include <cstdint>
#include <span>

namespace daedalus
{
struct AdapterCandidate
{
    bool supports_d3d12 = false;
    bool software = false;
    std::uint64_t dedicated_video_memory = 0;
    std::uint32_t enumeration_order = 0;
};

[[nodiscard]] std::int64_t adapter_rank(const AdapterCandidate& candidate) noexcept;
[[nodiscard]] std::size_t choose_adapter_index(std::span<const AdapterCandidate> candidates);
}
