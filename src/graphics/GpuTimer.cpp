#include "graphics/GpuTimer.h"

#include "core/Error.h"
#include "core/Log.h"
#include "graphics/D3D12Helpers.h"

#include <stdexcept>

namespace daedalus
{
const char* to_string(GpuPass pass) noexcept
{
    switch (pass)
    {
    case GpuPass::shadow: return "shadow";
    case GpuPass::opaque: return "opaque";
    case GpuPass::transparent: return "transparent";
    case GpuPass::acceleration_structure: return "acceleration_structure";
    case GpuPass::ray_dispatch: return "ray_dispatch";
    case GpuPass::denoise: return "denoise";
    case GpuPass::composite: return "composite";
    case GpuPass::upscale: return "upscale";
    case GpuPass::tone_map: return "tone_map";
    case GpuPass::count: break;
    }
    return "unknown";
}

GpuTimer::GpuTimer(D3D12Context& context) : context_(context)
{
    frequency_ = context_.timestamp_frequency();
    if (frequency_ == 0U)
    {
        Log::warning("D3D12 timestamp frequency is unavailable; GPU timing will report invalid");
        return;
    }
    ID3D12Device* device = context_.device();
    D3D12_QUERY_HEAP_DESC description{};
    description.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    description.Count = kMaximumMarks * D3D12Context::kFrameCount;
    DAEDALUS_THROW_IF_FAILED(device->CreateQueryHeap(&description, IID_PPV_ARGS(&query_heap_)));
    const std::uint64_t bytes = static_cast<std::uint64_t>(description.Count) * sizeof(std::uint64_t);
    readback_ = create_buffer(device, bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                              D3D12_RESOURCE_FLAG_NONE, L"Daedalus timestamp readback");
    void* mapped = nullptr;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
    DAEDALUS_THROW_IF_FAILED(readback_->Map(0, &range, &mapped));
    mapped_ = static_cast<std::uint64_t*>(mapped);
}

GpuTimer::~GpuTimer()
{
    if (readback_ != nullptr && mapped_ != nullptr)
    {
        readback_->Unmap(0, nullptr);
        mapped_ = nullptr;
    }
}

void GpuTimer::begin_frame(ID3D12GraphicsCommandList* command_list, std::uint32_t frame_index)
{
    if (frame_index >= D3D12Context::kFrameCount) throw std::out_of_range("GPU timer frame index out of range");
    consume(frame_index);
    frame_index_ = frame_index;
    mark_count_ = 0;
    slot_passes_[frame_index].clear();
    slot_valid_[frame_index] = false;
    recording_ = query_heap_ != nullptr;
    if (!recording_) return;
    command_list->EndQuery(query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_index * kMaximumMarks);
    mark_count_ = 1;
}

void GpuTimer::mark(ID3D12GraphicsCommandList* command_list, GpuPass pass)
{
    if (!recording_) return;
    if (mark_count_ >= kMaximumMarks) throw std::logic_error("GPU timer mark capacity exceeded");
    command_list->EndQuery(query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_index_ * kMaximumMarks + mark_count_);
    slot_passes_[frame_index_].push_back(pass);
    ++mark_count_;
}

void GpuTimer::end_frame(ID3D12GraphicsCommandList* command_list)
{
    if (!recording_) return;
    const UINT base = frame_index_ * kMaximumMarks;
    command_list->ResolveQueryData(query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, base, mark_count_, readback_.Get(),
                                   static_cast<UINT64>(base) * sizeof(std::uint64_t));
    slot_valid_[frame_index_] = true;
    recording_ = false;
}

void GpuTimer::consume(std::uint32_t frame_index) noexcept
{
    latest_ = {};
    if (mapped_ == nullptr || frequency_ == 0U || !slot_valid_[frame_index]) return;
    const std::vector<GpuPass>& passes = slot_passes_[frame_index];
    const std::size_t base = static_cast<std::size_t>(frame_index) * kMaximumMarks;
    const double to_ms = 1000.0 / static_cast<double>(frequency_);
    std::uint64_t previous = mapped_[base];
    for (std::size_t index = 0; index < passes.size(); ++index)
    {
        const std::uint64_t value = mapped_[base + index + 1U];
        if (value < previous) return; // non-monotonic timestamps: report invalid rather than garbage
        latest_.pass_ms[static_cast<std::size_t>(passes[index])] += static_cast<double>(value - previous) * to_ms;
        previous = value;
    }
    latest_.valid = !passes.empty();
    latest_.total_ms = static_cast<double>(previous - mapped_[base]) * to_ms;
    latest_.shadow_ms = latest_.pass_ms[static_cast<std::size_t>(GpuPass::shadow)];
    latest_.opaque_ms = latest_.pass_ms[static_cast<std::size_t>(GpuPass::opaque)];
    latest_.transparent_ms = latest_.pass_ms[static_cast<std::size_t>(GpuPass::transparent)];
    latest_.tone_map_ms = latest_.pass_ms[static_cast<std::size_t>(GpuPass::tone_map)];
}
}
