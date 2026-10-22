#include "graphics/raytracing/RaytracingScene.h"

#include "core/Error.h"
#include "core/Log.h"
#include "graphics/D3D12Helpers.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace daedalus
{
namespace
{
constexpr std::uint32_t kRootView = 0U;
constexpr std::uint32_t kRootLights = 1U;
constexpr std::uint32_t kRootPass = 2U;
constexpr std::uint32_t kRootTlas = 3U;
constexpr std::uint32_t kRootInstances = 4U;
constexpr std::uint32_t kRootGeometries = 5U;
constexpr std::uint32_t kRootMaterials = 6U;
constexpr std::uint32_t kRootEmissive = 7U;
constexpr std::uint32_t kRootTextures = 8U;
constexpr std::uint32_t kRootBuffers = 9U;
constexpr std::uint32_t kRootSamplers = 10U;
constexpr std::uint32_t kRootPassInputs = 11U;
constexpr std::uint32_t kRootPassOutputs = 12U;
constexpr D3D12_RESOURCE_STATES kTableState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

[[nodiscard]] std::uint64_t allocation(ID3D12Device* device, ID3D12Resource* resource) noexcept
{
    if (resource == nullptr) return 0U;
    const D3D12_RESOURCE_DESC description = resource->GetDesc();
    const D3D12_RESOURCE_ALLOCATION_INFO info = device->GetResourceAllocationInfo(0, 1, &description);
    return info.SizeInBytes == std::numeric_limits<std::uint64_t>::max() ? 0U : info.SizeInBytes;
}
}

RaytracingScene::RaytracingScene(D3D12Context& context,
                                 GpuSceneResources& resources,
                                 const CanonicalScene& scene,
                                 std::span<const PreparedRasterDraw> draws,
                                 std::span<const Mat4> initial_world)
    : context_(context), resources_(resources), device_(context.device())
{
    tables_ = prepare_ray_tracing_tables(scene, draws, resources_.layout());
    if (tables_.emissive_budget_exceeded)
        Log::warning("Emissive triangle count exceeds the light-table budget; emissive next-event estimation is disabled "
                     "(BSDF-sampled emission remains, output is unbiased but noisier)");
    acceleration_ = std::make_unique<AccelerationStructureManager>(context_, resources_, tables_, initial_world);
    create_root_signature();
    create_tables();
    std::ostringstream stream;
    stream << "Ray-tracing scene created: instances=" << tables_.instances.size() << " geometries=" << tables_.geometries.size()
           << " materials=" << tables_.materials.size() << " emissive_triangles=" << tables_.emissive_triangles.size()
           << " acceleration=" << acceleration_->stats().to_json();
    Log::info(stream.str());
}

RaytracingScene::~RaytracingScene()
{
    for (std::size_t index = 0; index < instance_staging_.size(); ++index)
    {
        if (instance_staging_[index] != nullptr && mapped_instance_staging_[index] != nullptr) instance_staging_[index]->Unmap(0, nullptr);
        mapped_instance_staging_[index] = nullptr;
    }
    if (pass_constants_ != nullptr && mapped_pass_constants_ != nullptr) pass_constants_->Unmap(0, nullptr);
    mapped_pass_constants_ = nullptr;
}

void RaytracingScene::create_root_signature()
{
    D3D12_DESCRIPTOR_RANGE textures{};
    textures.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textures.NumDescriptors = UINT_MAX; // unbounded (resource binding tier 3 is a DXR-mode requirement)
    textures.BaseShaderRegister = 0;
    textures.RegisterSpace = 1;
    textures.OffsetInDescriptorsFromTableStart = 0;
    D3D12_DESCRIPTOR_RANGE buffers = textures;
    buffers.RegisterSpace = 2;
    D3D12_DESCRIPTOR_RANGE samplers{};
    samplers.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplers.NumDescriptors = UINT_MAX;
    samplers.BaseShaderRegister = 0;
    samplers.RegisterSpace = 1;
    samplers.OffsetInDescriptorsFromTableStart = 0;
    D3D12_DESCRIPTOR_RANGE inputs{};
    inputs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    inputs.NumDescriptors = kPassInputCount;
    inputs.BaseShaderRegister = 5;
    inputs.RegisterSpace = 0;
    inputs.OffsetInDescriptorsFromTableStart = 0;
    D3D12_DESCRIPTOR_RANGE outputs{};
    outputs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    outputs.NumDescriptors = kPassOutputCount;
    outputs.BaseShaderRegister = 0;
    outputs.RegisterSpace = 0;
    outputs.OffsetInDescriptorsFromTableStart = 0;

    std::array<D3D12_ROOT_PARAMETER, 13> parameters{};
    for (std::uint32_t index = 0; index < 3U; ++index)
    {
        parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[index].Descriptor.ShaderRegister = index;
        parameters[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    for (std::uint32_t index = 0; index < 5U; ++index)
    {
        parameters[kRootTlas + index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[kRootTlas + index].Descriptor.ShaderRegister = index;
        parameters[kRootTlas + index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    auto table = [&](std::uint32_t parameter, const D3D12_DESCRIPTOR_RANGE* range)
    {
        parameters[parameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[parameter].DescriptorTable.NumDescriptorRanges = 1;
        parameters[parameter].DescriptorTable.pDescriptorRanges = range;
        parameters[parameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    };
    table(kRootTextures, &textures);
    table(kRootBuffers, &buffers);
    table(kRootSamplers, &samplers);
    table(kRootPassInputs, &inputs);
    table(kRootPassOutputs, &outputs);

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    root_signature_ = daedalus::create_root_signature(device_, description, "DXR global");
}

void RaytracingScene::create_tables()
{
    const auto upload_table = [&](const void* data, std::size_t count, std::size_t stride, std::wstring_view name)
    {
        // Root SRVs need a real buffer even for empty tables; one zeroed element is never indexed
        // because the matching counts in RtPassConstants are zero.
        std::vector<std::byte> fallback;
        const void* source = data;
        std::uint64_t bytes = static_cast<std::uint64_t>(count) * stride;
        if (count == 0U)
        {
            fallback.assign(stride, std::byte{0});
            source = fallback.data();
            bytes = stride;
        }
        return resources_.upload_buffer(source, bytes, kTableState, D3D12_RESOURCE_FLAG_NONE, name);
    };
    geometry_buffer_ = upload_table(tables_.geometries.data(), tables_.geometries.size(), sizeof(RtGeometryData), L"Daedalus RT geometry table");
    material_buffer_ = upload_table(tables_.materials.data(), tables_.materials.size(), sizeof(RtMaterialData), L"Daedalus RT material table");
    emissive_buffer_ = upload_table(tables_.emissive_triangles.data(), tables_.emissive_triangles.size(), sizeof(RtEmissiveTriangle),
                                    L"Daedalus RT emissive triangle table");

    const std::uint64_t instance_bytes = std::max<std::uint64_t>(
        checked_multiply_u64(tables_.instances.size(), sizeof(RtInstanceData), "instance table"), sizeof(RtInstanceData));
    instance_buffer_ = create_buffer(device_, instance_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                                     D3D12_RESOURCE_FLAG_NONE, L"Daedalus RT instance table");
    for (std::uint32_t frame = 0; frame < D3D12Context::kFrameCount; ++frame)
    {
        instance_staging_[frame] = create_buffer(device_, instance_bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                 D3D12_RESOURCE_FLAG_NONE, L"Daedalus RT instance staging");
        const D3D12_RANGE read_range{0, 0};
        DAEDALUS_THROW_IF_FAILED(instance_staging_[frame]->Map(0, &read_range, reinterpret_cast<void**>(&mapped_instance_staging_[frame])));
    }

    pass_constant_stride_ = static_cast<std::size_t>(align_up_u64(sizeof(RtPassConstants), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, "pass constants"));
    const std::uint64_t pass_bytes = checked_multiply_u64(
        pass_constant_stride_, static_cast<std::uint64_t>(RtPassSlot::count) * D3D12Context::kFrameCount, "pass constants");
    pass_constants_ = create_buffer(device_, pass_bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                                    D3D12_RESOURCE_FLAG_NONE, L"Daedalus RT pass constants");
    const D3D12_RANGE read_range{0, 0};
    DAEDALUS_THROW_IF_FAILED(pass_constants_->Map(0, &read_range, reinterpret_cast<void**>(&mapped_pass_constants_)));
}

void RaytracingScene::update(ID3D12GraphicsCommandList4* command_list,
                             std::uint32_t frame_index,
                             std::span<const Mat4> world,
                             std::span<const Mat4> previous_world,
                             bool transforms_changed,
                             GpuTimer& timer)
{
    if (frame_index >= D3D12Context::kFrameCount) throw std::out_of_range("ray-tracing frame index out of range");
    std::vector<RtInstanceData> instances = tables_.instances;
    update_instance_transforms(instances, world, previous_world);
    const bool changed = !instance_buffer_initialized_ || instances.size() != uploaded_instances_.size() ||
                         (!instances.empty() &&
                          std::memcmp(instances.data(), uploaded_instances_.data(), instances.size() * sizeof(RtInstanceData)) != 0);
    if (changed && !instances.empty())
    {
        const std::size_t bytes = instances.size() * sizeof(RtInstanceData);
        std::memcpy(mapped_instance_staging_[frame_index], instances.data(), bytes);
        if (instance_buffer_initialized_)
        {
            const D3D12_RESOURCE_BARRIER to_copy = transition_barrier(instance_buffer_.Get(), kTableState, D3D12_RESOURCE_STATE_COPY_DEST);
            command_list->ResourceBarrier(1, &to_copy);
        }
        command_list->CopyBufferRegion(instance_buffer_.Get(), 0, instance_staging_[frame_index].Get(), 0, bytes);
        const D3D12_RESOURCE_BARRIER to_read = transition_barrier(instance_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, kTableState);
        command_list->ResourceBarrier(1, &to_read);
        instance_buffer_initialized_ = true;
        uploaded_instances_ = std::move(instances);
    }
    else if (!instance_buffer_initialized_)
    {
        const D3D12_RESOURCE_BARRIER to_read = transition_barrier(instance_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, kTableState);
        command_list->ResourceBarrier(1, &to_read);
        instance_buffer_initialized_ = true;
    }
    static_cast<void>(acceleration_->record_top_level(command_list, frame_index, world, transforms_changed));
    timer.mark(command_list, GpuPass::acceleration_structure);
}

void RaytracingScene::write_pass_constants(std::uint32_t frame_index, RtPassSlot slot, const RtPassConstants& constants)
{
    if (frame_index >= D3D12Context::kFrameCount || slot >= RtPassSlot::count) throw std::out_of_range("pass constant slot out of range");
    const std::size_t index = static_cast<std::size_t>(frame_index) * static_cast<std::size_t>(RtPassSlot::count) +
                              static_cast<std::size_t>(slot);
    std::memcpy(mapped_pass_constants_ + index * pass_constant_stride_, &constants, sizeof(constants));
}

void RaytracingScene::bind(ID3D12GraphicsCommandList4* command_list,
                           std::uint32_t frame_index,
                           RtPassSlot slot,
                           D3D12_GPU_VIRTUAL_ADDRESS view_constants,
                           D3D12_GPU_VIRTUAL_ADDRESS light_constants,
                           D3D12_GPU_DESCRIPTOR_HANDLE pass_inputs,
                           D3D12_GPU_DESCRIPTOR_HANDLE pass_outputs) const
{
    const std::size_t index = static_cast<std::size_t>(frame_index) * static_cast<std::size_t>(RtPassSlot::count) +
                              static_cast<std::size_t>(slot);
    ID3D12DescriptorHeap* heaps[] = {resources_.srv_heap(), resources_.sampler_heap()};
    command_list->SetDescriptorHeaps(2, heaps);
    command_list->SetComputeRootSignature(root_signature_.Get());
    command_list->SetComputeRootConstantBufferView(kRootView, view_constants);
    command_list->SetComputeRootConstantBufferView(kRootLights, light_constants);
    command_list->SetComputeRootConstantBufferView(kRootPass, pass_constants_->GetGPUVirtualAddress() + index * pass_constant_stride_);
    command_list->SetComputeRootShaderResourceView(kRootTlas, acceleration_->top_level_address());
    command_list->SetComputeRootShaderResourceView(kRootInstances, instance_buffer_->GetGPUVirtualAddress());
    command_list->SetComputeRootShaderResourceView(kRootGeometries, geometry_buffer_->GetGPUVirtualAddress());
    command_list->SetComputeRootShaderResourceView(kRootMaterials, material_buffer_->GetGPUVirtualAddress());
    command_list->SetComputeRootShaderResourceView(kRootEmissive, emissive_buffer_->GetGPUVirtualAddress());
    command_list->SetComputeRootDescriptorTable(kRootTextures, resources_.srv_gpu(0U));
    command_list->SetComputeRootDescriptorTable(kRootBuffers, resources_.srv_gpu(resources_.layout().raw_base()));
    command_list->SetComputeRootDescriptorTable(kRootSamplers, resources_.sampler_gpu(0U));
    command_list->SetComputeRootDescriptorTable(kRootPassInputs, pass_inputs);
    command_list->SetComputeRootDescriptorTable(kRootPassOutputs, pass_outputs);
}

std::uint32_t RaytracingScene::emissive_triangle_count() const noexcept
{
    return static_cast<std::uint32_t>(tables_.emissive_triangles.size());
}

std::uint64_t RaytracingScene::committed_allocation_bytes() const noexcept
{
    std::uint64_t total = acceleration_->committed_allocation_bytes();
    total += allocation(device_, instance_buffer_.Get());
    for (const auto& staging : instance_staging_) total += allocation(device_, staging.Get());
    total += allocation(device_, geometry_buffer_.Get());
    total += allocation(device_, material_buffer_.Get());
    total += allocation(device_, emissive_buffer_.Get());
    total += allocation(device_, pass_constants_.Get());
    return total;
}
}
