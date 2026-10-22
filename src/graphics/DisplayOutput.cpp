#include "graphics/DisplayOutput.h"

#include "assets/FloatImage.h"
#include "core/Error.h"

#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace daedalus
{
namespace
{
constexpr std::uint32_t kRootSource = 0U;
constexpr std::uint32_t kRootConstants = 1U;

struct ToneMapRootConstants
{
    float exposure_ev = 0.0F;
    std::uint32_t diagnostic_mode = 0;
    std::uint32_t resample = 0;
    std::uint32_t padding0 = 0;
    float source_width = 0.0F;
    float source_height = 0.0F;
    float output_width = 0.0F;
    float output_height = 0.0F;
};
static_assert(sizeof(ToneMapRootConstants) == 32);
}

DisplayOutput::DisplayOutput(D3D12Context& context, GpuSceneResources& resources, const std::filesystem::path& shader_directory)
    : context_(context), resources_(resources), device_(context.device())
{
    D3D12_DESCRIPTOR_RANGE source_range{};
    source_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    source_range.NumDescriptors = 1;
    source_range.BaseShaderRegister = 0;
    source_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    std::array<D3D12_ROOT_PARAMETER, 2> parameters{};
    parameters[kRootSource].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[kRootSource].DescriptorTable.NumDescriptorRanges = 1;
    parameters[kRootSource].DescriptorTable.pDescriptorRanges = &source_range;
    parameters[kRootSource].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[kRootConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[kRootConstants].Constants.ShaderRegister = 0;
    parameters[kRootConstants].Constants.Num32BitValues = sizeof(ToneMapRootConstants) / 4U;
    parameters[kRootConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = 0.0F;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();
    description.NumStaticSamplers = 1;
    description.pStaticSamplers = &sampler;
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
    root_signature_ = create_root_signature(device_, description, "tone map");

    const std::vector<std::byte> vertex = load_shader(shader_directory, "ToneMapVS.dxil");
    const std::vector<std::byte> pixel = load_shader(shader_directory, "ToneMapPS.dxil");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC tone{};
    tone.pRootSignature = root_signature_.Get();
    tone.VS = {vertex.data(), vertex.size()};
    tone.PS = {pixel.data(), pixel.size()};
    tone.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    tone.SampleMask = UINT_MAX;
    tone.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    tone.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    tone.RasterizerState.DepthClipEnable = TRUE;
    tone.DepthStencilState.DepthEnable = FALSE;
    tone.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    tone.DepthStencilState.StencilEnable = FALSE;
    tone.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    tone.NumRenderTargets = 1;
    tone.RTVFormats[0] = D3D12Context::kRenderTargetFormat;
    tone.SampleDesc.Count = 1;
    DAEDALUS_THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&tone, IID_PPV_ARGS(&pipeline_)));
}

void DisplayOutput::request_capture(bool capture_ldr, bool capture_linear, std::uint32_t display_width,
                                    std::uint32_t display_height, const GpuTexture& linear_source)
{
    if (!capture_ldr && !capture_linear) throw std::invalid_argument("capture requires LDR and/or linear readback");
    if (capture_ldr_requested_ || capture_linear_requested_ || capture_recorded_)
        throw std::runtime_error("a validation capture is already pending");
    ldr_readback_.Reset();
    linear_readback_.Reset();
    ldr_footprint_ = {};
    linear_footprint_ = {};
    ldr_size_ = 0;
    linear_size_ = 0;
    if (capture_ldr)
    {
        const D3D12_RESOURCE_DESC desc = texture2d_description(display_width, display_height, D3D12Context::kRenderTargetFormat,
                                                               D3D12_RESOURCE_FLAG_NONE);
        UINT rows = 0;
        UINT64 size = 0;
        device_->GetCopyableFootprints(&desc, 0, 1, 0, &ldr_footprint_, &rows, nullptr, &size);
        if (rows != display_height || size == 0U) throw std::runtime_error("invalid LDR capture footprint");
        ldr_readback_ = create_buffer(device_, size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                                      D3D12_RESOURCE_FLAG_NONE, L"Daedalus LDR capture readback");
        ldr_size_ = size;
        capture_width_ = display_width;
        capture_height_ = display_height;
    }
    if (capture_linear)
    {
        if (!linear_source.valid()) throw std::invalid_argument("linear capture requires a source texture");
        if (linear_source.format != DXGI_FORMAT_R16G16B16A16_FLOAT && linear_source.format != DXGI_FORMAT_R32G32B32A32_FLOAT)
            throw std::invalid_argument("linear capture supports only RGBA16F and RGBA32F sources");
        const D3D12_RESOURCE_DESC desc = linear_source.resource->GetDesc();
        UINT rows = 0;
        UINT64 size = 0;
        device_->GetCopyableFootprints(&desc, 0, 1, 0, &linear_footprint_, &rows, nullptr, &size);
        if (rows != linear_source.height || size == 0U) throw std::runtime_error("invalid linear capture footprint");
        linear_readback_ = create_buffer(device_, size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                                         D3D12_RESOURCE_FLAG_NONE, L"Daedalus linear capture readback");
        linear_size_ = size;
        linear_width_ = linear_source.width;
        linear_height_ = linear_source.height;
        linear_format_ = linear_source.format;
    }
    capture_ldr_requested_ = capture_ldr;
    capture_linear_requested_ = capture_linear;
}

void DisplayOutput::record(ID3D12GraphicsCommandList* command_list,
                           const FrameRecordingContext& frame,
                           GpuTexture& linear_source,
                           bool passthrough,
                           float exposure_ev,
                           GpuTimer& timer)
{
    if (!linear_source.valid() || linear_source.srv == kInvalidDescriptor)
        throw std::invalid_argument("display output requires a shader-visible linear source");
    BarrierBatch barriers;
    if (capture_linear_requested_)
    {
        if (linear_source.width != linear_width_ || linear_source.height != linear_height_ || linear_source.format != linear_format_)
            throw std::runtime_error("linear capture source changed between request and record");
        barriers.transition(linear_source, D3D12_RESOURCE_STATE_COPY_SOURCE);
        barriers.flush(command_list);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = linear_source.resource.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = linear_readback_.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = linear_footprint_;
        command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }
    barriers.transition(linear_source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barriers.transition(frame.back_buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers.flush(command_list);

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(frame.width);
    viewport.Height = static_cast<float>(frame.height);
    viewport.MaxDepth = 1.0F;
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(frame.width), static_cast<LONG>(frame.height)};
    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor);
    command_list->OMSetRenderTargets(1, &frame.render_target, FALSE, nullptr);
    command_list->SetGraphicsRootSignature(root_signature_.Get());
    ID3D12DescriptorHeap* heaps[] = {resources_.srv_heap()};
    command_list->SetDescriptorHeaps(1, heaps);
    command_list->SetPipelineState(pipeline_.Get());
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->IASetVertexBuffers(0, 0, nullptr);
    command_list->IASetIndexBuffer(nullptr);
    command_list->SetGraphicsRootDescriptorTable(kRootSource, resources_.srv_gpu(linear_source.srv));
    ToneMapRootConstants constants{};
    constants.exposure_ev = exposure_ev;
    constants.diagnostic_mode = passthrough ? 1U : 0U;
    constants.resample = (linear_source.width != frame.width || linear_source.height != frame.height) ? 1U : 0U;
    constants.source_width = static_cast<float>(linear_source.width);
    constants.source_height = static_cast<float>(linear_source.height);
    constants.output_width = static_cast<float>(frame.width);
    constants.output_height = static_cast<float>(frame.height);
    command_list->SetGraphicsRoot32BitConstants(kRootConstants, sizeof(constants) / 4U, &constants, 0U);
    command_list->DrawInstanced(3, 1, 0, 0);
    timer.mark(command_list, GpuPass::tone_map);

    if (capture_ldr_requested_)
    {
        if (frame.width != capture_width_ || frame.height != capture_height_)
            throw std::runtime_error("display resolution changed between capture request and record");
        barriers.transition(frame.back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        barriers.flush(command_list);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = frame.back_buffer;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = ldr_readback_.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = ldr_footprint_;
        command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        barriers.transition(frame.back_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    }
    else
    {
        barriers.transition(frame.back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    }
    barriers.flush(command_list);
    if (capture_ldr_requested_ || capture_linear_requested_) capture_recorded_ = true;
}

RendererCaptureResult DisplayOutput::finalize_capture()
{
    if (!capture_recorded_) throw std::runtime_error("no completed validation capture is available");
    RendererCaptureResult result;
    result.width = capture_width_;
    result.height = capture_height_;

    if (ldr_readback_ != nullptr)
    {
        const std::size_t tight_row = static_cast<std::size_t>(capture_width_) * 4U;
        result.rgba8.resize(tight_row * capture_height_);
        void* mapped_void = nullptr;
        const D3D12_RANGE read_range{0, static_cast<SIZE_T>(ldr_size_)};
        DAEDALUS_THROW_IF_FAILED(ldr_readback_->Map(0, &read_range, &mapped_void));
        const auto* mapped = static_cast<const std::byte*>(mapped_void);
        for (std::uint32_t row = 0; row < capture_height_; ++row)
            std::memcpy(result.rgba8.data() + static_cast<std::size_t>(row) * tight_row,
                        mapped + static_cast<std::size_t>(ldr_footprint_.Offset) +
                            static_cast<std::size_t>(row) * ldr_footprint_.Footprint.RowPitch,
                        tight_row);
        const D3D12_RANGE written{0, 0};
        ldr_readback_->Unmap(0, &written);
    }

    if (linear_readback_ != nullptr)
    {
        result.linear_width = linear_width_;
        result.linear_height = linear_height_;
        result.linear_format = linear_format_ == DXGI_FORMAT_R32G32B32A32_FLOAT ? "R32G32B32A32_FLOAT" : "R16G16B16A16_FLOAT";
        result.linear_rgb.resize(static_cast<std::size_t>(linear_width_) * linear_height_ * 3U);
        void* mapped_void = nullptr;
        const D3D12_RANGE read_range{0, static_cast<SIZE_T>(linear_size_)};
        DAEDALUS_THROW_IF_FAILED(linear_readback_->Map(0, &read_range, &mapped_void));
        const auto* mapped = static_cast<const std::byte*>(mapped_void);
        for (std::uint32_t row = 0; row < linear_height_; ++row)
        {
            const std::byte* row_bytes = mapped + static_cast<std::size_t>(linear_footprint_.Offset) +
                                         static_cast<std::size_t>(row) * linear_footprint_.Footprint.RowPitch;
            for (std::uint32_t x = 0; x < linear_width_; ++x)
            {
                std::array<float, 4> pixel{};
                if (linear_format_ == DXGI_FORMAT_R32G32B32A32_FLOAT)
                {
                    std::memcpy(pixel.data(), row_bytes + static_cast<std::size_t>(x) * 16U, 16U);
                }
                else
                {
                    std::array<std::uint16_t, 4> halfs{};
                    std::memcpy(halfs.data(), row_bytes + static_cast<std::size_t>(x) * 8U, 8U);
                    for (std::size_t component = 0; component < 4U; ++component) pixel[component] = half_to_float(halfs[component]);
                }
                for (const float component : pixel)
                {
                    if (std::isnan(component)) ++result.hdr_nan_count;
                    else if (std::isinf(component)) ++result.hdr_infinity_count;
                }
                float* destination = result.linear_rgb.data() + (static_cast<std::size_t>(row) * linear_width_ + x) * 3U;
                destination[0] = pixel[0];
                destination[1] = pixel[1];
                destination[2] = pixel[2];
            }
        }
        const D3D12_RANGE written{0, 0};
        linear_readback_->Unmap(0, &written);
    }

    capture_ldr_requested_ = false;
    capture_linear_requested_ = false;
    capture_recorded_ = false;
    ldr_readback_.Reset();
    linear_readback_.Reset();
    return result;
}

std::uint64_t DisplayOutput::committed_allocation_bytes() const noexcept
{
    return 0U; // only transient capture readbacks; reported by their own capture metadata
}
}
