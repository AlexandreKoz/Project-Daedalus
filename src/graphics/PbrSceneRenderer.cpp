#include "graphics/PbrSceneRenderer.h"

#include "core/Error.h"
#include "core/Log.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace daedalus
{
namespace
{
constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;
constexpr DXGI_FORMAT kHdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr std::uint32_t kWhiteLinearSrv = 0U;
constexpr std::uint32_t kWhiteSrgbSrv = 1U;
constexpr std::uint32_t kTextureSrvBase = 2U;
constexpr std::uint32_t kRootFrameConstants = 0U;
constexpr std::uint32_t kRootDrawConstants = 1U;
constexpr std::uint32_t kRootLightConstants = 2U;
constexpr std::uint32_t kRootSrvBase = 3U;
constexpr std::uint32_t kRootSamplerBase = 8U;
constexpr std::uint32_t kToneRootSrv = 0U;
constexpr std::uint32_t kToneRootConstants = 1U;
constexpr std::uint32_t kRasterFlagBoundsOverlay = 1U << 8U;

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
    description.Width = byte_size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
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
    if (right != 0U && left > std::numeric_limits<std::uint64_t>::max() / right)
        throw std::overflow_error(std::string(description) + " size overflow");
    return left * right;
}

[[nodiscard]] std::uint64_t checked_add_u64(std::uint64_t left, std::uint64_t right, std::string_view description)
{
    if (left > std::numeric_limits<std::uint64_t>::max() - right)
        throw std::overflow_error(std::string(description) + " size overflow");
    return left + right;
}

[[nodiscard]] std::size_t align_constant_buffer_size(std::size_t value)
{
    constexpr std::size_t alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    if (value > std::numeric_limits<std::size_t>::max() - (alignment - 1U))
        throw std::overflow_error("constant-buffer alignment overflow");
    return (value + alignment - 1U) & ~(alignment - 1U);
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
};

[[nodiscard]] FilterBits filter_bits(const Sampler& sampler) noexcept
{
    FilterBits bits;
    bits.mag_linear = sampler.mag_filter != FilterMode::nearest;
    switch (sampler.min_filter)
    {
    case FilterMode::nearest:
    case FilterMode::nearest_mipmap_nearest:
    case FilterMode::nearest_mipmap_linear:
        bits.min_linear = false;
        break;
    case FilterMode::linear:
    case FilterMode::linear_mipmap_nearest:
    case FilterMode::linear_mipmap_linear:
    case FilterMode::unspecified:
        bits.min_linear = true;
        break;
    }
    return bits;
}

[[nodiscard]] D3D12_FILTER d3d_filter(const Sampler& sampler) noexcept
{
    const FilterBits bits = filter_bits(sampler);
    if (!bits.min_linear && !bits.mag_linear) return D3D12_FILTER_MIN_MAG_MIP_POINT;
    if (!bits.min_linear && bits.mag_linear) return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
    if (bits.min_linear && !bits.mag_linear) return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
}

[[nodiscard]] std::uint32_t shader_diagnostic_value(DiagnosticMode mode) noexcept
{
    switch (mode)
    {
    case DiagnosticMode::shaded: return 0U;
    case DiagnosticMode::normals: return 1U;
    case DiagnosticMode::uv: return 2U;
    case DiagnosticMode::tangents: return 3U;
    case DiagnosticMode::bounds: return 0U;
    case DiagnosticMode::base_color: return 5U;
    case DiagnosticMode::metallic: return 6U;
    case DiagnosticMode::roughness: return 7U;
    case DiagnosticMode::emissive: return 8U;
    case DiagnosticMode::material_id: return 9U;
    }
    return 0U;
}

[[nodiscard]] std::uint32_t alpha_mode_value(AlphaMode mode) noexcept
{
    switch (mode)
    {
    case AlphaMode::opaque: return 0U;
    case AlphaMode::mask: return 1U;
    case AlphaMode::blend: return 2U;
    }
    return 0U;
}

[[nodiscard]] D3D12_BLEND_DESC blend_description(bool enabled) noexcept
{
    D3D12_BLEND_DESC description{};
    description.AlphaToCoverageEnable = FALSE;
    description.IndependentBlendEnable = FALSE;
    auto& target = description.RenderTarget[0];
    target.BlendEnable = enabled ? TRUE : FALSE;
    target.LogicOpEnable = FALSE;
    target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D12_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D12_BLEND_ONE;
    target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    target.LogicOp = D3D12_LOGIC_OP_NOOP;
    target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return description;
}
}

PbrSceneRenderer::PbrSceneRenderer(D3D12Context& context,
                                   const CanonicalScene& scene,
                                   DiagnosticMode mode,
                                   float exposure_ev,
                                   const std::filesystem::path& raster_vertex_shader,
                                   const std::filesystem::path& raster_pixel_shader,
                                   const std::filesystem::path& tone_vertex_shader,
                                   const std::filesystem::path& tone_pixel_shader)
    : context_(context),
      device_(context.device()),
      scene_(scene),
      mode_(mode),
      exposure_ev_(exposure_ev),
      camera_(scene.selected_scene.valid() && scene.selected_scene.value() < scene.scenes.size()
                  ? scene.scenes[scene.selected_scene.value()].bounds
                  : Aabb{}),
      viewport_width_(context.width()),
      viewport_height_(context.height())
{
    static_assert(sizeof(Vertex) == 72, "canonical Vertex layout changed; update the D3D12 input layout");
    if (device_ == nullptr) throw std::invalid_argument("PbrSceneRenderer requires a D3D12 device");
    if (!std::isfinite(exposure_ev_) || exposure_ev_ < -24.0F || exposure_ev_ > 24.0F)
        throw std::invalid_argument("PBR exposure must be finite and within [-24, +24] EV");

    create_root_signatures();
    create_pipeline_states(raster_vertex_shader, raster_pixel_shader, tone_vertex_shader, tone_pixel_shader);
    create_descriptor_heaps();
    create_textures_and_samplers();
    create_geometry();
    create_draw_items();
    create_bounds_geometry();
    create_constant_buffers();
    create_depth_buffer(viewport_width_, viewport_height_);
    create_hdr_target(viewport_width_, viewport_height_);

    std::ostringstream stream;
    stream << "Forward PBR renderer created: draws=" << draw_items_.size()
           << " primitives=" << primitives_.size()
           << " textures=" << scene_.textures.size()
           << " punctual_lights=" << lights_.size()
           << " exposure_ev=" << exposure_ev_
           << " diagnostic=" << to_string(mode_);
    Log::info(stream.str());
}

PbrSceneRenderer::~PbrSceneRenderer()
{
    if (frame_constant_buffer_ != nullptr && mapped_frame_constants_ != nullptr)
    {
        frame_constant_buffer_->Unmap(0, nullptr);
        mapped_frame_constants_ = nullptr;
    }
    if (light_constant_buffer_ != nullptr && mapped_light_constants_ != nullptr)
    {
        light_constant_buffer_->Unmap(0, nullptr);
        mapped_light_constants_ = nullptr;
    }
    if (draw_constant_buffer_ != nullptr && mapped_draw_constants_ != nullptr)
    {
        draw_constant_buffer_->Unmap(0, nullptr);
        mapped_draw_constants_ = nullptr;
    }
}

void PbrSceneRenderer::create_root_signatures()
{
    std::array<D3D12_DESCRIPTOR_RANGE, 10> ranges{};
    for (std::uint32_t index = 0; index < 5U; ++index)
    {
        auto& range = ranges[index];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = index;
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }
    for (std::uint32_t index = 0; index < 5U; ++index)
    {
        auto& range = ranges[5U + index];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = index;
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }

    std::array<D3D12_ROOT_PARAMETER, 13> parameters{};
    for (std::uint32_t index = 0; index < 3U; ++index)
    {
        parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[index].Descriptor.ShaderRegister = index;
        parameters[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    for (std::uint32_t index = 0; index < 5U; ++index)
    {
        parameters[kRootSrvBase + index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[kRootSrvBase + index].DescriptorTable.NumDescriptorRanges = 1;
        parameters[kRootSrvBase + index].DescriptorTable.pDescriptorRanges = &ranges[index];
        parameters[kRootSrvBase + index].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[kRootSamplerBase + index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[kRootSamplerBase + index].DescriptorTable.NumDescriptorRanges = 1;
        parameters[kRootSamplerBase + index].DescriptorTable.pDescriptorRanges = &ranges[5U + index];
        parameters[kRootSamplerBase + index].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    D3D12_ROOT_SIGNATURE_DESC raster_description{};
    raster_description.NumParameters = static_cast<UINT>(parameters.size());
    raster_description.pParameters = parameters.data();
    raster_description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                               D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                               D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                               D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    HRESULT result = D3D12SerializeRootSignature(&raster_description, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(result))
    {
        const std::string details = errors == nullptr ? std::string{} :
            std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        throw ResultError(static_cast<ResultCode>(result), "D3D12SerializeRootSignature(PBR): " + details);
    }
    DAEDALUS_THROW_IF_FAILED(device_->CreateRootSignature(
        0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&raster_root_signature_)));

    D3D12_DESCRIPTOR_RANGE tone_range{};
    tone_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    tone_range.NumDescriptors = 1;
    tone_range.BaseShaderRegister = 0;
    tone_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    std::array<D3D12_ROOT_PARAMETER, 2> tone_parameters{};
    tone_parameters[kToneRootSrv].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    tone_parameters[kToneRootSrv].DescriptorTable.NumDescriptorRanges = 1;
    tone_parameters[kToneRootSrv].DescriptorTable.pDescriptorRanges = &tone_range;
    tone_parameters[kToneRootSrv].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    tone_parameters[kToneRootConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    tone_parameters[kToneRootConstants].Constants.ShaderRegister = 0;
    tone_parameters[kToneRootConstants].Constants.Num32BitValues = 4;
    tone_parameters[kToneRootConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC tone_description{};
    tone_description.NumParameters = static_cast<UINT>(tone_parameters.size());
    tone_description.pParameters = tone_parameters.data();
    tone_description.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                             D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                             D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
    serialized.Reset();
    errors.Reset();
    result = D3D12SerializeRootSignature(&tone_description, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(result))
    {
        const std::string details = errors == nullptr ? std::string{} :
            std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        throw ResultError(static_cast<ResultCode>(result), "D3D12SerializeRootSignature(tone map): " + details);
    }
    DAEDALUS_THROW_IF_FAILED(device_->CreateRootSignature(
        0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&tone_root_signature_)));
}

void PbrSceneRenderer::create_pipeline_states(const std::filesystem::path& raster_vertex_shader,
                                              const std::filesystem::path& raster_pixel_shader,
                                              const std::filesystem::path& tone_vertex_shader,
                                              const std::filesystem::path& tone_pixel_shader)
{
    const std::vector<std::byte> raster_vertex_bytes = read_binary_file(raster_vertex_shader);
    const std::vector<std::byte> raster_pixel_bytes = read_binary_file(raster_pixel_shader);
    const D3D12_INPUT_ELEMENT_DESC input_layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    auto create_raster_pipeline = [&](bool blend, bool double_sided, bool negative_determinant,
                                      D3D12_PRIMITIVE_TOPOLOGY_TYPE topology,
                                      D3D12_DEPTH_WRITE_MASK depth_write,
                                      Microsoft::WRL::ComPtr<ID3D12PipelineState>& destination)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
        description.pRootSignature = raster_root_signature_.Get();
        description.VS = {raster_vertex_bytes.data(), raster_vertex_bytes.size()};
        description.PS = {raster_pixel_bytes.data(), raster_pixel_bytes.size()};
        description.BlendState = blend_description(blend);
        description.SampleMask = UINT_MAX;
        description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        description.RasterizerState.CullMode = double_sided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
        description.RasterizerState.FrontCounterClockwise = negative_determinant ? FALSE : TRUE;
        description.RasterizerState.DepthClipEnable = TRUE;
        description.DepthStencilState.DepthEnable = TRUE;
        description.DepthStencilState.DepthWriteMask = depth_write;
        description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        description.DepthStencilState.StencilEnable = FALSE;
        description.InputLayout = {input_layout, static_cast<UINT>(std::size(input_layout))};
        description.PrimitiveTopologyType = topology;
        description.NumRenderTargets = 1;
        description.RTVFormats[0] = kHdrFormat;
        description.DSVFormat = kDepthFormat;
        description.SampleDesc.Count = 1;
        DAEDALUS_THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&destination)));
    };

    create_raster_pipeline(false, false, false, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D12_DEPTH_WRITE_MASK_ALL,
                           raster_pipelines_[static_cast<std::size_t>(PsoIndex::opaque_single_positive)]);
    create_raster_pipeline(false, false, true, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D12_DEPTH_WRITE_MASK_ALL,
                           raster_pipelines_[static_cast<std::size_t>(PsoIndex::opaque_single_negative)]);
    create_raster_pipeline(false, true, false, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D12_DEPTH_WRITE_MASK_ALL,
                           raster_pipelines_[static_cast<std::size_t>(PsoIndex::opaque_double_positive)]);
    create_raster_pipeline(false, true, true, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D12_DEPTH_WRITE_MASK_ALL,
                           raster_pipelines_[static_cast<std::size_t>(PsoIndex::opaque_double_negative)]);
    create_raster_pipeline(true, false, false, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D12_DEPTH_WRITE_MASK_ZERO,
                           raster_pipelines_[static_cast<std::size_t>(PsoIndex::blend_single_positive)]);
    create_raster_pipeline(true, false, true, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D12_DEPTH_WRITE_MASK_ZERO,
                           raster_pipelines_[static_cast<std::size_t>(PsoIndex::blend_single_negative)]);
    create_raster_pipeline(true, true, false, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D12_DEPTH_WRITE_MASK_ZERO,
                           raster_pipelines_[static_cast<std::size_t>(PsoIndex::blend_double_positive)]);
    create_raster_pipeline(true, true, true, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D12_DEPTH_WRITE_MASK_ZERO,
                           raster_pipelines_[static_cast<std::size_t>(PsoIndex::blend_double_negative)]);
    create_raster_pipeline(false, true, false, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE, D3D12_DEPTH_WRITE_MASK_ZERO, line_pipeline_);

    const std::vector<std::byte> tone_vertex_bytes = read_binary_file(tone_vertex_shader);
    const std::vector<std::byte> tone_pixel_bytes = read_binary_file(tone_pixel_shader);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC tone{};
    tone.pRootSignature = tone_root_signature_.Get();
    tone.VS = {tone_vertex_bytes.data(), tone_vertex_bytes.size()};
    tone.PS = {tone_pixel_bytes.data(), tone_pixel_bytes.size()};
    tone.BlendState = blend_description(false);
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
    DAEDALUS_THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&tone, IID_PPV_ARGS(&tone_pipeline_)));
}

void PbrSceneRenderer::create_descriptor_heaps()
{
    const std::uint64_t scene_srv_count = checked_multiply_u64(scene_.textures.size(), 2U, "texture SRV descriptor");
    const std::uint64_t total_srv_count = checked_add_u64(scene_srv_count, 3U, "SRV descriptor"); // white linear/sRGB plus HDR
    if (total_srv_count > D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1)
        throw std::runtime_error("scene requires more shader-visible SRV descriptors than the C1 forward renderer supports");
    D3D12_DESCRIPTOR_HEAP_DESC srv_description{};
    srv_description.NumDescriptors = checked_uint(static_cast<std::size_t>(total_srv_count), "SRV descriptor count");
    srv_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    DAEDALUS_THROW_IF_FAILED(device_->CreateDescriptorHeap(&srv_description, IID_PPV_ARGS(&srv_heap_)));
    srv_increment_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    hdr_srv_index_ = checked_uint(static_cast<std::size_t>(checked_add_u64(scene_srv_count, 2U, "HDR SRV index")), "HDR SRV index");

    if (scene_.samplers.size() == std::numeric_limits<std::size_t>::max())
        throw std::overflow_error("sampler descriptor count overflow");
    const std::size_t sampler_count = scene_.samplers.size() + 1U;
    if (sampler_count > D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE)
        throw std::runtime_error("scene requires more shader-visible samplers than D3D12 permits");
    D3D12_DESCRIPTOR_HEAP_DESC sampler_description{};
    sampler_description.NumDescriptors = checked_uint(sampler_count, "sampler descriptor count");
    sampler_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampler_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    DAEDALUS_THROW_IF_FAILED(device_->CreateDescriptorHeap(&sampler_description, IID_PPV_ARGS(&sampler_heap_)));
    sampler_increment_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    D3D12_DESCRIPTOR_HEAP_DESC hdr_rtv_description{};
    hdr_rtv_description.NumDescriptors = 1;
    hdr_rtv_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    DAEDALUS_THROW_IF_FAILED(device_->CreateDescriptorHeap(&hdr_rtv_description, IID_PPV_ARGS(&hdr_rtv_heap_)));

    D3D12_DESCRIPTOR_HEAP_DESC dsv_description{};
    dsv_description.NumDescriptors = 1;
    dsv_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    DAEDALUS_THROW_IF_FAILED(device_->CreateDescriptorHeap(&dsv_description, IID_PPV_ARGS(&dsv_heap_)));
}

void PbrSceneRenderer::create_textures_and_samplers()
{
    if (scene_.textures.size() == std::numeric_limits<std::size_t>::max())
        throw std::overflow_error("texture resource count overflow");
    textures_.reserve(scene_.textures.size() + 1U);
    texture_linear_srv_indices_.resize(scene_.textures.size());
    texture_srgb_srv_indices_.resize(scene_.textures.size());
    texture_sampler_indices_.resize(scene_.textures.size());

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

    const std::array<std::byte, 4> white{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
    textures_.push_back(upload_texture_rgba8(1U, 1U, white.data()));
    create_srv(kWhiteLinearSrv, textures_.front().Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
    create_srv(kWhiteSrgbSrv, textures_.front().Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

    for (std::size_t index = 0; index < scene_.textures.size(); ++index)
    {
        const Texture& texture = scene_.textures[index];
        if (!texture.image.valid() || texture.image.value() >= scene_.images.size())
            throw std::runtime_error("canonical texture references an invalid image");
        const Image& image = scene_.images[texture.image.value()];
        const std::uint64_t expected_bytes = checked_multiply_u64(
            checked_multiply_u64(image.width, image.height, "canonical decoded image"), 4U, "canonical decoded image");
        if (image.decoded_rgba8.size() != expected_bytes || image.row_stride != static_cast<std::uint64_t>(image.width) * 4ULL)
            throw std::runtime_error("canonical image does not contain validated tightly packed RGBA8 pixels");
        textures_.push_back(upload_texture_rgba8(image.width, image.height, image.decoded_rgba8.data()));
        const std::uint32_t linear_descriptor = kTextureSrvBase + static_cast<std::uint32_t>(index * 2U);
        const std::uint32_t srgb_descriptor = linear_descriptor + 1U;
        texture_linear_srv_indices_[index] = linear_descriptor;
        texture_srgb_srv_indices_[index] = srgb_descriptor;
        if (texture.sampler.valid() && texture.sampler.value() >= scene_.samplers.size())
            throw std::runtime_error("canonical texture references a sampler outside the sampler table");
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
        // C1 uploads exactly one mip. Clamp LOD to zero so mipmapped glTF min filters cannot
        // address nonexistent levels. C2 may replace this with a real mip-generation policy.
        description.MinLOD = 0.0F;
        description.MaxLOD = 0.0F;
        D3D12_CPU_DESCRIPTOR_HANDLE handle = sampler_heap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(descriptor_index) * sampler_increment_;
        device_->CreateSampler(&description, handle);
    };
    write_sampler(0U, Sampler{});
    for (std::size_t index = 0; index < scene_.samplers.size(); ++index)
        write_sampler(static_cast<std::uint32_t>(index + 1U), scene_.samplers[index]);
}

void PbrSceneRenderer::create_geometry()
{
    primitives_.reserve(scene_.primitives.size());
    for (const Primitive& primitive : scene_.primitives)
    {
        if (primitive.vertices.empty() || primitive.indices.empty())
            throw std::runtime_error("canonical primitive is empty");
        const std::uint64_t vertex_bytes_u64 = checked_multiply_u64(primitive.vertices.size(), sizeof(Vertex), "vertex buffer");
        const std::uint64_t index_bytes_u64 = checked_multiply_u64(primitive.indices.size(), sizeof(std::uint32_t), "index buffer");
        if (vertex_bytes_u64 > std::numeric_limits<std::size_t>::max() || index_bytes_u64 > std::numeric_limits<std::size_t>::max())
            throw std::overflow_error("canonical geometry exceeds the process address range");

        GpuPrimitive gpu;
        gpu.vertex_buffer = upload_buffer(primitive.vertices.data(), static_cast<std::size_t>(vertex_bytes_u64),
                                          D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        gpu.index_buffer = upload_buffer(primitive.indices.data(), static_cast<std::size_t>(index_bytes_u64),
                                         D3D12_RESOURCE_STATE_INDEX_BUFFER);
        gpu.vertex_view.BufferLocation = gpu.vertex_buffer->GetGPUVirtualAddress();
        gpu.vertex_view.SizeInBytes = checked_uint(static_cast<std::size_t>(vertex_bytes_u64), "vertex buffer view");
        gpu.vertex_view.StrideInBytes = checked_uint(sizeof(Vertex), "vertex stride");
        gpu.index_view.BufferLocation = gpu.index_buffer->GetGPUVirtualAddress();
        gpu.index_view.SizeInBytes = checked_uint(static_cast<std::size_t>(index_bytes_u64), "index buffer view");
        gpu.index_view.Format = DXGI_FORMAT_R32_UINT;
        gpu.index_count = checked_uint(primitive.indices.size(), "index count");
        primitives_.push_back(std::move(gpu));
    }
}

void PbrSceneRenderer::create_draw_items()
{
    draw_items_ = prepare_raster_draws(scene_);
    lights_ = prepare_punctual_lights(scene_);
    for (const PreparedRasterDraw& draw : draw_items_)
    {
        if (draw.primitive_index >= primitives_.size())
            throw std::runtime_error("prepared raster draw references an invalid GPU primitive");
        if (draw.normal.texture.has_value() && !draw.normal_mapping_enabled)
        {
            Log::warning("Normal map disabled for primitive " + std::to_string(draw.primitive_index) +
                         " because the canonical primitive has no tangent basis");
        }
    }
}

void PbrSceneRenderer::create_bounds_geometry()
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

void PbrSceneRenderer::create_constant_buffers()
{
    frame_constant_stride_ = align_constant_buffer_size(sizeof(RasterFrameConstants));
    light_constant_stride_ = align_constant_buffer_size(sizeof(RasterLightConstants));
    draw_constant_stride_ = align_constant_buffer_size(sizeof(RasterDrawConstants));
    if (draw_items_.size() == std::numeric_limits<std::size_t>::max())
        throw std::overflow_error("draw constant slot count overflow");
    draw_slots_per_frame_ = std::max<std::size_t>(1U, draw_items_.size() + 1U);

    auto create_mapped_upload = [&](std::uint64_t byte_size,
                                    Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
                                    std::byte*& mapped)
    {
        const D3D12_HEAP_PROPERTIES heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC description = buffer_description(byte_size);
        DAEDALUS_THROW_IF_FAILED(device_->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource)));
        D3D12_RANGE read_range{0, 0};
        DAEDALUS_THROW_IF_FAILED(resource->Map(0, &read_range, reinterpret_cast<void**>(&mapped)));
    };

    create_mapped_upload(checked_multiply_u64(frame_constant_stride_, D3D12Context::kFrameCount, "frame constants"),
                         frame_constant_buffer_, mapped_frame_constants_);
    create_mapped_upload(checked_multiply_u64(light_constant_stride_, D3D12Context::kFrameCount, "light constants"),
                         light_constant_buffer_, mapped_light_constants_);
    const std::uint64_t draw_frame_bytes = checked_multiply_u64(draw_constant_stride_, draw_slots_per_frame_, "draw frame constants");
    create_mapped_upload(checked_multiply_u64(draw_frame_bytes, D3D12Context::kFrameCount, "draw constants"),
                         draw_constant_buffer_, mapped_draw_constants_);
}

void PbrSceneRenderer::create_depth_buffer(std::uint32_t width, std::uint32_t height)
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

void PbrSceneRenderer::create_hdr_target(std::uint32_t width, std::uint32_t height)
{
    hdr_target_.Reset();
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = kHdrFormat;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = kHdrFormat;
    clear.Color[0] = 0.0F;
    clear.Color[1] = 0.0F;
    clear.Color[2] = 0.0F;
    clear.Color[3] = 0.0F;
    const D3D12_HEAP_PROPERTIES heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    DAEDALUS_THROW_IF_FAILED(device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
        IID_PPV_ARGS(&hdr_target_)));

    D3D12_RENDER_TARGET_VIEW_DESC rtv{};
    rtv.Format = kHdrFormat;
    rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device_->CreateRenderTargetView(hdr_target_.Get(), &rtv, hdr_rtv_heap_->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = kHdrFormat;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE handle = srv_heap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(hdr_srv_index_) * srv_increment_;
    device_->CreateShaderResourceView(hdr_target_.Get(), &srv, handle);
}

void PbrSceneRenderer::write_frame_constants(std::uint32_t frame_index, const RasterFrameConstants& constants)
{
    std::memcpy(mapped_frame_constants_ + static_cast<std::size_t>(frame_index) * frame_constant_stride_, &constants, sizeof(constants));
}

void PbrSceneRenderer::write_light_constants(std::uint32_t frame_index, const RasterLightConstants& constants)
{
    std::memcpy(mapped_light_constants_ + static_cast<std::size_t>(frame_index) * light_constant_stride_, &constants, sizeof(constants));
}

void PbrSceneRenderer::write_draw_constants(std::size_t slot, const RasterDrawConstants& constants)
{
    std::memcpy(mapped_draw_constants_ + slot * draw_constant_stride_, &constants, sizeof(constants));
}

RasterDrawConstants PbrSceneRenderer::make_draw_constants(const PreparedRasterDraw& draw) const
{
    RasterDrawConstants constants{};
    constants.world = draw.world;
    bool invertible = false;
    constants.normal_matrix = inverse_transpose(draw.world, &invertible);
    if (!invertible)
        constants.normal_matrix = identity_matrix();
    constants.base_color_factor = draw.base_color_factor;
    constants.emissive_metallic = {draw.emissive_factor.x, draw.emissive_factor.y, draw.emissive_factor.z, draw.metallic_factor};
    constants.roughness_normal_ao_cutoff = {draw.roughness_factor, draw.normal_scale, draw.occlusion_strength, draw.alpha_cutoff};
    if (draw.has_vertex_colors) constants.flags |= kRasterFlagVertexColor;
    if (draw.base_color.texture.has_value()) constants.flags |= kRasterFlagBaseColorTexture;
    if (draw.metallic_roughness.texture.has_value()) constants.flags |= kRasterFlagMetallicRoughnessTexture;
    if (draw.normal_mapping_enabled) constants.flags |= kRasterFlagNormalTexture;
    if (draw.occlusion.texture.has_value()) constants.flags |= kRasterFlagOcclusionTexture;
    if (draw.emissive.texture.has_value()) constants.flags |= kRasterFlagEmissiveTexture;
    if (draw.double_sided) constants.flags |= kRasterFlagDoubleSided;
    if (draw.has_tangents) constants.flags |= kRasterFlagHasTangents;
    constants.material_diagnostic_id = draw.material_diagnostic_id;
    constants.alpha_mode = alpha_mode_value(draw.alpha_mode);
    constants.base_color_texcoord = draw.base_color.texcoord_set;
    constants.metallic_roughness_texcoord = draw.metallic_roughness.texcoord_set;
    constants.normal_texcoord = draw.normal.texcoord_set;
    constants.occlusion_texcoord = draw.occlusion.texcoord_set;
    constants.emissive_texcoord = draw.emissive.texcoord_set;
    constants.world_handedness = draw.negative_determinant ? -1.0F : 1.0F;
    return constants;
}

ID3D12PipelineState* PbrSceneRenderer::pipeline_for(const PreparedRasterDraw& draw) const noexcept
{
    const bool blend = draw.alpha_mode == AlphaMode::blend;
    const bool double_sided = draw.double_sided;
    const bool negative = draw.negative_determinant;
    std::size_t index = blend ? 4U : 0U;
    if (double_sided) index += 2U;
    if (negative) index += 1U;
    return raster_pipelines_[index].Get();
}

D3D12_GPU_DESCRIPTOR_HANDLE PbrSceneRenderer::srv_gpu_handle(std::uint32_t index) const noexcept
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = srv_heap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * srv_increment_;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE PbrSceneRenderer::sampler_gpu_handle(std::uint32_t index) const noexcept
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = sampler_heap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * sampler_increment_;
    return handle;
}

std::uint32_t PbrSceneRenderer::srv_index_for(const PreparedTextureBinding& binding, bool srgb) const
{
    if (!binding.texture.has_value()) return srgb ? kWhiteSrgbSrv : kWhiteLinearSrv;
    const std::uint32_t index = binding.texture->value();
    if (index >= texture_linear_srv_indices_.size())
        throw std::runtime_error("prepared draw references a texture outside the uploaded texture table");
    return srgb ? texture_srgb_srv_indices_[index] : texture_linear_srv_indices_[index];
}

std::uint32_t PbrSceneRenderer::sampler_index_for(const PreparedTextureBinding& binding) const
{
    if (!binding.texture.has_value()) return 0U;
    const std::uint32_t index = binding.texture->value();
    if (index >= texture_sampler_indices_.size())
        throw std::runtime_error("prepared draw references a sampler outside the uploaded texture table");
    return texture_sampler_indices_[index];
}

void PbrSceneRenderer::record(const FrameRecordingContext& frame)
{
    if (frame.frame_index >= D3D12Context::kFrameCount)
        throw std::runtime_error("frame index exceeds constant-buffer frame partition count");
    ID3D12GraphicsCommandList* command_list = frame.command_list;

    RasterFrameConstants frame_constants{};
    const float aspect = frame.height == 0U ? 1.0F : static_cast<float>(frame.width) / static_cast<float>(frame.height);
    frame_constants.view_projection = camera_.view_projection_matrix(aspect);
    const Vec3 camera_position = camera_.eye();
    frame_constants.camera_position = {camera_position.x, camera_position.y, camera_position.z, 0.0F};
    frame_constants.provisional_ambient = {0.03F, 0.03F, 0.03F, 0.0F};
    frame_constants.diagnostic_mode = shader_diagnostic_value(mode_);
    frame_constants.light_count = checked_uint(lights_.size(), "punctual light count");
    write_frame_constants(frame.frame_index, frame_constants);

    RasterLightConstants light_constants{};
    for (std::size_t index = 0; index < lights_.size(); ++index)
    {
        const PreparedPunctualLight& source = lights_[index];
        RasterLightGpu& destination = light_constants.lights[index];
        destination.position_range = {source.position.x, source.position.y, source.position.z, source.range.value_or(0.0F)};
        destination.direction_outer_cos = {source.direction.x, source.direction.y, source.direction.z, source.outer_cone_cos};
        destination.color_intensity = {source.color.x, source.color.y, source.color.z, source.intensity};
        destination.type = static_cast<std::uint32_t>(source.type);
        destination.inner_cone_cos = source.inner_cone_cos;
    }
    write_light_constants(frame.frame_index, light_constants);

    const D3D12_RESOURCE_BARRIER hdr_begin = transition(
        hdr_target_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    command_list->ResourceBarrier(1, &hdr_begin);
    const D3D12_CPU_DESCRIPTOR_HANDLE hdr_rtv = hdr_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    const float hdr_clear[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    command_list->ClearRenderTargetView(hdr_rtv, hdr_clear, 0, nullptr);
    command_list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);
    command_list->OMSetRenderTargets(1, &hdr_rtv, FALSE, &dsv);

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(frame.width);
    viewport.Height = static_cast<float>(frame.height);
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(frame.width), static_cast<LONG>(frame.height)};
    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor);
    command_list->SetGraphicsRootSignature(raster_root_signature_.Get());
    ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get(), sampler_heap_.Get()};
    command_list->SetDescriptorHeaps(2, heaps);
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const D3D12_GPU_VIRTUAL_ADDRESS frame_address = frame_constant_buffer_->GetGPUVirtualAddress() +
        static_cast<UINT64>(frame.frame_index) * frame_constant_stride_;
    const D3D12_GPU_VIRTUAL_ADDRESS light_address = light_constant_buffer_->GetGPUVirtualAddress() +
        static_cast<UINT64>(frame.frame_index) * light_constant_stride_;
    command_list->SetGraphicsRootConstantBufferView(kRootFrameConstants, frame_address);
    command_list->SetGraphicsRootConstantBufferView(kRootLightConstants, light_address);

    const std::vector<std::size_t> draw_order = build_raster_draw_order(draw_items_, camera_position);
    const std::size_t frame_slot_base = static_cast<std::size_t>(frame.frame_index) * draw_slots_per_frame_;
    for (std::size_t order_index = 0; order_index < draw_order.size(); ++order_index)
    {
        const std::size_t draw_index = draw_order[order_index];
        const PreparedRasterDraw& draw = draw_items_[draw_index];
        const GpuPrimitive& primitive = primitives_[draw.primitive_index];
        const RasterDrawConstants constants = make_draw_constants(draw);
        const std::size_t slot = frame_slot_base + order_index;
        write_draw_constants(slot, constants);
        command_list->SetPipelineState(pipeline_for(draw));
        command_list->SetGraphicsRootConstantBufferView(
            kRootDrawConstants, draw_constant_buffer_->GetGPUVirtualAddress() + slot * draw_constant_stride_);

        const std::array<std::uint32_t, 5> srvs{
            srv_index_for(draw.base_color, true),
            srv_index_for(draw.metallic_roughness, false),
            srv_index_for(draw.normal, false),
            srv_index_for(draw.occlusion, false),
            srv_index_for(draw.emissive, true)};
        const std::array<std::uint32_t, 5> samplers{
            sampler_index_for(draw.base_color),
            sampler_index_for(draw.metallic_roughness),
            sampler_index_for(draw.normal),
            sampler_index_for(draw.occlusion),
            sampler_index_for(draw.emissive)};
        for (std::uint32_t binding = 0; binding < 5U; ++binding)
        {
            command_list->SetGraphicsRootDescriptorTable(kRootSrvBase + binding, srv_gpu_handle(srvs[binding]));
            command_list->SetGraphicsRootDescriptorTable(kRootSamplerBase + binding, sampler_gpu_handle(samplers[binding]));
        }
        command_list->IASetVertexBuffers(0, 1, &primitive.vertex_view);
        command_list->IASetIndexBuffer(&primitive.index_view);
        command_list->DrawIndexedInstanced(primitive.index_count, 1, 0, 0, 0);
    }

    if (mode_ == DiagnosticMode::bounds && bounds_vertex_count_ != 0U)
    {
        const std::size_t slot = frame_slot_base + draw_order.size();
        RasterDrawConstants constants{};
        constants.world = identity_matrix();
        constants.normal_matrix = identity_matrix();
        constants.base_color_factor = {1.0F, 0.8F, 0.1F, 1.0F};
        constants.flags = kRasterFlagBoundsOverlay;
        write_draw_constants(slot, constants);
        command_list->SetPipelineState(line_pipeline_.Get());
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        command_list->SetGraphicsRootConstantBufferView(
            kRootDrawConstants, draw_constant_buffer_->GetGPUVirtualAddress() + slot * draw_constant_stride_);
        for (std::uint32_t binding = 0; binding < 5U; ++binding)
        {
            command_list->SetGraphicsRootDescriptorTable(kRootSrvBase + binding,
                                                         srv_gpu_handle(binding == 0U || binding == 4U ? kWhiteSrgbSrv : kWhiteLinearSrv));
            command_list->SetGraphicsRootDescriptorTable(kRootSamplerBase + binding, sampler_gpu_handle(0U));
        }
        command_list->IASetVertexBuffers(0, 1, &bounds_vertex_view_);
        command_list->IASetIndexBuffer(nullptr);
        command_list->DrawInstanced(bounds_vertex_count_, 1, 0, 0);
    }

    const D3D12_RESOURCE_BARRIER hdr_end = transition(
        hdr_target_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    const D3D12_RESOURCE_BARRIER back_buffer_begin = transition(
        frame.back_buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const std::array<D3D12_RESOURCE_BARRIER, 2> pre_tone{hdr_end, back_buffer_begin};
    command_list->ResourceBarrier(static_cast<UINT>(pre_tone.size()), pre_tone.data());

    command_list->OMSetRenderTargets(1, &frame.render_target, FALSE, nullptr);
    command_list->SetGraphicsRootSignature(tone_root_signature_.Get());
    ID3D12DescriptorHeap* tone_heaps[] = {srv_heap_.Get()};
    command_list->SetDescriptorHeaps(1, tone_heaps);
    command_list->SetPipelineState(tone_pipeline_.Get());
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->IASetVertexBuffers(0, 0, nullptr);
    command_list->IASetIndexBuffer(nullptr);
    command_list->SetGraphicsRootDescriptorTable(kToneRootSrv, srv_gpu_handle(hdr_srv_index_));
    struct ToneRootConstants
    {
        float exposure_ev;
        std::uint32_t diagnostic_mode;
        std::uint32_t padding0;
        std::uint32_t padding1;
    } tone_constants{exposure_ev_, shader_diagnostic_value(mode_), 0U, 0U};
    command_list->SetGraphicsRoot32BitConstants(kToneRootConstants, 4U, &tone_constants, 0U);
    command_list->DrawInstanced(3, 1, 0, 0);

    const D3D12_RESOURCE_BARRIER back_buffer_end = transition(
        frame.back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    command_list->ResourceBarrier(1, &back_buffer_end);
}

void PbrSceneRenderer::resize(std::uint32_t width, std::uint32_t height)
{
    if (width == 0U || height == 0U) return;
    viewport_width_ = width;
    viewport_height_ = height;
    create_depth_buffer(width, height);
    create_hdr_target(width, height);
}

void PbrSceneRenderer::apply_input(const OrbitInput& input)
{
    camera_.apply_input(input, viewport_width_, viewport_height_);
}

Microsoft::WRL::ComPtr<ID3D12Resource> PbrSceneRenderer::upload_buffer(
    const void* data,
    std::size_t byte_size,
    D3D12_RESOURCE_STATES final_state)
{
    if (data == nullptr || byte_size == 0U) throw std::invalid_argument("upload_buffer requires nonempty data");
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

Microsoft::WRL::ComPtr<ID3D12Resource> PbrSceneRenderer::upload_texture_rgba8(
    std::uint32_t width,
    std::uint32_t height,
    const std::byte* rgba8)
{
    if (width == 0U || height == 0U || rgba8 == nullptr)
        throw std::invalid_argument("texture upload requires nonempty RGBA8 data");
    D3D12_RESOURCE_DESC texture_description{};
    texture_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_description.Width = width;
    texture_description.Height = height;
    texture_description.DepthOrArraySize = 1;
    texture_description.MipLevels = 1;
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
    if (rows != height || footprint.Footprint.RowPitch < source_pitch || upload_size == 0U)
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
        const D3D12_RESOURCE_BARRIER barrier = transition(
            texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        command_list->ResourceBarrier(1, &barrier);
    });
    return texture;
}
}
