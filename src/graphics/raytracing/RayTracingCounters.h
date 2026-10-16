#pragma once

#ifndef _WIN32
#error RayTracingCounters is available only on Windows.
#endif

#include "graphics/D3D12Context.h"
#include "rendering/RayTracingContract.h"

#include <array>
#include <cstdint>
#include <string>

namespace daedalus
{
struct RayTracingCounterSnapshot
{
    bool valid = false;
    std::array<std::uint64_t, kRtCounterCount> values{};
    [[nodiscard]] std::uint64_t operator[](RtCounter counter) const noexcept { return values[static_cast<std::size_t>(counter)]; }
};

[[nodiscard]] const char* to_string(RtCounter counter) noexcept;

// GPU event counters (UAV raw buffer) with per-frame-slot readback. Values are measured by the
// shaders with wave-aggregated atomics; nothing is estimated on the CPU. A slot's readback is only
// consumed after D3D12Context::begin_frame waited for that slot's fence.
class RayTracingCounters final
{
public:
    explicit RayTracingCounters(ID3D12Device* device);
    ~RayTracingCounters();

    RayTracingCounters(const RayTracingCounters&) = delete;
    RayTracingCounters& operator=(const RayTracingCounters&) = delete;
    RayTracingCounters(RayTracingCounters&&) = delete;
    RayTracingCounters& operator=(RayTracingCounters&&) = delete;

    // Reads the previous result for `frame_index`, then zeroes the counters (buffer ends in UAV state).
    void begin(ID3D12GraphicsCommandList* command_list, std::uint32_t frame_index);
    // Copies the counters to the slot's readback buffer (buffer returns to UAV state).
    void end(ID3D12GraphicsCommandList* command_list, std::uint32_t frame_index);

    [[nodiscard]] ID3D12Resource* buffer() const noexcept { return counters_.Get(); }
    [[nodiscard]] const RayTracingCounterSnapshot& latest() const noexcept { return latest_; }
    [[nodiscard]] const RayTracingCounterSnapshot& cumulative() const noexcept { return cumulative_; }
    [[nodiscard]] std::string to_json(const RayTracingCounterSnapshot& snapshot) const;

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> counters_;
    Microsoft::WRL::ComPtr<ID3D12Resource> zeros_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, D3D12Context::kFrameCount> readback_{};
    std::array<const std::uint32_t*, D3D12Context::kFrameCount> mapped_{};
    std::array<bool, D3D12Context::kFrameCount> pending_{};
    RayTracingCounterSnapshot latest_{};
    RayTracingCounterSnapshot cumulative_{};
};
}
