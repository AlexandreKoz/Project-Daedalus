#include "graphics/ComputePass.h"

#include "core/Error.h"

#include <stdexcept>
#include <string>

namespace daedalus
{
Microsoft::WRL::ComPtr<ID3D12RootSignature> create_compute_root_signature(ID3D12Device* device)
{
    D3D12_DESCRIPTOR_RANGE inputs{};
    inputs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    inputs.NumDescriptors = kComputeInputCount;
    inputs.BaseShaderRegister = 0;
    inputs.OffsetInDescriptorsFromTableStart = 0;
    D3D12_DESCRIPTOR_RANGE outputs{};
    outputs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    outputs.NumDescriptors = kComputeOutputCount;
    outputs.BaseShaderRegister = 0;
    outputs.OffsetInDescriptorsFromTableStart = 0;

    std::array<D3D12_ROOT_PARAMETER, 4> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.ShaderRegister = 1;
    parameters[1].Constants.Num32BitValues = 16;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters[2].DescriptorTable.pDescriptorRanges = &inputs;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[3].DescriptorTable.NumDescriptorRanges = 1;
    parameters[3].DescriptorTable.pDescriptorRanges = &outputs;
    parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    std::array<D3D12_STATIC_SAMPLER_DESC, 2> samplers{};
    for (std::uint32_t index = 0; index < samplers.size(); ++index)
    {
        samplers[index].Filter = index == 0U ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[index].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[index].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[index].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[index].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplers[index].MaxLOD = 0.0F;
        samplers[index].ShaderRegister = index;
        samplers[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();
    description.NumStaticSamplers = static_cast<UINT>(samplers.size());
    description.pStaticSamplers = samplers.data();
    return create_root_signature(device, description, "compute");
}

ComputeBindingSet::ComputeBindingSet(GpuSceneResources& resources, std::uint32_t base) : resources_(&resources), base_(base)
{
    clear();
}

void ComputeBindingSet::clear()
{
    if (resources_ == nullptr) throw std::logic_error("compute binding set is not attached to a descriptor heap");
    for (std::uint32_t slot = 0; slot < kComputeInputCount; ++slot) resources_->write_null_texture_srv(base_ + slot);
    for (std::uint32_t slot = 0; slot < kComputeOutputCount; ++slot) resources_->write_null_texture_uav(base_ + kComputeInputCount + slot);
}

void ComputeBindingSet::input(std::uint32_t slot, const GpuTexture& texture)
{
    if (resources_ == nullptr || slot >= kComputeInputCount) throw std::out_of_range("compute input slot out of range");
    resources_->write_texture_srv(base_ + slot, texture.resource.Get(), texture.format);
}

void ComputeBindingSet::input_raw(std::uint32_t slot, ID3D12Resource* resource, DXGI_FORMAT format)
{
    if (resources_ == nullptr || slot >= kComputeInputCount) throw std::out_of_range("compute input slot out of range");
    resources_->write_texture_srv(base_ + slot, resource, format);
}

void ComputeBindingSet::output(std::uint32_t slot, const GpuTexture& texture)
{
    if (resources_ == nullptr || slot >= kComputeOutputCount) throw std::out_of_range("compute output slot out of range");
    resources_->write_texture_uav(base_ + kComputeInputCount + slot, texture.resource.Get(), texture.format);
}

D3D12_GPU_DESCRIPTOR_HANDLE ComputeBindingSet::inputs() const
{
    if (resources_ == nullptr) throw std::logic_error("compute binding set is not attached");
    return resources_->srv_gpu(base_);
}

D3D12_GPU_DESCRIPTOR_HANDLE ComputeBindingSet::outputs() const
{
    if (resources_ == nullptr) throw std::logic_error("compute binding set is not attached");
    return resources_->srv_gpu(base_ + kComputeInputCount);
}

ComputePipeline::ComputePipeline(ID3D12Device* device, ID3D12RootSignature* root_signature,
                                 const std::filesystem::path& shader_directory, std::string_view file_name)
    : root_signature_(root_signature)
{
    const std::vector<std::byte> bytecode = load_shader(shader_directory, file_name);
    D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
    description.pRootSignature = root_signature_;
    description.CS = {bytecode.data(), bytecode.size()};
    const HRESULT result = device->CreateComputePipelineState(&description, IID_PPV_ARGS(&pipeline_));
    if (FAILED(result))
        throw ResultError(static_cast<ResultCode>(result), "CreateComputePipelineState(" + std::string(file_name) + ")");
}

void ComputePipeline::dispatch(ID3D12GraphicsCommandList* command_list,
                               const GpuSceneResources& resources,
                               D3D12_GPU_VIRTUAL_ADDRESS view_constants,
                               const ComputeConstants& constants,
                               const ComputeBindingSet& bindings,
                               std::uint32_t width,
                               std::uint32_t height) const
{
    if (width == 0U || height == 0U) return;
    ID3D12DescriptorHeap* heaps[] = {resources.srv_heap(), resources.sampler_heap()};
    command_list->SetDescriptorHeaps(2, heaps);
    command_list->SetComputeRootSignature(root_signature_);
    command_list->SetPipelineState(pipeline_.Get());
    command_list->SetComputeRootConstantBufferView(0, view_constants);
    command_list->SetComputeRoot32BitConstants(1, 16, constants.values.data(), 0);
    command_list->SetComputeRootDescriptorTable(2, bindings.inputs());
    command_list->SetComputeRootDescriptorTable(3, bindings.outputs());
    command_list->Dispatch((width + kComputeGroupSize - 1U) / kComputeGroupSize, (height + kComputeGroupSize - 1U) / kComputeGroupSize, 1);
}
}
