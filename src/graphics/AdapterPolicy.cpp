#include "graphics/AdapterPolicy.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace daedalus
{
std::int64_t adapter_rank(const AdapterCandidate& candidate) noexcept
{
    if (!candidate.supports_d3d12 || candidate.software)
    {
        return std::numeric_limits<std::int64_t>::min();
    }

    constexpr std::uint64_t one_megabyte = 1024ULL * 1024ULL;
    const std::uint64_t memory_megabytes = candidate.dedicated_video_memory / one_megabyte;
    const std::uint64_t capped_memory = std::min<std::uint64_t>(memory_megabytes, 1'000'000ULL);
    const std::uint32_t capped_order = std::min<std::uint32_t>(candidate.enumeration_order, 1'000'000U);
    const std::int64_t preference_bonus = static_cast<std::int64_t>(1'000'000U - capped_order);
    return static_cast<std::int64_t>(capped_memory * 1'000'001ULL) + preference_bonus;
}

std::size_t choose_adapter_index(std::span<const AdapterCandidate> candidates)
{
    std::size_t best_index = candidates.size();
    std::int64_t best_rank = std::numeric_limits<std::int64_t>::min();

    for (std::size_t index = 0; index < candidates.size(); ++index)
    {
        const std::int64_t rank = adapter_rank(candidates[index]);
        if (rank > best_rank)
        {
            best_rank = rank;
            best_index = index;
        }
    }

    if (best_index == candidates.size() || best_rank == std::numeric_limits<std::int64_t>::min())
    {
        throw std::runtime_error("no suitable hardware Direct3D 12 adapter was found");
    }

    return best_index;
}
}
