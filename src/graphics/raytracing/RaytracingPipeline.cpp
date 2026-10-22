#include "graphics/raytracing/RaytracingPipeline.h"

#include "core/Error.h"
#include "graphics/D3D12Helpers.h"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace daedalus
{
static_assert(kShaderIdentifierBytes == D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
static_assert(kShaderRecordAlignment == D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
static_assert(kShaderTableAlignment == D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
static_assert(kMaxShaderRecordStride == D3D12_RAYTRACING_MAX_SHADER_RECORD_STRIDE);

namespace
{
[[nodiscard]] std::string narrow(std::wstring_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value) result.push_back(character < 0x80 ? static_cast<char>(character) : '?');
    return result;
}
}

RaytracingPipeline::RaytracingPipeline(ID3D12Device5* device,
                                       GpuSceneResources& resources,
                                       const RaytracingPipelineDescription& description,
                                       std::wstring_view debug_name)
    : device_(device)
{
    if (device_ == nullptr) throw std::invalid_argument("ray-tracing pipelines require ID3D12Device5");
    if (description.library.empty() || description.ray_generation.empty() || description.miss.empty() ||
        description.hit_groups.empty() || description.global_root_signature == nullptr)
        throw std::invalid_argument("incomplete ray-tracing pipeline description");
    if (description.max_payload_bytes == 0U || description.max_payload_bytes % 4U != 0U ||
        description.max_attribute_bytes > D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES ||
        description.max_recursion_depth == 0U || description.max_recursion_depth > D3D12_RAYTRACING_MAX_DECLARABLE_TRACE_RECURSION_DEPTH)
        throw std::invalid_argument("invalid ray-tracing shader/pipeline configuration");

    // Every export the library must provide, with stable storage for the export descriptors.
    std::vector<std::wstring> export_names;
    export_names.push_back(description.ray_generation);
    for (const std::wstring& miss : description.miss) export_names.push_back(miss);
    for (const RaytracingHitGroup& group : description.hit_groups)
    {
        if (group.name.empty() || (group.closest_hit.empty() && group.any_hit.empty()))
            throw std::invalid_argument("hit groups need a name and at least one hit shader");
        if (!group.closest_hit.empty()) export_names.push_back(group.closest_hit);
        if (!group.any_hit.empty()) export_names.push_back(group.any_hit);
    }
    // De-duplicate (hit groups may share a shader).
    std::vector<std::wstring> unique_exports;
    for (const std::wstring& name : export_names)
    {
        bool present = false;
        for (const std::wstring& existing : unique_exports) present = present || existing == name;
        if (!present) unique_exports.push_back(name);
    }
    std::vector<D3D12_EXPORT_DESC> exports(unique_exports.size());
    for (std::size_t index = 0; index < unique_exports.size(); ++index)
    {
        exports[index].Name = unique_exports[index].c_str();
        exports[index].ExportToRename = nullptr;
        exports[index].Flags = D3D12_EXPORT_FLAG_NONE;
    }

    D3D12_DXIL_LIBRARY_DESC library{};
    library.DXILLibrary.pShaderBytecode = description.library.data();
    library.DXILLibrary.BytecodeLength = description.library.size();
    library.NumExports = checked_uint(exports.size(), "DXIL library export count");
    library.pExports = exports.data();

    std::vector<D3D12_HIT_GROUP_DESC> hit_groups(description.hit_groups.size());
    for (std::size_t index = 0; index < hit_groups.size(); ++index)
    {
        const RaytracingHitGroup& group = description.hit_groups[index];
        hit_groups[index].HitGroupExport = group.name.c_str();
        hit_groups[index].Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hit_groups[index].ClosestHitShaderImport = group.closest_hit.empty() ? nullptr : group.closest_hit.c_str();
        hit_groups[index].AnyHitShaderImport = group.any_hit.empty() ? nullptr : group.any_hit.c_str();
        hit_groups[index].IntersectionShaderImport = nullptr;
    }

    D3D12_RAYTRACING_SHADER_CONFIG shader_config{};
    shader_config.MaxPayloadSizeInBytes = description.max_payload_bytes;
    shader_config.MaxAttributeSizeInBytes = description.max_attribute_bytes;
    D3D12_GLOBAL_ROOT_SIGNATURE global_root{};
    global_root.pGlobalRootSignature = description.global_root_signature;
    D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{};
    pipeline_config.MaxTraceRecursionDepth = description.max_recursion_depth;

    // Subobjects without explicit associations apply as defaults to every export in the state object.
    std::vector<D3D12_STATE_SUBOBJECT> subobjects;
    subobjects.reserve(4U + hit_groups.size());
    subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &library});
    for (const D3D12_HIT_GROUP_DESC& group : hit_groups) subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &group});
    subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shader_config});
    subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &global_root});
    subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline_config});

    D3D12_STATE_OBJECT_DESC state_object{};
    state_object.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    state_object.NumSubobjects = checked_uint(subobjects.size(), "state subobject count");
    state_object.pSubobjects = subobjects.data();
    const HRESULT created = device_->CreateStateObject(&state_object, IID_PPV_ARGS(&state_object_));
    if (FAILED(created))
        throw ResultError(static_cast<ResultCode>(created), "ID3D12Device5::CreateStateObject(" + narrow(debug_name) +
                                                                  "); enable the D3D12 debug layer for subobject diagnostics");
    set_debug_name(state_object_.Get(), debug_name);

    Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> properties;
    DAEDALUS_THROW_IF_FAILED(state_object_.As(&properties));
    auto identifier = [&](const std::wstring& name)
    {
        const void* value = properties->GetShaderIdentifier(name.c_str());
        if (value == nullptr) throw std::runtime_error("ray-tracing state object has no shader identifier for export " + narrow(name));
        return value;
    };

    layout_ = compute_shader_table_layout(1U, checked_uint(description.miss.size(), "miss count"),
                                          checked_uint(description.hit_groups.size(), "hit-group count"), 0U);
    std::vector<std::byte> table(static_cast<std::size_t>(layout_.total_bytes), std::byte{0});
    auto write_record = [&](const ShaderTableSection& section, std::size_t record, const void* shader_identifier)
    {
        const std::uint64_t offset = section.offset + section.stride * record;
        if (offset + kShaderIdentifierBytes > layout_.total_bytes) throw std::logic_error("shader record exceeds the shader table");
        std::memcpy(table.data() + offset, shader_identifier, kShaderIdentifierBytes);
    };
    write_record(layout_.ray_generation, 0U, identifier(description.ray_generation));
    for (std::size_t index = 0; index < description.miss.size(); ++index)
        write_record(layout_.miss, index, identifier(description.miss[index]));
    for (std::size_t index = 0; index < description.hit_groups.size(); ++index)
        write_record(layout_.hit_group, index, identifier(description.hit_groups[index].name));

    // Default-heap copy in NON_PIXEL_SHADER_RESOURCE (the state DispatchRays reads shader tables in).
    shader_table_ = resources.upload_buffer(table.data(), table.size(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                            D3D12_RESOURCE_FLAG_NONE, L"Daedalus shader binding table");
    if (shader_table_->GetGPUVirtualAddress() % kShaderTableAlignment != 0U)
        throw std::runtime_error("shader binding table base address is not 64-byte aligned");
}

void RaytracingPipeline::dispatch(ID3D12GraphicsCommandList4* command_list, std::uint32_t width, std::uint32_t height) const
{
    if (command_list == nullptr) throw std::invalid_argument("DispatchRays requires ID3D12GraphicsCommandList4");
    if (width == 0U || height == 0U) return;
    const D3D12_GPU_VIRTUAL_ADDRESS base = shader_table_->GetGPUVirtualAddress();
    D3D12_DISPATCH_RAYS_DESC dispatch{};
    dispatch.RayGenerationShaderRecord.StartAddress = base + layout_.ray_generation.offset;
    dispatch.RayGenerationShaderRecord.SizeInBytes = layout_.ray_generation.stride;
    dispatch.MissShaderTable.StartAddress = base + layout_.miss.offset;
    dispatch.MissShaderTable.SizeInBytes = layout_.miss.size;
    dispatch.MissShaderTable.StrideInBytes = layout_.miss.stride;
    dispatch.HitGroupTable.StartAddress = base + layout_.hit_group.offset;
    dispatch.HitGroupTable.SizeInBytes = layout_.hit_group.size;
    dispatch.HitGroupTable.StrideInBytes = layout_.hit_group.stride;
    dispatch.Width = width;
    dispatch.Height = height;
    dispatch.Depth = 1;
    command_list->SetPipelineState1(state_object_.Get());
    command_list->DispatchRays(&dispatch);
}

std::uint64_t RaytracingPipeline::committed_allocation_bytes() const noexcept
{
    if (shader_table_ == nullptr) return 0U;
    const D3D12_RESOURCE_DESC description = shader_table_->GetDesc();
    const D3D12_RESOURCE_ALLOCATION_INFO info = device_->GetResourceAllocationInfo(0, 1, &description);
    return info.SizeInBytes == std::numeric_limits<std::uint64_t>::max() ? 0U : info.SizeInBytes;
}
}
