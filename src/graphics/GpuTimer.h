#pragma once

#ifndef _WIN32
#error GpuTimer is available only on Windows.
#endif

#include "graphics/D3D12Context.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace daedalus
{
// Named GPU pass intervals measured with D3D12 timestamp queries. Each frame slot owns its own
// query range and readback range; results for a slot are consumed only after D3D12Context has
// waited on that slot's fence in begin_frame, so the readback never races the GPU.
enum class GpuPass : std::uint32_t
{
    shadow = 0,
    opaque,
    transparent,
    acceleration_structure,
    ray_dispatch,
    denoise,
    composite,
    upscale,
    tone_map,
    count
};

[[nodiscard]] const char* to_string(GpuPass pass) noexcept;

struct GpuPassTimings
{
    bool valid = false;
    double shadow_ms = 0.0;
    double opaque_ms = 0.0;
    double transparent_ms = 0.0;
    double tone_map_ms = 0.0;
    double total_ms = 0.0;
    std::array<double, static_cast<std::size_t>(GpuPass::count)> pass_ms{};
};

class GpuTimer final
{
public:
    explicit GpuTimer(D3D12Context& context);
    ~GpuTimer();

    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;
    GpuTimer(GpuTimer&&) = delete;
    GpuTimer& operator=(GpuTimer&&) = delete;

    // Consumes the previous result for this slot and writes the frame-start timestamp.
    void begin_frame(ID3D12GraphicsCommandList* command_list, std::uint32_t frame_index);
    // Writes a timestamp; the interval since the previous mark is attributed to `pass`.
    void mark(ID3D12GraphicsCommandList* command_list, GpuPass pass);
    void end_frame(ID3D12GraphicsCommandList* command_list);

    [[nodiscard]] const GpuPassTimings& latest() const noexcept { return latest_; }
    [[nodiscard]] bool available() const noexcept { return query_heap_ != nullptr; }

private:
    static constexpr std::uint32_t kMaximumMarks = 24U;

    void consume(std::uint32_t frame_index) noexcept;

    D3D12Context& context_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> query_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    std::uint64_t* mapped_ = nullptr;
    std::uint64_t frequency_ = 0;
    std::uint32_t frame_index_ = 0;
    std::uint32_t mark_count_ = 0;
    bool recording_ = false;
    std::array<std::vector<GpuPass>, D3D12Context::kFrameCount> slot_passes_{};
    std::array<bool, D3D12Context::kFrameCount> slot_valid_{};
    GpuPassTimings latest_{};
};
}
