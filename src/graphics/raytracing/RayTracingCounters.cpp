#include "graphics/raytracing/RayTracingCounters.h"

#include "core/Error.h"
#include "graphics/D3D12Helpers.h"

#include <cstring>
#include <sstream>
#include <stdexcept>

namespace daedalus
{
const char* to_string(RtCounter counter) noexcept
{
    switch (counter)
    {
    case RtCounter::primary_samples: return "primary_samples";
    case RtCounter::path_segments: return "path_segments";
    case RtCounter::nonfinite_radiance: return "nonfinite_radiance";
    case RtCounter::nonfinite_throughput: return "nonfinite_throughput";
    case RtCounter::invalid_pdf: return "invalid_pdf";
    case RtCounter::negative_radiance: return "negative_radiance";
    case RtCounter::clamped_samples: return "clamped_samples";
    case RtCounter::terminated_russian_roulette: return "terminated_russian_roulette";
    case RtCounter::terminated_max_depth: return "terminated_max_depth";
    case RtCounter::terminated_miss: return "terminated_miss";
    case RtCounter::terminated_absorbed: return "terminated_absorbed";
    case RtCounter::visibility_rays: return "visibility_rays";
    case RtCounter::total_internal_reflections: return "total_internal_reflections";
    case RtCounter::light_samples: return "light_samples";
    case RtCounter::reflection_rays: return "reflection_rays";
    case RtCounter::reserved0: return "reserved0";
    case RtCounter::count: break;
    }
    return "unknown";
}

RayTracingCounters::RayTracingCounters(ID3D12Device* device)
{
    counters_ = create_buffer(device, kRtCounterBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, L"Daedalus RT counters");
    zeros_ = create_buffer(device, kRtCounterBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                           D3D12_RESOURCE_FLAG_NONE, L"Daedalus RT counter zeros");
    void* mapped_zeros = nullptr;
    const D3D12_RANGE read_range{0, 0};
    DAEDALUS_THROW_IF_FAILED(zeros_->Map(0, &read_range, &mapped_zeros));
    std::memset(mapped_zeros, 0, kRtCounterBytes);
    zeros_->Unmap(0, nullptr);
    for (std::uint32_t frame = 0; frame < D3D12Context::kFrameCount; ++frame)
    {
        readback_[frame] = create_buffer(device, kRtCounterBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                                         D3D12_RESOURCE_FLAG_NONE, L"Daedalus RT counter readback");
        void* mapped = nullptr;
        const D3D12_RANGE range{0, kRtCounterBytes};
        DAEDALUS_THROW_IF_FAILED(readback_[frame]->Map(0, &range, &mapped));
        mapped_[frame] = static_cast<const std::uint32_t*>(mapped);
    }
}

RayTracingCounters::~RayTracingCounters()
{
    for (std::size_t index = 0; index < readback_.size(); ++index)
    {
        if (readback_[index] != nullptr && mapped_[index] != nullptr) readback_[index]->Unmap(0, nullptr);
        mapped_[index] = nullptr;
    }
}

void RayTracingCounters::begin(ID3D12GraphicsCommandList* command_list, std::uint32_t frame_index)
{
    if (frame_index >= D3D12Context::kFrameCount) throw std::out_of_range("RT counter frame index out of range");
    latest_ = {};
    if (pending_[frame_index])
    {
        latest_.valid = true;
        for (std::uint32_t index = 0; index < kRtCounterCount; ++index)
        {
            latest_.values[index] = mapped_[frame_index][index];
            cumulative_.values[index] += mapped_[frame_index][index];
        }
        cumulative_.valid = true;
        pending_[frame_index] = false;
    }
    const D3D12_RESOURCE_BARRIER to_copy = transition_barrier(counters_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                              D3D12_RESOURCE_STATE_COPY_DEST);
    command_list->ResourceBarrier(1, &to_copy);
    command_list->CopyBufferRegion(counters_.Get(), 0, zeros_.Get(), 0, kRtCounterBytes);
    const D3D12_RESOURCE_BARRIER to_uav = transition_barrier(counters_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    command_list->ResourceBarrier(1, &to_uav);
}

void RayTracingCounters::end(ID3D12GraphicsCommandList* command_list, std::uint32_t frame_index)
{
    if (frame_index >= D3D12Context::kFrameCount) throw std::out_of_range("RT counter frame index out of range");
    const D3D12_RESOURCE_BARRIER to_copy = transition_barrier(counters_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                              D3D12_RESOURCE_STATE_COPY_SOURCE);
    command_list->ResourceBarrier(1, &to_copy);
    command_list->CopyBufferRegion(readback_[frame_index].Get(), 0, counters_.Get(), 0, kRtCounterBytes);
    const D3D12_RESOURCE_BARRIER to_uav = transition_barrier(counters_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    command_list->ResourceBarrier(1, &to_uav);
    pending_[frame_index] = true;
}

std::string RayTracingCounters::to_json(const RayTracingCounterSnapshot& snapshot) const
{
    std::ostringstream stream;
    stream << "{\"valid\": " << (snapshot.valid ? "true" : "false");
    for (std::uint32_t index = 0; index < kRtCounterCount; ++index)
        stream << ", \"" << to_string(static_cast<RtCounter>(index)) << "\": " << snapshot.values[index];
    stream << '}';
    return stream.str();
}
}
