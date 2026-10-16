#include "graphics/raytracing/AccelerationStructureManager.h"

#include "core/Error.h"
#include "core/Log.h"
#include "graphics/D3D12Helpers.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace daedalus
{
static_assert(kDxrInstanceFlagTriangleCullDisable == D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE);
static_assert(kDxrInstanceFlagTriangleFrontCounterClockwise == D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE);
static_assert(kDxrGeometryFlagOpaque == D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE);
static_assert(kDxrGeometryFlagNoDuplicateAnyHit == D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION);
static_assert(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) == 64);

namespace
{
constexpr std::uint64_t kScratchBatchBudget = 128ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kAsAlignment = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;

[[nodiscard]] Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> as_list4(ID3D12GraphicsCommandList* command_list)
{
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> list4;
    DAEDALUS_THROW_IF_FAILED(command_list->QueryInterface(IID_PPV_ARGS(&list4)));
    return list4;
}

[[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Resource> create_acceleration_structure_buffer(ID3D12Device* device,
                                                                                          std::uint64_t size,
                                                                                          std::wstring_view name)
{
    return create_buffer(device, align_up_u64(std::max<std::uint64_t>(size, kAsAlignment), kAsAlignment, "acceleration structure"),
                         D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, name);
}

[[nodiscard]] std::uint64_t allocation_size(ID3D12Device* device, ID3D12Resource* resource) noexcept
{
    if (resource == nullptr) return 0U;
    const D3D12_RESOURCE_DESC description = resource->GetDesc();
    const D3D12_RESOURCE_ALLOCATION_INFO info = device->GetResourceAllocationInfo(0, 1, &description);
    return info.SizeInBytes == std::numeric_limits<std::uint64_t>::max() ? 0U : info.SizeInBytes;
}
}

std::string AccelerationStructureStats::to_json() const
{
    std::ostringstream stream;
    stream << "{\"blas_count\": " << blas_count << ", \"blas_triangles\": " << blas_triangles
           << ", \"blas_bytes_before_compaction\": " << blas_bytes_before_compaction << ", \"blas_bytes\": " << blas_bytes
           << ", \"tlas_bytes\": " << tlas_bytes << ", \"tlas_scratch_bytes\": " << tlas_scratch_bytes
           << ", \"instance_count\": " << instance_count << ", \"tlas_full_builds\": " << tlas_full_builds
           << ", \"tlas_updates\": " << tlas_updates << ", \"compaction_applied\": " << (compaction_applied ? "true" : "false") << '}';
    return stream.str();
}

AccelerationStructureManager::AccelerationStructureManager(D3D12Context& context,
                                                           GpuSceneResources& resources,
                                                           const RtSceneTables& tables,
                                                           std::span<const Mat4> initial_world,
                                                           bool enable_compaction)
    : context_(context), resources_(resources), device_(context.device5())
{
    if (device_ == nullptr) throw std::runtime_error("acceleration structures require ID3D12Device5");
    if (tables.instances.size() != tables.instance_policies.size() || initial_world.size() != tables.instances.size())
        throw std::invalid_argument("acceleration structure instance tables are inconsistent");
    policies_ = tables.instance_policies;
    instance_geometry_.reserve(tables.instances.size());
    for (const RtInstanceData& instance : tables.instances)
    {
        if (instance.geometry_index >= resources_.primitives().size())
            throw std::runtime_error("ray-tracing instance references a geometry without a BLAS");
        instance_geometry_.push_back(instance.geometry_index);
    }
    build_bottom_level(tables, enable_compaction);
    create_top_level();
    stats_.instance_count = tables.instances.size();
    static_cast<void>(initial_world);
}

AccelerationStructureManager::~AccelerationStructureManager()
{
    for (std::size_t index = 0; index < instance_descs_.size(); ++index)
    {
        if (instance_descs_[index] != nullptr && mapped_instance_descs_[index] != nullptr)
            instance_descs_[index]->Unmap(0, nullptr);
        mapped_instance_descs_[index] = nullptr;
    }
}

void AccelerationStructureManager::build_bottom_level(const RtSceneTables& tables, bool enable_compaction)
{
    const auto& primitives = resources_.primitives();
    const std::size_t count = primitives.size();
    if (tables.geometry_flags.size() != count) throw std::invalid_argument("geometry flag table does not match primitives");
    bottom_levels_.resize(count);
    if (count == 0U) return;

    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries(count);
    std::vector<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO> prebuild(count);
    std::uint64_t maximum_scratch = 0;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    if (enable_compaction) flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
    for (std::size_t index = 0; index < count; ++index)
    {
        const GpuSceneResources::GpuPrimitive& primitive = primitives[index];
        D3D12_RAYTRACING_GEOMETRY_DESC& geometry = geometries[index];
        geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometry.Flags = static_cast<D3D12_RAYTRACING_GEOMETRY_FLAGS>(tables.geometry_flags[index]);
        geometry.Triangles.Transform3x4 = 0;
        geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT; // Vertex::position at offset 0
        geometry.Triangles.IndexCount = primitive.index_count;
        geometry.Triangles.VertexCount = primitive.vertex_count;
        geometry.Triangles.IndexBuffer = primitive.index_buffer->GetGPUVirtualAddress();
        geometry.Triangles.VertexBuffer.StartAddress = primitive.vertex_buffer->GetGPUVirtualAddress();
        geometry.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
        stats_.blas_triangles += primitive.index_count / 3U;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.Flags = flags;
        inputs.NumDescs = 1;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.pGeometryDescs = &geometry;
        device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild[index]);
        if (prebuild[index].ResultDataMaxSizeInBytes == 0U)
            throw std::runtime_error("BLAS prebuild info returned a zero result size for primitive " + std::to_string(index));
        maximum_scratch = std::max(maximum_scratch, align_up_u64(prebuild[index].ScratchDataSizeInBytes, kAsAlignment, "BLAS scratch"));
        bottom_levels_[index] = create_acceleration_structure_buffer(device_, prebuild[index].ResultDataMaxSizeInBytes, L"Daedalus BLAS");
        stats_.blas_bytes_before_compaction += allocation_size(device_, bottom_levels_[index].Get());
    }
    const std::uint64_t scratch_size = std::max(maximum_scratch, std::min(kScratchBatchBudget, maximum_scratch * count));
    Microsoft::WRL::ComPtr<ID3D12Resource> scratch = create_buffer(device_, scratch_size, D3D12_HEAP_TYPE_DEFAULT,
                                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, L"Daedalus BLAS scratch");
    const std::uint64_t postbuild_bytes = checked_multiply_u64(count, sizeof(UINT64), "BLAS postbuild info");
    Microsoft::WRL::ComPtr<ID3D12Resource> postbuild;
    Microsoft::WRL::ComPtr<ID3D12Resource> postbuild_readback;
    if (enable_compaction)
    {
        postbuild = create_buffer(device_, postbuild_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                  D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, L"Daedalus BLAS compacted sizes");
        postbuild_readback = create_buffer(device_, postbuild_bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                                           D3D12_RESOURCE_FLAG_NONE, L"Daedalus BLAS compacted size readback");
    }

    context_.execute_immediate([&](ID3D12GraphicsCommandList* command_list)
    {
        const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> list4 = as_list4(command_list);
        std::uint64_t scratch_offset = 0;
        for (std::size_t index = 0; index < count; ++index)
        {
            const std::uint64_t needed = align_up_u64(prebuild[index].ScratchDataSizeInBytes, kAsAlignment, "BLAS scratch");
            if (scratch_offset + needed > scratch_size)
            {
                // Scratch is reused: the previous builds must finish reading/writing it first.
                const D3D12_RESOURCE_BARRIER barrier = uav_barrier(scratch.Get());
                list4->ResourceBarrier(1, &barrier);
                scratch_offset = 0;
            }
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
            build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            build.Inputs.Flags = flags;
            build.Inputs.NumDescs = 1;
            build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            build.Inputs.pGeometryDescs = &geometries[index];
            build.DestAccelerationStructureData = bottom_levels_[index]->GetGPUVirtualAddress();
            build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress() + scratch_offset;
            list4->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
            scratch_offset += needed;
        }
        // All BLAS writes must complete before postbuild queries or TLAS builds read them.
        const D3D12_RESOURCE_BARRIER all_uav = uav_barrier(nullptr);
        list4->ResourceBarrier(1, &all_uav);
        if (enable_compaction)
        {
            std::vector<D3D12_GPU_VIRTUAL_ADDRESS> addresses(count);
            for (std::size_t index = 0; index < count; ++index) addresses[index] = bottom_levels_[index]->GetGPUVirtualAddress();
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC info{};
            info.DestBuffer = postbuild->GetGPUVirtualAddress();
            info.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
            list4->EmitRaytracingAccelerationStructurePostbuildInfo(&info, checked_uint(count, "BLAS count"), addresses.data());
            const D3D12_RESOURCE_BARRIER to_copy = transition_barrier(postbuild.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                      D3D12_RESOURCE_STATE_COPY_SOURCE);
            list4->ResourceBarrier(1, &to_copy);
            list4->CopyBufferRegion(postbuild_readback.Get(), 0, postbuild.Get(), 0, postbuild_bytes);
        }
    });

    stats_.blas_count = count;
    stats_.blas_bytes = stats_.blas_bytes_before_compaction;
    if (!enable_compaction) return;

    std::vector<UINT64> compacted_sizes(count);
    void* mapped = nullptr;
    const D3D12_RANGE read_range{0, static_cast<SIZE_T>(postbuild_bytes)};
    DAEDALUS_THROW_IF_FAILED(postbuild_readback->Map(0, &read_range, &mapped));
    std::memcpy(compacted_sizes.data(), mapped, static_cast<std::size_t>(postbuild_bytes));
    const D3D12_RANGE written{0, 0};
    postbuild_readback->Unmap(0, &written);

    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> compacted(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        if (compacted_sizes[index] == 0U || compacted_sizes[index] > prebuild[index].ResultDataMaxSizeInBytes)
            throw std::runtime_error("driver reported an invalid compacted BLAS size for primitive " + std::to_string(index));
        compacted[index] = create_acceleration_structure_buffer(device_, compacted_sizes[index], L"Daedalus compacted BLAS");
    }
    context_.execute_immediate([&](ID3D12GraphicsCommandList* command_list)
    {
        const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> list4 = as_list4(command_list);
        for (std::size_t index = 0; index < count; ++index)
            list4->CopyRaytracingAccelerationStructure(compacted[index]->GetGPUVirtualAddress(),
                                                       bottom_levels_[index]->GetGPUVirtualAddress(),
                                                       D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
        const D3D12_RESOURCE_BARRIER all_uav = uav_barrier(nullptr);
        list4->ResourceBarrier(1, &all_uav);
    });
    // execute_immediate proved GPU idle; the uncompacted BLAS may now be released.
    bottom_levels_ = std::move(compacted);
    stats_.blas_bytes = 0;
    for (const auto& blas : bottom_levels_) stats_.blas_bytes += allocation_size(device_, blas.Get());
    stats_.compaction_applied = true;
    std::ostringstream stream;
    stream << "BLAS compaction: " << count << " structures, " << stats_.blas_bytes_before_compaction << " -> "
           << stats_.blas_bytes << " bytes";
    Log::info(stream.str());
}

void AccelerationStructureManager::create_top_level()
{
    const auto instance_count = static_cast<std::uint32_t>(instance_geometry_.size());
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
                   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = instance_count;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
    top_level_ = create_acceleration_structure_buffer(device_, prebuild.ResultDataMaxSizeInBytes, L"Daedalus TLAS");
    const std::uint64_t scratch = std::max(prebuild.ScratchDataSizeInBytes, prebuild.UpdateScratchDataSizeInBytes);
    top_level_scratch_ = create_buffer(device_, align_up_u64(std::max<std::uint64_t>(scratch, kAsAlignment), kAsAlignment, "TLAS scratch"),
                                       D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, L"Daedalus TLAS scratch");
    stats_.tlas_bytes = allocation_size(device_, top_level_.Get());
    stats_.tlas_scratch_bytes = allocation_size(device_, top_level_scratch_.Get());

    const std::uint64_t desc_bytes = std::max<std::uint64_t>(
        checked_multiply_u64(instance_count, sizeof(D3D12_RAYTRACING_INSTANCE_DESC), "TLAS instance descriptors"),
        sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
    for (std::uint32_t frame = 0; frame < D3D12Context::kFrameCount; ++frame)
    {
        instance_descs_[frame] = create_buffer(device_, desc_bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                                               D3D12_RESOURCE_FLAG_NONE, L"Daedalus TLAS instance descriptors");
        void* mapped = nullptr;
        const D3D12_RANGE read_range{0, 0};
        DAEDALUS_THROW_IF_FAILED(instance_descs_[frame]->Map(0, &read_range, &mapped));
        mapped_instance_descs_[frame] = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(mapped);
    }
}

void AccelerationStructureManager::write_instance_descs(std::uint32_t frame_index, std::span<const Mat4> world)
{
    D3D12_RAYTRACING_INSTANCE_DESC* destination = mapped_instance_descs_.at(frame_index);
    for (std::size_t index = 0; index < instance_geometry_.size(); ++index)
    {
        D3D12_RAYTRACING_INSTANCE_DESC desc{};
        // Explicit column-major Mat4 -> row-major 3x4 conversion (never a raw Mat4 memcpy).
        const std::array<float, 12> rows = to_row_major_3x4(world[index]);
        for (std::size_t row = 0; row < 3U; ++row)
            for (std::size_t column = 0; column < 4U; ++column)
                desc.Transform[row][column] = rows[row * 4U + column];
        desc.InstanceID = static_cast<UINT>(index) & 0x00FFFFFFU;
        desc.InstanceMask = policies_[index].mask;
        desc.InstanceContributionToHitGroupIndex = 0;
        desc.Flags = policies_[index].instance_flags;
        desc.AccelerationStructure = bottom_levels_[instance_geometry_[index]]->GetGPUVirtualAddress();
        std::memcpy(destination + index, &desc, sizeof(desc));
    }
}

bool AccelerationStructureManager::record_top_level(ID3D12GraphicsCommandList4* command_list,
                                                    std::uint32_t frame_index,
                                                    std::span<const Mat4> world,
                                                    bool transforms_changed)
{
    if (command_list == nullptr) throw std::invalid_argument("TLAS recording requires ID3D12GraphicsCommandList4");
    if (world.size() != instance_geometry_.size()) throw std::invalid_argument("TLAS world transform count mismatch");
    const bool full_build = !top_level_built_ || force_rebuild_ || updates_since_rebuild_ >= kUpdatesBeforeRebuild;
    if (!full_build && !transforms_changed) return false;

    write_instance_descs(frame_index, world);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    build.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
                         D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    if (!full_build) build.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    build.Inputs.NumDescs = static_cast<UINT>(instance_geometry_.size());
    build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    build.Inputs.InstanceDescs = instance_geometry_.empty() ? 0U : instance_descs_[frame_index]->GetGPUVirtualAddress();
    build.DestAccelerationStructureData = top_level_->GetGPUVirtualAddress();
    build.SourceAccelerationStructureData = full_build ? 0U : top_level_->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData = top_level_scratch_->GetGPUVirtualAddress();
    command_list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    const D3D12_RESOURCE_BARRIER barriers[] = {uav_barrier(top_level_.Get()), uav_barrier(top_level_scratch_.Get())};
    command_list->ResourceBarrier(2, barriers);

    if (full_build)
    {
        top_level_built_ = true;
        force_rebuild_ = false;
        updates_since_rebuild_ = 0;
        ++stats_.tlas_full_builds;
    }
    else
    {
        ++updates_since_rebuild_;
        ++stats_.tlas_updates;
    }
    return true;
}

D3D12_GPU_VIRTUAL_ADDRESS AccelerationStructureManager::top_level_address() const noexcept
{
    return top_level_ != nullptr ? top_level_->GetGPUVirtualAddress() : 0U;
}

std::uint64_t AccelerationStructureManager::committed_allocation_bytes() const noexcept
{
    std::uint64_t total = stats_.blas_bytes + stats_.tlas_bytes + stats_.tlas_scratch_bytes;
    for (const auto& buffer : instance_descs_) total += allocation_size(device_, buffer.Get());
    return total;
}
}
