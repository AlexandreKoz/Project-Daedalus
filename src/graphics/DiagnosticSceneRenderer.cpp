#include "graphics/DiagnosticSceneRenderer.h"

#include "core/Error.h"
#include "core/Log.h"
#include "graphics/WicImageDecoder.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace daedalus
{
namespace
{
constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;

[[nodiscard]] std::vector<std::byte> read_binary_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("unable to open shader bytecode: " + path.string());
    const std::streamoff size = input.tellg();
    if (size <= 0) throw std::runtime_error("shader bytecode is empty: " + path.string());
    if (static_cast<std::uintmax_t>(size) > std::numeric_limits<std::size_t>::max())
        throw std::overflow_error("shader bytecode is too large for this process: " + path.string());
    input.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input) throw std::runtime_error("unable to read shader bytecode: " + path.string());
    return bytes;
}

[[nodiscard]] D3D12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE type) noexcept
{
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

[[nodiscard]] D3D12_RESOURCE_DESC buffer_description(std::uint64_t byte_size) noexcept
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Alignment = 0;
    description.Width = byte_size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = D3D12_RESOURCE_FLAG_NONE;
    return description;
}

[[nodiscard]] UINT checked_uint(std::size_t value, std::string_view description)
{
    if (value > static_cast<std::size_t>(std::numeric_limits<UINT>::max()))
        throw std::overflow_error(std::string(description) + " exceeds the D3D12 UINT range");
    return static_cast<UINT>(value);
}

[[nodiscard]] std::uint64_t checked_multiply_u64(std::uint64_t left, std::uint64_t right, std::string_view description)
{
    if (right != 0 && left > std::numeric_limits<std::uint64_t>::max() / right)
        throw std::overflow_error(std::string(description) + " size overflow");
    return left * right;
}

[[nodiscard]] D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource,
                                                D3D12_RESOURCE_STATES before,
                                                D3D12_RESOURCE_STATES after) noexcept
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

[[nodiscard]] D3D12_TEXTURE_ADDRESS_MODE address_mode(WrapMode mode) noexcept
{
    switch (mode)
    {
    case WrapMode::clamp_to_edge: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case WrapMode::mirrored_repeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case WrapMode::repeat: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
}

struct FilterBits
{
    bool min_linear = true;
    bool mag_linear = true;
    bool mip_linear = true;
};

[[nodiscard]] FilterBits filter_bits(const Sampler& sampler) noexcept
{
    FilterBits bits;
    switch (sampler.mag_filter)
    {
    case FilterMode::nearest: bits.mag_linear = false; break;
    default: bits.mag_linear = true; break;
    }
    switch (sampler.min_filter)
    {
    case FilterMode::nearest: bits.min_linear = false; bits.mip_linear = false; break;
    case FilterMode::linear: bits.min_linear = true; bits.mip_linear = false; break;
    case FilterMode::nearest_mipmap_nearest: bits.min_linear = false; bits.mip_linear = false; break;
    case FilterMode::linear_mipmap_nearest: bits.min_linear = true; bits.mip_linear = false; break;
    case FilterMode::nearest_mipmap_linear: bits.min_linear = false; bits.mip_linear = true; break;
    case FilterMode::linear_mipmap_linear: bits.min_linear = true; bits.mip_linear = true; break;
    case FilterMode::unspecified: bits.min_linear = true; bits.mip_linear = true; break;
    }
    return bits;
}

[[nodiscard]] D3D12_FILTER d3d_filter(const Sampler& sampler) noexcept
{
    const FilterBits bits = filter_bits(sampler);
    if (!bits.min_linear && !bits.mag_linear && !bits.mip_linear) return D3D12_FILTER_MIN_MAG_MIP_POINT;
    if (!bits.min_linear && !bits.mag_linear && bits.mip_linear) return D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    if (!bits.min_linear && bits.mag_linear && !bits.mip_linear) return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
    if (!bits.min_linear && bits.mag_linear && bits.mip_linear) return D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR;
    if (bits.min_linear && !bits.mag_linear && !bits.mip_linear) return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    if (bits.min_linear && !bits.mag_linear && bits.mip_linear) return D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
    if (bits.min_linear && bits.mag_linear && !bits.mip_linear) return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
}

[[nodiscard]] std::uint32_t diagnostic_value(DiagnosticMode mode) noexcept
{
    switch (mode)
    {
    case DiagnosticMode::shaded: return 0;
    case DiagnosticMode::normals: return 1;
    case DiagnosticMode::uv: return 2;
    case DiagnosticMode::bounds: return 0;
    }
    return 0;
}
}

DiagnosticSceneRenderer::DiagnosticSceneRenderer(D3D12Context& context,
                                                 const CanonicalScene& scene,
                                                 DiagnosticMode mode,
                                                 const std::filesystem::path& vertex_shader,
                                                 const std::filesystem::path& pixel_shader)
    : context_(context),
      device_(context.device()),
      scene_(scene),
      mode_(mode),
      camera_(scene.selected_scene.valid() && scene.selected_scene.value() < scene.scenes.size()
                  ? scene.scenes[scene.selected_scene.value()].bounds
                  : Aabb{}),
      viewport_width_(context.width()),
      viewport_height_(context.height())
{
    static_assert(sizeof(Vertex) == 72, "canonical Vertex layout changed; update the D3D12 input layout");
    static_assert(sizeof(DrawConstants) <= 256, "draw constants must fit one 256-byte CBV slot");
    if (device_ == nullptr) throw std::invalid_argument("DiagnosticSceneRenderer requires a D3D12 device");

    create_root_signature();
    create_pipeline_states(vertex_shader, pixel_shader);
    create_descriptor_heaps();
    create_textures();
    create_geometry();
    create_draw_items();
    create_bounds_geometry();
    create_constant_buffer();
    create_depth_buffer(viewport_width_, viewport_height_);

    std::ostringstream stream;
    stream << "Diagnostic renderer created: draws=" << draw_items_.size()
           << " primitives=" << primitives_.size() << " textures=" << scene_.textures.size()
           << " mode=" << to_string(mode_);
    Log::info(stream.str());
}

DiagnosticSceneRenderer::~DiagnosticSceneRenderer()
{
    if (constant_buffer_ != nullptr && mapped_constants_ != nullptr)
    {
        constant_buffer_->Unmap(0, nullptr);
        mapped_constants_ = nullptr;
    }
}

void DiagnosticSceneRenderer::create_root_signature()
{
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].RegisterSpace = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &ranges[0];
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters[2].DescriptorTable.pDescriptorRanges = &ranges[1];
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = 3;
    description.pParameters = parameters;
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT result = D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(result))
    {
        const std::string details = errors == nullptr ? std::string{} : std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        throw ResultError(static_cast<ResultCode>(result), "D3D12SerializeRootSignature: " + details);
    }
    DAEDALUS_THROW_IF_FAILED(device_->CreateRootSignature(
        0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root_signature_)));
}

void DiagnosticSceneRenderer::create_pipeline_states(const std::filesystem::path& vertex_shader,
                                                      const std::filesystem::path& pixel_shader)
{
    const std::vector<std::byte> vertex_bytes = read_binary_file(vertex_shader);
    const std::vector<std::byte> pixel_bytes = read_binary_file(pixel_shader);
    const D3D12_INPUT_ELEMENT_DESC input_layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = root_signature_.Get();
    description.VS = {vertex_bytes.data(), vertex_bytes.size()};
    description.PS = {pixel_bytes.data(), pixel_bytes.size()};
    description.BlendState.AlphaToCoverageEnable = FALSE;
    description.BlendState.IndependentBlendEnable = FALSE;
    description.BlendState.RenderTarget[0].BlendEnable = FALSE;
    description.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    description.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    description.SampleMask = UINT_MAX;
    description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    description.RasterizerState.FrontCounterClockwise = TRUE;
    description.RasterizerState.DepthClipEnable = TRUE;
    description.DepthStencilState.DepthEnable = TRUE;
    description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    description.DepthStencilState.StencilEnable = FALSE;
    description.InputLayout = {input_layout, static_cast<UINT>(std::size(input_layout))};
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = D3D12Context::kRenderTargetFormat;
    description.DSVFormat = kDepthFormat;
    description.SampleDesc.Count = 1;
    DAEDALUS_THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&triangle_pipeline_)));

    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    DAEDALUS_THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&line_pipeline_)));
}

void DiagnosticSceneRenderer::create_descriptor_heaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC srv_description{};
    const std::size_t srv_count = std::max<std::size_t>(1, checked_multiply_u64(scene_.textures.size(), 2U, "SRV descriptor") + 1U);
    if (srv_count > D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1)
        throw std::runtime_error("scene requires more shader-visible SRV descriptors than Campaign B supports");
    srv_description.NumDescriptors = checked_uint(srv_count, "SRV descriptor count");
    srv_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    DAEDALUS_THROW_IF_FAILED(device_->CreateDescriptorHeap(&srv_description, IID_PPV_ARGS(&srv_heap_)));
    srv_increment_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC sampler_description{};
    const std::size_t sampler_count = std::max<std::size_t>(1, scene_.samplers.size() + 1U);
    if (sampler_count > D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE)
        throw std::runtime_error("scene requires more shader-visible samplers than D3D12 permits");
    sampler_description.NumDescriptors = checked_uint(sampler_count, "sampler descriptor count");
    sampler_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampler_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    DAEDALUS_THROW_IF_FAILED(device_->CreateDescriptorHeap(&sampler_description, IID_PPV_ARGS(&sampler_heap_)));
    sampler_increment_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    D3D12_DESCRIPTOR_HEAP_DESC dsv_description{};
    dsv_description.NumDescriptors = 1;
    dsv_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    DAEDALUS_THROW_IF_FAILED(device_->CreateDescriptorHeap(&dsv_description, IID_PPV_ARGS(&dsv_heap_)));
}

void DiagnosticSceneRenderer::create_textures()
{
    textures_.reserve(scene_.textures.size() + 1);
    texture_linear_srv_indices_.resize(scene_.textures.size());
    texture_srgb_srv_indices_.resize(scene_.textures.size());
    texture_sampler_indices_.resize(scene_.textures.size());

    const std::array<std::byte, 4> white{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
    textures_.push_back(upload_texture_rgba8(1, 1, white.data()));

    auto create_srv = [&](std::uint32_t descriptor_index, ID3D12Resource* resource, DXGI_FORMAT format)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC description{};
        description.Format = format;
        description.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        description.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        description.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE handle = srv_heap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(descriptor_index) * srv_increment_;
        device_->CreateShaderResourceView(resource, &description, handle);
    };
    create_srv(0, textures_[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM);

    for (std::size_t index = 0; index < scene_.textures.size(); ++index)
    {
        const Texture& texture = scene_.textures[index];
        if (!texture.image.valid() || texture.image.value() >= scene_.images.size())
            throw std::runtime_error("canonical texture references an invalid image");
        const DecodedImage decoded = decode_image_wic(scene_.images[texture.image.value()].encoded_bytes);
        if (decoded.width != scene_.images[texture.image.value()].width || decoded.height != scene_.images[texture.image.value()].height)
            throw std::runtime_error("WIC dimensions disagree with portable image validation");
        textures_.push_back(upload_texture_rgba8(decoded.width, decoded.height, decoded.rgba8.data()));
        const std::uint32_t linear_descriptor = static_cast<std::uint32_t>(index * 2U + 1U);
        const std::uint32_t srgb_descriptor = linear_descriptor + 1U;
        texture_linear_srv_indices_[index] = linear_descriptor;
        texture_srgb_srv_indices_[index] = srgb_descriptor;
        texture_sampler_indices_[index] = texture.sampler.valid() ? texture.sampler.value() + 1U : 0U;
        create_srv(linear_descriptor, textures_.back().Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
        create_srv(srgb_descriptor, textures_.back().Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    }

    auto write_sampler = [&](std::uint32_t descriptor_index, const Sampler& sampler)
    {
        D3D12_SAMPLER_DESC description{};
        description.Filter = d3d_filter(sampler);
        description.AddressU = address_mode(sampler.wrap_s);
        description.AddressV = address_mode(sampler.wrap_t);
        description.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        description.MipLODBias = 0.0F;
        description.MaxAnisotropy = 1;
        description.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        description.MinLOD = 0.0F;
        description.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_CPU_DESCRIPTOR_HANDLE handle = sampler_heap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(descriptor_index) * sampler_increment_;
        device_->CreateSampler(&description, handle);
    };
    write_sampler(0, Sampler{});
    for (std::size_t index = 0; index < scene_.samplers.size(); ++index)
        write_sampler(static_cast<std::uint32_t>(index + 1), scene_.samplers[index]);
}

void DiagnosticSceneRenderer::create_geometry()
{
    primitives_.reserve(scene_.primitives.size());
    for (const Primitive& primitive : scene_.primitives)
    {
        if (primitive.vertices.empty() || primitive.indices.empty())
            throw std::runtime_error("canonical primitive is empty");
        const std::uint64_t vertex_bytes_u64 = checked_multiply_u64(
            static_cast<std::uint64_t>(primitive.vertices.size()), sizeof(Vertex), "vertex buffer");
        const std::uint64_t index_bytes_u64 = checked_multiply_u64(
            static_cast<std::uint64_t>(primitive.indices.size()), sizeof(std::uint32_t), "index buffer");
        if (vertex_bytes_u64 > std::numeric_limits<std::size_t>::max() ||
            index_bytes_u64 > std::numeric_limits<std::size_t>::max())
            throw std::overflow_error("canonical geometry exceeds the process address range");
        const std::size_t vertex_bytes = static_cast<std::size_t>(vertex_bytes_u64);
        const std::size_t index_bytes = static_cast<std::size_t>(index_bytes_u64);

        GpuPrimitive gpu;
        gpu.vertex_buffer = upload_buffer(primitive.vertices.data(), vertex_bytes, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        gpu.index_buffer = upload_buffer(primitive.indices.data(), index_bytes, D3D12_RESOURCE_STATE_INDEX_BUFFER);
        gpu.vertex_view.BufferLocation = gpu.vertex_buffer->GetGPUVirtualAddress();
        gpu.vertex_view.SizeInBytes = checked_uint(vertex_bytes, "vertex buffer view");
        gpu.vertex_view.StrideInBytes = checked_uint(sizeof(Vertex), "vertex stride");
        gpu.index_view.BufferLocation = gpu.index_buffer->GetGPUVirtualAddress();
        gpu.index_view.SizeInBytes = checked_uint(index_bytes, "index buffer view");
        gpu.index_view.Format = DXGI_FORMAT_R32_UINT;
        gpu.index_count = checked_uint(primitive.indices.size(), "index count");
        gpu.material = primitive.material;
        gpu.has_texture_coordinates = primitive.has_texcoord0;
        gpu.has_vertex_colors = primitive.has_colors;
        primitives_.push_back(std::move(gpu));
    }
}

void DiagnosticSceneRenderer::create_draw_items()
{
    if (!scene_.selected_scene.valid() || scene_.selected_scene.value() >= scene_.scenes.size())
        throw std::runtime_error("canonical scene has no valid selected scene");
    std::function<void(NodeId)> visit = [&](NodeId id)
    {
        if (!id.valid() || id.value() >= scene_.nodes.size()) return;
        const Node& node = scene_.nodes[id.value()];
        if (node.mesh.valid() && node.mesh.value() < scene_.meshes.size())
        {
            for (const PrimitiveId primitive : scene_.meshes[node.mesh.value()].primitives)
            {
                if (!primitive.valid() || primitive.value() >= primitives_.size())
                    throw std::runtime_error("canonical mesh references an invalid primitive");
                draw_items_.push_back({primitive.value(), node.world_transform});
            }
        }
        for (const NodeId child : node.children) visit(child);
    };
    for (const NodeId root : scene_.scenes[scene_.selected_scene.value()].roots) visit(root);
}

void DiagnosticSceneRenderer::create_bounds_geometry()
{
    if (!scene_.selected_scene.valid() || scene_.selected_scene.value() >= scene_.scenes.size()) return;
    const Aabb& bounds = scene_.scenes[scene_.selected_scene.value()].bounds;
    if (bounds.empty()) return;
    const Vec3 min = bounds.minimum;
    const Vec3 max = bounds.maximum;
    const std::array<Vec3, 8> corners{{
        {min.x,min.y,min.z},{max.x,min.y,min.z},{max.x,max.y,min.z},{min.x,max.y,min.z},
        {min.x,min.y,max.z},{max.x,min.y,max.z},{max.x,max.y,max.z},{min.x,max.y,max.z}}};
    constexpr std::array<std::uint32_t, 24> edges{
        0,1,1,2,2,3,3,0, 4,5,5,6,6,7,7,4, 0,4,1,5,2,6,3,7};
    std::array<Vertex, 24> vertices{};
    for (std::size_t index = 0; index < edges.size(); ++index)
    {
        vertices[index].position = corners[edges[index]];
        vertices[index].normal = {0.0F, 0.0F, 1.0F};
        vertices[index].color = {1.0F, 0.8F, 0.1F, 1.0F};
    }
    bounds_vertex_buffer_ = upload_buffer(vertices.data(), sizeof(vertices), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    bounds_vertex_view_.BufferLocation = bounds_vertex_buffer_->GetGPUVirtualAddress();
    bounds_vertex_view_.SizeInBytes = checked_uint(sizeof(vertices), "bounds vertex buffer view");
    bounds_vertex_view_.StrideInBytes = checked_uint(sizeof(Vertex), "bounds vertex stride");
    bounds_vertex_count_ = static_cast<std::uint32_t>(vertices.size());
}

void DiagnosticSceneRenderer::create_constant_buffer()
{
    draw_slots_per_frame_ = std::max<std::size_t>(1, draw_items_.size() + 1);
    const std::uint64_t frame_bytes = checked_multiply_u64(draw_slots_per_frame_, constant_stride_, "constant buffer frame partition");
    const std::uint64_t byte_size = checked_multiply_u64(frame_bytes, D3D12Context::kFrameCount, "constant buffer");
    const D3D12_HEAP_PROPERTIES upload = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC description = buffer_description(byte_size);
    DAEDALUS_THROW_IF_FAILED(device_->CreateCommittedResource(
        &upload, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constant_buffer_)));
    D3D12_RANGE read_range{0, 0};
    DAEDALUS_THROW_IF_FAILED(constant_buffer_->Map(0, &read_range, reinterpret_cast<void**>(&mapped_constants_)));
}

void DiagnosticSceneRenderer::create_depth_buffer(std::uint32_t width, std::uint32_t height)
{
    depth_buffer_.Reset();
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = kDepthFormat;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = kDepthFormat;
    clear.DepthStencil.Depth = 1.0F;
    const D3D12_HEAP_PROPERTIES heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    DAEDALUS_THROW_IF_FAILED(device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth_buffer_)));
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = kDepthFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(depth_buffer_.Get(), &dsv, dsv_heap_->GetCPUDescriptorHandleForHeapStart());
}

Microsoft::WRL::ComPtr<ID3D12Resource> DiagnosticSceneRenderer::upload_buffer(
    const void* data,
    std::size_t byte_size,
    D3D12_RESOURCE_STATES final_state)
{
    if (data == nullptr || byte_size == 0) throw std::invalid_argument("upload_buffer requires nonempty data");
    Microsoft::WRL::ComPtr<ID3D12Resource> destination;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    const D3D12_RESOURCE_DESC description = buffer_description(byte_size);
    const D3D12_HEAP_PROPERTIES default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_HEAP_PROPERTIES upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
    DAEDALUS_THROW_IF_FAILED(device_->CreateCommittedResource(
        &default_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&destination)));
    DAEDALUS_THROW_IF_FAILED(device_->CreateCommittedResource(
        &upload_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)));
    void* mapped = nullptr;
    D3D12_RANGE read_range{0, 0};
    DAEDALUS_THROW_IF_FAILED(upload->Map(0, &read_range, &mapped));
    std::memcpy(mapped, data, byte_size);
    upload->Unmap(0, nullptr);
    context_.execute_immediate([&](ID3D12GraphicsCommandList* command_list)
    {
        command_list->CopyBufferRegion(destination.Get(), 0, upload.Get(), 0, byte_size);
        const D3D12_RESOURCE_BARRIER barrier = transition(destination.Get(), D3D12_RESOURCE_STATE_COPY_DEST, final_state);
        command_list->ResourceBarrier(1, &barrier);
    });
    return destination;
}

Microsoft::WRL::ComPtr<ID3D12Resource> DiagnosticSceneRenderer::upload_texture_rgba8(
    std::uint32_t width,
    std::uint32_t height,
    const std::byte* rgba8)
{
    if (width == 0 || height == 0 || rgba8 == nullptr) throw std::invalid_argument("texture upload requires nonempty RGBA8 data");
    D3D12_RESOURCE_DESC texture_description{};
    texture_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_description.Width = width;
    texture_description.Height = height;
    texture_description.DepthOrArraySize = 1;
    texture_description.MipLevels = 1;
    // Typeless storage permits both linear UNORM and sRGB SRVs for per-material-slot interpretation.
    texture_description.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    texture_description.SampleDesc.Count = 1;
    texture_description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    const D3D12_HEAP_PROPERTIES default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    DAEDALUS_THROW_IF_FAILED(device_->CreateCommittedResource(
        &default_heap, D3D12_HEAP_FLAG_NONE, &texture_description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)));

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 upload_size = 0;
    device_->GetCopyableFootprints(&texture_description, 0, 1, 0, &footprint, &rows, nullptr, &upload_size);
    const std::size_t source_pitch = static_cast<std::size_t>(width) * 4U;
    if (rows != height || footprint.Footprint.RowPitch < source_pitch || upload_size == 0)
        throw std::runtime_error("D3D12 returned an invalid RGBA8 copy footprint");
    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    const D3D12_HEAP_PROPERTIES upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC upload_description = buffer_description(upload_size);
    DAEDALUS_THROW_IF_FAILED(device_->CreateCommittedResource(
        &upload_heap, D3D12_HEAP_FLAG_NONE, &upload_description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)));
    std::byte* mapped = nullptr;
    D3D12_RANGE read_range{0, 0};
    DAEDALUS_THROW_IF_FAILED(upload->Map(0, &read_range, reinterpret_cast<void**>(&mapped)));
    for (UINT row = 0; row < rows; ++row)
    {
        std::memcpy(mapped + static_cast<std::size_t>(footprint.Offset) + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                    rgba8 + static_cast<std::size_t>(row) * source_pitch,
                    source_pitch);
    }
    upload->Unmap(0, nullptr);

    context_.execute_immediate([&](ID3D12GraphicsCommandList* command_list)
    {
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        const D3D12_RESOURCE_BARRIER barrier = transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        command_list->ResourceBarrier(1, &barrier);
    });
    return texture;
}

void DiagnosticSceneRenderer::write_constants(std::size_t index, const DrawConstants& constants)
{
    std::memcpy(mapped_constants_ + index * constant_stride_, &constants, sizeof(constants));
}

D3D12_GPU_DESCRIPTOR_HANDLE DiagnosticSceneRenderer::srv_gpu_handle(std::uint32_t index) const noexcept
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = srv_heap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * srv_increment_;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DiagnosticSceneRenderer::sampler_gpu_handle(std::uint32_t index) const noexcept
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = sampler_heap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * sampler_increment_;
    return handle;
}

void DiagnosticSceneRenderer::record(const FrameRecordingContext& frame)
{
    ID3D12GraphicsCommandList* command_list = frame.command_list;
    const D3D12_RESOURCE_BARRIER begin_barrier = transition(frame.back_buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    command_list->ResourceBarrier(1, &begin_barrier);

    const float clear_color[4] = {0.025F, 0.035F, 0.055F, 1.0F};
    command_list->ClearRenderTargetView(frame.render_target, clear_color, 0, nullptr);
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    command_list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);
    command_list->OMSetRenderTargets(1, &frame.render_target, FALSE, &dsv);

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(frame.width);
    viewport.Height = static_cast<float>(frame.height);
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(frame.width), static_cast<LONG>(frame.height)};
    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor);
    command_list->SetGraphicsRootSignature(root_signature_.Get());
    ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get(), sampler_heap_.Get()};
    command_list->SetDescriptorHeaps(2, heaps);
    command_list->SetPipelineState(triangle_pipeline_.Get());
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    if (frame.frame_index >= D3D12Context::kFrameCount)
        throw std::runtime_error("frame index exceeds constant-buffer frame partition count");
    const std::size_t frame_slot_base = static_cast<std::size_t>(frame.frame_index) * draw_slots_per_frame_;
    const float aspect = frame.height == 0 ? 1.0F : static_cast<float>(frame.width) / static_cast<float>(frame.height);
    const Mat4 view_projection = camera_.view_projection_matrix(aspect);
    for (std::size_t index = 0; index < draw_items_.size(); ++index)
    {
        const DrawItem& draw = draw_items_[index];
        const GpuPrimitive& primitive = primitives_[draw.primitive_index];
        DrawConstants constants{};
        constants.world = draw.world;
        constants.world_view_projection = multiply(view_projection, draw.world);
        bool invertible = false;
        constants.normal_matrix = inverse_transpose(draw.world, &invertible);
        if (!invertible) constants.normal_matrix = identity_matrix();
        constants.base_color_factor = {1.0F, 1.0F, 1.0F, 1.0F};
        constants.diagnostic_mode = diagnostic_value(mode_);
        constants.use_vertex_color = primitive.has_vertex_colors ? 1U : 0U;
        std::uint32_t srv_index = 0;
        std::uint32_t sampler_index = 0;
        if (primitive.material.valid() && primitive.material.value() < scene_.materials.size())
        {
            const Material& material = scene_.materials[primitive.material.value()];
            constants.base_color_factor = material.base_color_factor;
            if (material.base_color_texture.has_value() && primitive.has_texture_coordinates)
            {
                const std::uint32_t texture_index = material.base_color_texture->texture.value();
                if (texture_index < texture_srgb_srv_indices_.size())
                {
                    srv_index = texture_srgb_srv_indices_[texture_index];
                    sampler_index = texture_sampler_indices_[texture_index];
                    constants.has_texture = 1;
                }
            }
        }
        const std::size_t slot = frame_slot_base + index;
        write_constants(slot, constants);
        command_list->SetGraphicsRootConstantBufferView(0, constant_buffer_->GetGPUVirtualAddress() + slot * constant_stride_);
        command_list->SetGraphicsRootDescriptorTable(1, srv_gpu_handle(srv_index));
        command_list->SetGraphicsRootDescriptorTable(2, sampler_gpu_handle(sampler_index));
        command_list->IASetVertexBuffers(0, 1, &primitive.vertex_view);
        command_list->IASetIndexBuffer(&primitive.index_view);
        command_list->DrawIndexedInstanced(primitive.index_count, 1, 0, 0, 0);
    }

    if (mode_ == DiagnosticMode::bounds && bounds_vertex_count_ != 0)
    {
        const std::size_t slot = frame_slot_base + draw_items_.size();
        DrawConstants constants{};
        constants.world = identity_matrix();
        constants.world_view_projection = view_projection;
        constants.normal_matrix = identity_matrix();
        constants.base_color_factor = {1.0F, 0.8F, 0.1F, 1.0F};
        constants.diagnostic_mode = 3;
        write_constants(slot, constants);
        command_list->SetPipelineState(line_pipeline_.Get());
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        command_list->SetGraphicsRootConstantBufferView(0, constant_buffer_->GetGPUVirtualAddress() + slot * constant_stride_);
        command_list->SetGraphicsRootDescriptorTable(1, srv_gpu_handle(0));
        command_list->SetGraphicsRootDescriptorTable(2, sampler_gpu_handle(0));
        command_list->IASetVertexBuffers(0, 1, &bounds_vertex_view_);
        command_list->IASetIndexBuffer(nullptr);
        command_list->DrawInstanced(bounds_vertex_count_, 1, 0, 0);
    }

    const D3D12_RESOURCE_BARRIER end_barrier = transition(frame.back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    command_list->ResourceBarrier(1, &end_barrier);
}

void DiagnosticSceneRenderer::resize(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0) return;
    viewport_width_ = width;
    viewport_height_ = height;
    create_depth_buffer(width, height);
}

void DiagnosticSceneRenderer::apply_input(const OrbitInput& input)
{
    camera_.apply_input(input, viewport_width_, viewport_height_);
}
}
