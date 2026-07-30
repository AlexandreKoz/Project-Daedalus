#include "graphics/TriangleRenderer.h"

#include "core/Error.h"
#include "core/Log.h"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace daedalus
{
namespace
{
[[nodiscard]] std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        throw std::runtime_error("unable to open shader binary: " + path.string());
    }

    const std::streamsize size = file.tellg();
    if (size <= 0)
    {
        throw std::runtime_error("shader binary is empty: " + path.string());
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        throw std::runtime_error("unable to read shader binary: " + path.string());
    }
    return bytes;
}

[[nodiscard]] D3D12_RESOURCE_BARRIER transition_barrier(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) noexcept
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}
}

TriangleRenderer::TriangleRenderer(
    ID3D12Device* device,
    const std::filesystem::path& vertex_shader_path,
    const std::filesystem::path& pixel_shader_path)
{
    if (device == nullptr)
    {
        throw std::invalid_argument("TriangleRenderer requires a valid Direct3D 12 device");
    }

    Log::info("Vertex shader binary: " + vertex_shader_path.string());
    Log::info("Pixel shader binary: " + pixel_shader_path.string());
    create_root_signature(device);
    create_pipeline(device, vertex_shader_path, pixel_shader_path);
    create_vertex_buffer(device);
}

void TriangleRenderer::record(const FrameRecordingContext& frame) const
{
    if (frame.command_list == nullptr || frame.back_buffer == nullptr || frame.width == 0 || frame.height == 0)
    {
        throw std::invalid_argument("TriangleRenderer received an invalid frame context");
    }

    ID3D12GraphicsCommandList* command_list = frame.command_list;
    command_list->SetGraphicsRootSignature(root_signature_.Get());
    command_list->SetPipelineState(pipeline_state_.Get());

    const D3D12_VIEWPORT viewport{
        0.0f,
        0.0f,
        static_cast<float>(frame.width),
        static_cast<float>(frame.height),
        0.0f,
        1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(frame.width), static_cast<LONG>(frame.height)};
    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor);

    const D3D12_RESOURCE_BARRIER to_render_target = transition_barrier(
        frame.back_buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    command_list->ResourceBarrier(1, &to_render_target);

    command_list->OMSetRenderTargets(1, &frame.render_target, FALSE, nullptr);
    constexpr float clear_color[] = {0.035f, 0.055f, 0.09f, 1.0f};
    command_list->ClearRenderTargetView(frame.render_target, clear_color, 0, nullptr);

    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->IASetVertexBuffers(0, 1, &vertex_buffer_view_);
    command_list->DrawInstanced(3, 1, 0, 0);

    const D3D12_RESOURCE_BARRIER to_present = transition_barrier(
        frame.back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    command_list->ResourceBarrier(1, &to_present);
}

void TriangleRenderer::create_root_signature(ID3D12Device* device)
{
    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = 0;
    description.pParameters = nullptr;
    description.NumStaticSamplers = 0;
    description.pStaticSamplers = nullptr;
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT result = D3D12SerializeRootSignature(
        &description, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(result))
    {
        std::string detail;
        if (errors != nullptr && errors->GetBufferPointer() != nullptr)
        {
            detail.assign(
                static_cast<const char*>(errors->GetBufferPointer()),
                static_cast<std::size_t>(errors->GetBufferSize()));
        }
        throw ResultError(
            static_cast<ResultCode>(result),
            detail.empty() ? "D3D12SerializeRootSignature" : "D3D12SerializeRootSignature: " + detail);
    }

    DAEDALUS_THROW_IF_FAILED(device->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(&root_signature_)));
}

void TriangleRenderer::create_pipeline(
    ID3D12Device* device,
    const std::filesystem::path& vertex_shader_path,
    const std::filesystem::path& pixel_shader_path)
{
    const std::vector<std::uint8_t> vertex_shader = read_binary_file(vertex_shader_path);
    const std::vector<std::uint8_t> pixel_shader = read_binary_file(pixel_shader_path);

    constexpr std::array input_elements{
        D3D12_INPUT_ELEMENT_DESC{
            "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        D3D12_INPUT_ELEMENT_DESC{
            "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = root_signature_.Get();
    description.VS = D3D12_SHADER_BYTECODE{vertex_shader.data(), vertex_shader.size()};
    description.PS = D3D12_SHADER_BYTECODE{pixel_shader.data(), pixel_shader.size()};
    description.BlendState.AlphaToCoverageEnable = FALSE;
    description.BlendState.IndependentBlendEnable = FALSE;
    description.BlendState.RenderTarget[0].BlendEnable = FALSE;
    description.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    description.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    description.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    description.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    description.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    description.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    description.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    description.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    description.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    description.SampleMask = std::numeric_limits<UINT>::max();
    description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    description.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    description.RasterizerState.FrontCounterClockwise = FALSE;
    description.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    description.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    description.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    description.RasterizerState.DepthClipEnable = TRUE;
    description.RasterizerState.MultisampleEnable = FALSE;
    description.RasterizerState.AntialiasedLineEnable = FALSE;
    description.RasterizerState.ForcedSampleCount = 0;
    description.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    description.DepthStencilState.DepthEnable = FALSE;
    description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    description.DepthStencilState.StencilEnable = FALSE;
    description.InputLayout = D3D12_INPUT_LAYOUT_DESC{input_elements.data(), static_cast<UINT>(input_elements.size())};
    description.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = D3D12Context::kRenderTargetFormat;
    description.DSVFormat = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1;
    description.SampleDesc.Quality = 0;
    description.NodeMask = 0;
    description.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    DAEDALUS_THROW_IF_FAILED(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&pipeline_state_)));
}

void TriangleRenderer::create_vertex_buffer(ID3D12Device* device)
{
    constexpr std::array vertices{
        Vertex{{0.0f, 0.62f, 0.0f}, {1.0f, 0.15f, 0.12f, 1.0f}},
        Vertex{{0.62f, -0.55f, 0.0f}, {0.12f, 0.95f, 0.32f, 1.0f}},
        Vertex{{-0.62f, -0.55f, 0.0f}, {0.15f, 0.38f, 1.0f, 1.0f}}};

    const UINT64 buffer_size = sizeof(vertices);
    D3D12_HEAP_PROPERTIES heap_properties{};
    heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_properties.CreationNodeMask = 1;
    heap_properties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resource_description{};
    resource_description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_description.Alignment = 0;
    resource_description.Width = buffer_size;
    resource_description.Height = 1;
    resource_description.DepthOrArraySize = 1;
    resource_description.MipLevels = 1;
    resource_description.Format = DXGI_FORMAT_UNKNOWN;
    resource_description.SampleDesc.Count = 1;
    resource_description.SampleDesc.Quality = 0;
    resource_description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_description.Flags = D3D12_RESOURCE_FLAG_NONE;

    DAEDALUS_THROW_IF_FAILED(device->CreateCommittedResource(
        &heap_properties,
        D3D12_HEAP_FLAG_NONE,
        &resource_description,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&vertex_buffer_)));

    void* mapped = nullptr;
    const D3D12_RANGE no_read_range{0, 0};
    DAEDALUS_THROW_IF_FAILED(vertex_buffer_->Map(0, &no_read_range, &mapped));
    std::memcpy(mapped, vertices.data(), sizeof(vertices));
    const D3D12_RANGE written_range{0, static_cast<SIZE_T>(buffer_size)};
    vertex_buffer_->Unmap(0, &written_range);

    vertex_buffer_view_.BufferLocation = vertex_buffer_->GetGPUVirtualAddress();
    vertex_buffer_view_.SizeInBytes = static_cast<UINT>(buffer_size);
    vertex_buffer_view_.StrideInBytes = sizeof(Vertex);
}
}
