#include "graphics/PbrSceneRenderer.h"

#include "core/Error.h"
#include "core/Log.h"
#include "rendering/CampaignC2Math.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstring>
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
constexpr std::uint32_t kRootFrameConstants = 0U;
constexpr std::uint32_t kRootDrawConstants = 1U;
constexpr std::uint32_t kRootLightConstants = 2U;
constexpr std::uint32_t kRootSrvBase = 3U;
constexpr std::uint32_t kRootSamplerBase = 9U;
constexpr std::uint32_t kRootShadowSrv = kRootSrvBase + 5U;
constexpr std::uint32_t kRootShadowSampler = kRootSamplerBase + 5U;
constexpr std::uint32_t kRootTemporalDrawConstants = 15U; // b3
constexpr std::uint32_t kRootViewConstants = 16U;         // b4
constexpr std::uint32_t kShadowMapSize = 2048U;
constexpr std::uint32_t kRasterFlagBoundsOverlay = 1U << 8U;

// G-buffer MRT formats, in SV_Target1..5 order (SV_Target0 is the HDR radiance target).
constexpr std::array<DXGI_FORMAT, kGBufferCount> kGBufferFormats{
    DXGI_FORMAT_R16G16B16A16_FLOAT,  // normal xyz + perceptual roughness
    DXGI_FORMAT_R16G16B16A16_FLOAT,  // linear base colour + metallic
    DXGI_FORMAT_R32G32B32A32_FLOAT,  // world position + linear view depth
    DXGI_FORMAT_R16G16B16A16_FLOAT,  // motion vector (previous - current pixel) + previous linear depth
    DXGI_FORMAT_R32G32B32A32_UINT};  // instance+1, material+1, octahedral geometric normal, AO/flags
static_assert(kGBufferFormats[kGBufferNormalRoughness] == DXGI_FORMAT_R16G16B16A16_FLOAT);
static_assert(kGBufferFormats[kGBufferPositionDepth] == DXGI_FORMAT_R32G32B32A32_FLOAT);
static_assert(kGBufferFormats[kGBufferMotion] == DXGI_FORMAT_R16G16B16A16_FLOAT);
static_assert(kGBufferFormats[kGBufferIdentifiers] == DXGI_FORMAT_R32G32B32A32_UINT);

constexpr D3D12_RESOURCE_STATES kShaderReadState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

[[nodiscard]] std::size_t align_constant_buffer_size(std::size_t value)
{
    constexpr std::size_t alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    if (value > std::numeric_limits<std::size_t>::max() - (alignment - 1U))
        throw std::overflow_error("constant-buffer alignment overflow");
    return (value + alignment - 1U) & ~(alignment - 1U);
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
    case DiagnosticMode::depth: return 10U;
    case DiagnosticMode::overdraw: return 11U;
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
    for (auto& target : description.RenderTarget)
    {
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
    }
    return description;
}

[[nodiscard]] D3D12_BLEND_DESC additive_blend_description() noexcept
{
    D3D12_BLEND_DESC description{};
    auto& target = description.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D12_BLEND_ONE;
    target.DestBlend = D3D12_BLEND_ONE;
    target.BlendOp = D3D12_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D12_BLEND_ONE;
    target.DestBlendAlpha = D3D12_BLEND_ONE;
    target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return description;
}
}

PbrSceneRenderer::PbrSceneRenderer(D3D12Context& context,
                                   GpuSceneResources& resources,
                                   const CanonicalScene& scene,
                                   DiagnosticMode mode,
                                   float environment_intensity,
                                   bool shadows_enabled,
                                   float shadow_constant_bias,
                                   float shadow_normal_bias,
                                   const std::filesystem::path& shader_directory,
                                   std::uint32_t render_width,
                                   std::uint32_t render_height)
    : context_(context),
      resources_(resources),
      device_(context.device()),
      scene_(scene),
      mode_(mode),
      environment_intensity_(environment_intensity),
      shadows_enabled_(shadows_enabled),
      viewport_width_(render_width),
      viewport_height_(render_height)
{
    static_assert(sizeof(Vertex) == 72, "canonical Vertex layout changed; update the D3D12 input layout");
    if (device_ == nullptr) throw std::invalid_argument("PbrSceneRenderer requires a D3D12 device");
    if (render_width == 0U || render_height == 0U) throw std::invalid_argument("raster render dimensions must be nonzero");
    if (!std::isfinite(environment_intensity_) || environment_intensity_ < 0.0F || environment_intensity_ > 64.0F)
        throw std::invalid_argument("IBL environment intensity must be finite and within [0, 64]");
    if (!std::isfinite(shadow_constant_bias) || shadow_constant_bias < 0.0F || shadow_constant_bias > 0.05F ||
        !std::isfinite(shadow_normal_bias) || shadow_normal_bias < 0.0F || shadow_normal_bias > 0.05F)
        throw std::invalid_argument("shadow bias values must be finite and within [0, 0.05]");

    const std::uint32_t block = resources_.block_base(DescriptorBlock::raster_targets);
    hdr_srv_index_ = block;
    shadow_srv_index_ = block + 1U;
    geometry_buffer_srv_base_ = block + 2U;

    create_root_signatures();
    create_pipeline_states(shader_directory);
    create_rtv_dsv_heaps();
    create_draw_items();
    shadow_projection_.constant_bias = shadow_constant_bias;
    shadow_projection_.normal_bias = shadow_normal_bias;
    if (shadows_enabled_ && scene_.selected_scene.valid() && scene_.selected_scene.value() < scene_.scenes.size())
    {
        const float constant_bias = shadow_projection_.constant_bias;
        const float normal_bias = shadow_projection_.normal_bias;
        shadow_projection_ = build_shadow_projection(scene_.scenes[scene_.selected_scene.value()].bounds, lights_);
        shadow_projection_.constant_bias = constant_bias;
        shadow_projection_.normal_bias = normal_bias;
    }
    create_shadow_resources();
    create_bounds_geometry();
    create_constant_buffers();
    create_depth_buffer(viewport_width_, viewport_height_);
    create_hdr_target(viewport_width_, viewport_height_);

    std::ostringstream stream;
    stream << "Forward PBR raster passes created: draws=" << draw_items_.size()
           << " primitives=" << resources_.primitives().size()
           << " textures=" << scene_.textures.size()
           << " punctual_lights=" << lights_.size()
           << " shadow_light=" << (shadow_projection_.enabled ? std::to_string(shadow_projection_.light_index) : std::string("none"))
           << " environment_intensity=" << environment_intensity_
           << " diagnostic=" << to_string(mode_);
    Log::info(stream.str());
}

PbrSceneRenderer::~PbrSceneRenderer()
{
    auto unmap = [](Microsoft::WRL::ComPtr<ID3D12Resource>& resource, std::byte*& mapped)
    {
        if (resource != nullptr && mapped != nullptr) resource->Unmap(0, nullptr);
        mapped = nullptr;
    };
    unmap(frame_constant_buffer_, mapped_frame_constants_);
    unmap(light_constant_buffer_, mapped_light_constants_);
    unmap(draw_constant_buffer_, mapped_draw_constants_);
    unmap(temporal_draw_constant_buffer_, mapped_temporal_draw_constants_);
    unmap(shadow_frame_constant_buffer_, mapped_shadow_frame_constants_);
}

void PbrSceneRenderer::create_root_signatures()
{
    std::array<D3D12_DESCRIPTOR_RANGE, 12> ranges{};
    for (std::uint32_t index = 0; index < 6U; ++index)
    {
        auto& range = ranges[index];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = index;
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }
    for (std::uint32_t index = 0; index < 6U; ++index)
    {
        auto& range = ranges[6U + index];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = index;
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }

    std::array<D3D12_ROOT_PARAMETER, 17> parameters{};
    for (std::uint32_t index = 0; index < 3U; ++index)
    {
        parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[index].Descriptor.ShaderRegister = index;
        parameters[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    for (std::uint32_t index = 0; index < 6U; ++index)
    {
        parameters[kRootSrvBase + index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[kRootSrvBase + index].DescriptorTable.NumDescriptorRanges = 1;
        parameters[kRootSrvBase + index].DescriptorTable.pDescriptorRanges = &ranges[index];
        parameters[kRootSrvBase + index].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[kRootSamplerBase + index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[kRootSamplerBase + index].DescriptorTable.NumDescriptorRanges = 1;
        parameters[kRootSamplerBase + index].DescriptorTable.pDescriptorRanges = &ranges[6U + index];
        parameters[kRootSamplerBase + index].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    parameters[kRootTemporalDrawConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[kRootTemporalDrawConstants].Descriptor.ShaderRegister = 3;
    parameters[kRootTemporalDrawConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[kRootViewConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[kRootViewConstants].Descriptor.ShaderRegister = 4;
    parameters[kRootViewConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC raster_description{};
    raster_description.NumParameters = static_cast<UINT>(parameters.size());
    raster_description.pParameters = parameters.data();
    raster_description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                               D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                               D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                               D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
    raster_root_signature_ = create_root_signature(device_, raster_description, "PBR");

    D3D12_DESCRIPTOR_RANGE shadow_srv_range{};
    shadow_srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadow_srv_range.NumDescriptors = 1;
    shadow_srv_range.BaseShaderRegister = 0;
    shadow_srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_DESCRIPTOR_RANGE shadow_sampler_range{};
    shadow_sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    shadow_sampler_range.NumDescriptors = 1;
    shadow_sampler_range.BaseShaderRegister = 0;
    shadow_sampler_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    std::array<D3D12_ROOT_PARAMETER, 4> shadow_parameters{};
    shadow_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    shadow_parameters[0].Descriptor.ShaderRegister = 0;
    shadow_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    shadow_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    shadow_parameters[1].Descriptor.ShaderRegister = 1;
    shadow_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    shadow_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    shadow_parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    shadow_parameters[2].DescriptorTable.pDescriptorRanges = &shadow_srv_range;
    shadow_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    shadow_parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    shadow_parameters[3].DescriptorTable.NumDescriptorRanges = 1;
    shadow_parameters[3].DescriptorTable.pDescriptorRanges = &shadow_sampler_range;
    shadow_parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC shadow_description{};
    shadow_description.NumParameters = static_cast<UINT>(shadow_parameters.size());
    shadow_description.pParameters = shadow_parameters.data();
    shadow_description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                               D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                               D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                               D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
    shadow_root_signature_ = create_root_signature(device_, shadow_description, "shadow");
}

void PbrSceneRenderer::create_pipeline_states(const std::filesystem::path& shader_directory)
{
    const std::vector<std::byte> raster_vertex_bytes = load_shader(shader_directory, "RasterPbrVS.dxil");
    const std::vector<std::byte> raster_pixel_bytes = load_shader(shader_directory, "RasterPbrPS.dxil");
    const std::vector<std::byte> gbuffer_pixel_bytes = load_shader(shader_directory, "RasterPbrGBufferPS.dxil");
    const D3D12_INPUT_ELEMENT_DESC input_layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    auto base_description = [&](bool double_sided, bool negative_determinant)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
        description.pRootSignature = raster_root_signature_.Get();
        description.VS = {raster_vertex_bytes.data(), raster_vertex_bytes.size()};
        description.PS = {raster_pixel_bytes.data(), raster_pixel_bytes.size()};
        description.SampleMask = UINT_MAX;
        description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        description.RasterizerState.CullMode = double_sided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
        // Raster winding is evaluated in screen space, so mirrored (negative-determinant) draws flip
        // the front-face convention. DXR evaluates facing in object space and needs no such flip.
        description.RasterizerState.FrontCounterClockwise = negative_determinant ? FALSE : TRUE;
        description.RasterizerState.DepthClipEnable = TRUE;
        description.DepthStencilState.DepthEnable = TRUE;
        description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        description.DepthStencilState.StencilEnable = FALSE;
        description.InputLayout = {input_layout, static_cast<UINT>(std::size(input_layout))};
        description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        description.NumRenderTargets = 1;
        description.RTVFormats[0] = kHdrFormat;
        description.DSVFormat = kDepthFormat;
        description.SampleDesc.Count = 1;
        return description;
    };

    auto create_raster_pipeline = [&](bool blend, bool double_sided, bool negative_determinant,
                                      D3D12_PRIMITIVE_TOPOLOGY_TYPE topology,
                                      D3D12_DEPTH_WRITE_MASK depth_write,
                                      Microsoft::WRL::ComPtr<ID3D12PipelineState>& destination)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC description = base_description(double_sided, negative_determinant);
        description.BlendState = blend_description(blend);
        description.DepthStencilState.DepthWriteMask = depth_write;
        description.PrimitiveTopologyType = topology;
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

    // OPAQUE/MASK G-buffer variants: SV_Target0 HDR radiance + SV_Target1..5 G-buffer, no blending.
    for (std::size_t index = 0; index < geometry_buffer_pipelines_.size(); ++index)
    {
        const bool double_sided = (index & 2U) != 0U;
        const bool negative = (index & 1U) != 0U;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC description = base_description(double_sided, negative);
        description.PS = {gbuffer_pixel_bytes.data(), gbuffer_pixel_bytes.size()};
        description.BlendState = blend_description(false);
        description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        description.NumRenderTargets = 1U + kGBufferCount;
        for (std::uint32_t target = 0; target < kGBufferCount; ++target) description.RTVFormats[1U + target] = kGBufferFormats[target];
        DAEDALUS_THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&geometry_buffer_pipelines_[index])));
    }

    for (std::size_t index = 0; index < overdraw_pipelines_.size(); ++index)
    {
        const bool double_sided = (index & 2U) != 0U;
        const bool negative = (index & 1U) != 0U;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC description = base_description(double_sided, negative);
        description.BlendState = additive_blend_description();
        description.DepthStencilState.DepthEnable = FALSE;
        description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        description.DSVFormat = DXGI_FORMAT_UNKNOWN;
        DAEDALUS_THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&overdraw_pipelines_[index])));
    }

    const std::vector<std::byte> shadow_vertex_bytes = load_shader(shader_directory, "ShadowVS.dxil");
    const std::vector<std::byte> shadow_pixel_bytes = load_shader(shader_directory, "ShadowPS.dxil");
    for (std::size_t index = 0; index < shadow_pipelines_.size(); ++index)
    {
        const bool double_sided = (index & 2U) != 0U;
        const bool negative = (index & 1U) != 0U;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
        description.pRootSignature = shadow_root_signature_.Get();
        description.VS = {shadow_vertex_bytes.data(), shadow_vertex_bytes.size()};
        description.PS = {shadow_pixel_bytes.data(), shadow_pixel_bytes.size()};
        description.BlendState = blend_description(false);
        description.SampleMask = UINT_MAX;
        description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        description.RasterizerState.CullMode = double_sided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
        description.RasterizerState.FrontCounterClockwise = negative ? FALSE : TRUE;
        description.RasterizerState.DepthBias = 0;
        description.RasterizerState.SlopeScaledDepthBias = 0.0F;
        description.RasterizerState.DepthClipEnable = TRUE;
        description.DepthStencilState.DepthEnable = TRUE;
        description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        description.InputLayout = {input_layout, static_cast<UINT>(std::size(input_layout))};
        description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        description.NumRenderTargets = 0;
        description.DSVFormat = kDepthFormat;
        description.SampleDesc.Count = 1;
        DAEDALUS_THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&shadow_pipelines_[index])));
    }
}

void PbrSceneRenderer::create_rtv_dsv_heaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtv_description{};
    rtv_description.NumDescriptors = 1U + kGBufferCount; // HDR then G-buffer, contiguous for one OMSetRenderTargets range
    rtv_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    DAEDALUS_THROW_IF_FAILED(device_->CreateDescriptorHeap(&rtv_description, IID_PPV_ARGS(&rtv_heap_)));
    rtv_increment_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsv_description{};
    dsv_description.NumDescriptors = 1;
    dsv_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    DAEDALUS_THROW_IF_FAILED(device_->CreateDescriptorHeap(&dsv_description, IID_PPV_ARGS(&dsv_heap_)));
    DAEDALUS_THROW_IF_FAILED(device_->CreateDescriptorHeap(&dsv_description, IID_PPV_ARGS(&shadow_dsv_heap_)));
}

void PbrSceneRenderer::create_draw_items()
{
    draw_items_ = prepare_raster_draws(scene_);
    lights_ = prepare_punctual_lights(scene_);
    for (const PreparedRasterDraw& draw : draw_items_)
    {
        if (draw.primitive_index >= resources_.primitives().size())
            throw std::runtime_error("prepared raster draw references an invalid GPU primitive");
        if (draw.normal.texture.has_value() && !draw.normal_mapping_enabled)
        {
            Log::warning("Normal map disabled for primitive " + std::to_string(draw.primitive_index) +
                         " because the canonical primitive has no tangent basis");
        }
    }
}

void PbrSceneRenderer::create_shadow_resources()
{
    shadow_map_.Reset();
    const D3D12_RESOURCE_DESC description = texture2d_description(kShadowMapSize, kShadowMapSize, DXGI_FORMAT_R32_TYPELESS,
                                                                  D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE clear{};
    clear.Format = kDepthFormat;
    clear.DepthStencil.Depth = 1.0F;
    const D3D12_HEAP_PROPERTIES heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    DAEDALUS_THROW_IF_FAILED(device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clear, IID_PPV_ARGS(&shadow_map_)));
    set_debug_name(shadow_map_.Get(), L"Daedalus raster shadow map");

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = kDepthFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(shadow_map_.Get(), &dsv, shadow_dsv_heap_->GetCPUDescriptorHandleForHeapStart());
    resources_.write_texture_srv(shadow_srv_index_, shadow_map_.Get(), DXGI_FORMAT_R32_FLOAT);
}

void PbrSceneRenderer::record_shadow_pass(ID3D12GraphicsCommandList* command_list,
                                          const RasterFrameInputs& inputs,
                                          std::size_t frame_slot_base)
{
    if (!shadows_enabled_ || !shadow_projection_.enabled) return;

    ShadowFrameConstants shadow_constants{};
    shadow_constants.light_view_projection = shadow_projection_.view_projection;
    std::memcpy(mapped_shadow_frame_constants_ + static_cast<std::size_t>(inputs.frame_index) * shadow_frame_constant_stride_,
                &shadow_constants, sizeof(shadow_constants));

    const D3D12_RESOURCE_BARRIER begin = transition_barrier(
        shadow_map_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    command_list->ResourceBarrier(1, &begin);
    const D3D12_CPU_DESCRIPTOR_HANDLE shadow_dsv = shadow_dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    command_list->ClearDepthStencilView(shadow_dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);
    command_list->OMSetRenderTargets(0, nullptr, FALSE, &shadow_dsv);

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(kShadowMapSize);
    viewport.Height = static_cast<float>(kShadowMapSize);
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(kShadowMapSize), static_cast<LONG>(kShadowMapSize)};
    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor);
    command_list->SetGraphicsRootSignature(shadow_root_signature_.Get());
    ID3D12DescriptorHeap* heaps[] = {resources_.srv_heap(), resources_.sampler_heap()};
    command_list->SetDescriptorHeaps(2, heaps);
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->SetGraphicsRootConstantBufferView(
        0U, shadow_frame_constant_buffer_->GetGPUVirtualAddress() + static_cast<UINT64>(inputs.frame_index) * shadow_frame_constant_stride_);

    std::size_t slot_offset = 0U;
    for (std::size_t draw_index = 0; draw_index < draw_items_.size(); ++draw_index)
    {
        const PreparedRasterDraw& draw = draw_items_[draw_index];
        if (draw.alpha_mode == AlphaMode::blend) continue;
        const GpuSceneResources::GpuPrimitive& primitive = resources_.primitives().at(draw.primitive_index);
        const std::size_t slot = frame_slot_base + slot_offset++;
        write_draw_constants(slot, make_draw_constants(draw, inputs.draw_world[draw_index], inputs.emissive_scale));
        std::size_t pipeline_index = draw.double_sided ? 2U : 0U;
        if (draw.negative_determinant) pipeline_index += 1U;
        command_list->SetPipelineState(shadow_pipelines_[pipeline_index].Get());
        command_list->SetGraphicsRootConstantBufferView(
            1U, draw_constant_buffer_->GetGPUVirtualAddress() + slot * draw_constant_stride_);
        command_list->SetGraphicsRootDescriptorTable(2U, resources_.srv_gpu(srv_index_for(draw.base_color, true)));
        command_list->SetGraphicsRootDescriptorTable(3U, resources_.sampler_gpu(sampler_index_for(draw.base_color)));
        command_list->IASetVertexBuffers(0, 1, &primitive.vertex_view);
        command_list->IASetIndexBuffer(&primitive.index_view);
        command_list->DrawIndexedInstanced(primitive.index_count, 1, 0, 0, 0);
    }

    const D3D12_RESOURCE_BARRIER end = transition_barrier(
        shadow_map_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    command_list->ResourceBarrier(1, &end);
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
    bounds_vertex_buffer_ = resources_.upload_buffer(vertices.data(), sizeof(vertices),
                                                     D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                                                     D3D12_RESOURCE_FLAG_NONE, L"Daedalus bounds overlay");
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
    temporal_draw_constant_stride_ = align_constant_buffer_size(sizeof(RasterTemporalDrawConstants));
    shadow_frame_constant_stride_ = align_constant_buffer_size(sizeof(ShadowFrameConstants));
    if (draw_items_.size() >= std::numeric_limits<std::size_t>::max() / 4U)
        throw std::overflow_error("draw constant slot count overflow");
    // Per frame: [0, draws) main draws in draw order, [draws] bounds overlay, [draws + 1, ...) shadow draws.
    draw_slots_per_frame_ = std::max<std::size_t>(2U, (draw_items_.size() + 1U) * 2U);
    // Per frame: one temporal slot per prepared draw (indexed by stable instance ID) plus the overlay.
    temporal_slots_per_frame_ = draw_items_.size() + 1U;

    auto create_mapped_upload = [&](std::uint64_t byte_size, Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
                                    std::byte*& mapped, std::wstring_view name)
    {
        resource = create_buffer(device_, byte_size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                                 D3D12_RESOURCE_FLAG_NONE, name);
        const D3D12_RANGE read_range{0, 0};
        DAEDALUS_THROW_IF_FAILED(resource->Map(0, &read_range, reinterpret_cast<void**>(&mapped)));
    };

    create_mapped_upload(checked_multiply_u64(frame_constant_stride_, D3D12Context::kFrameCount, "frame constants"),
                         frame_constant_buffer_, mapped_frame_constants_, L"Daedalus raster frame constants");
    create_mapped_upload(checked_multiply_u64(light_constant_stride_, D3D12Context::kFrameCount, "light constants"),
                         light_constant_buffer_, mapped_light_constants_, L"Daedalus light constants");
    create_mapped_upload(checked_multiply_u64(shadow_frame_constant_stride_, D3D12Context::kFrameCount, "shadow frame constants"),
                         shadow_frame_constant_buffer_, mapped_shadow_frame_constants_, L"Daedalus shadow constants");
    const std::uint64_t draw_frame_bytes = checked_multiply_u64(draw_constant_stride_, draw_slots_per_frame_, "draw frame constants");
    create_mapped_upload(checked_multiply_u64(draw_frame_bytes, D3D12Context::kFrameCount, "draw constants"),
                         draw_constant_buffer_, mapped_draw_constants_, L"Daedalus draw constants");
    const std::uint64_t temporal_frame_bytes =
        checked_multiply_u64(temporal_draw_constant_stride_, temporal_slots_per_frame_, "temporal draw frame constants");
    create_mapped_upload(checked_multiply_u64(temporal_frame_bytes, D3D12Context::kFrameCount, "temporal draw constants"),
                         temporal_draw_constant_buffer_, mapped_temporal_draw_constants_, L"Daedalus temporal draw constants");
}

void PbrSceneRenderer::create_depth_buffer(std::uint32_t width, std::uint32_t height)
{
    depth_buffer_.Reset();
    // Typeless so an external consumer (the DLSS vendor glue) can create an R32_FLOAT view; Daedalus
    // itself never samples hardware depth (it uses linear depth from the G-buffer).
    const D3D12_RESOURCE_DESC description = texture2d_description(width, height, DXGI_FORMAT_R32_TYPELESS,
                                                                  D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE clear{};
    clear.Format = kDepthFormat;
    clear.DepthStencil.Depth = 1.0F;
    const D3D12_HEAP_PROPERTIES heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    DAEDALUS_THROW_IF_FAILED(device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth_buffer_)));
    set_debug_name(depth_buffer_.Get(), L"Daedalus raster depth");
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = kDepthFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(depth_buffer_.Get(), &dsv, dsv_heap_->GetCPUDescriptorHandleForHeapStart());
}

void PbrSceneRenderer::create_hdr_target(std::uint32_t width, std::uint32_t height)
{
    hdr_target_.reset();
    D3D12_CLEAR_VALUE clear{};
    clear.Format = kHdrFormat;
    hdr_target_ = create_texture(device_, width, height, kHdrFormat, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                 kShaderReadState, &clear, L"Daedalus raster HDR target");
    D3D12_RENDER_TARGET_VIEW_DESC rtv{};
    rtv.Format = kHdrFormat;
    rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    hdr_target_.rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    device_->CreateRenderTargetView(hdr_target_.resource.Get(), &rtv, hdr_target_.rtv);
    hdr_target_.srv = hdr_srv_index_;
    resources_.write_texture_srv(hdr_srv_index_, hdr_target_.resource.Get(), kHdrFormat);
}

void PbrSceneRenderer::create_geometry_buffer(std::uint32_t width, std::uint32_t height)
{
    for (std::uint32_t index = 0; index < kGBufferCount; ++index)
    {
        D3D12_CLEAR_VALUE clear{};
        clear.Format = kGBufferFormats[index];
        GpuTexture& target = geometry_buffer_[index];
        target = create_texture(device_, width, height, kGBufferFormats[index], D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                kShaderReadState, &clear, L"Daedalus raster G-buffer");
        D3D12_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.Format = kGBufferFormats[index];
        rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        target.rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        target.rtv.ptr += static_cast<SIZE_T>(1U + index) * rtv_increment_;
        device_->CreateRenderTargetView(target.resource.Get(), &rtv, target.rtv);
        target.srv = geometry_buffer_srv_base_ + index;
        resources_.write_texture_srv(target.srv, target.resource.Get(), kGBufferFormats[index]);
    }
}

void PbrSceneRenderer::set_geometry_buffer_enabled(bool enabled)
{
    if (enabled == geometry_buffer_enabled_) return;
    geometry_buffer_enabled_ = enabled;
    if (enabled)
    {
        create_geometry_buffer(viewport_width_, viewport_height_);
    }
    else
    {
        for (GpuTexture& target : geometry_buffer_) target.reset();
        for (std::uint32_t index = 0; index < kGBufferCount; ++index) resources_.write_null_texture_srv(geometry_buffer_srv_base_ + index);
    }
}

void PbrSceneRenderer::resize(std::uint32_t width, std::uint32_t height)
{
    if (width == 0U || height == 0U) return;
    viewport_width_ = width;
    viewport_height_ = height;
    create_depth_buffer(width, height);
    create_hdr_target(width, height);
    if (geometry_buffer_enabled_) create_geometry_buffer(width, height);
}

D3D12_GPU_VIRTUAL_ADDRESS PbrSceneRenderer::light_constants_address(std::uint32_t frame_index) const
{
    if (frame_index >= D3D12Context::kFrameCount) throw std::out_of_range("light constant frame index out of range");
    return light_constant_buffer_->GetGPUVirtualAddress() + static_cast<UINT64>(frame_index) * light_constant_stride_;
}

void PbrSceneRenderer::write_draw_constants(std::size_t slot, const RasterDrawConstants& constants)
{
    std::memcpy(mapped_draw_constants_ + slot * draw_constant_stride_, &constants, sizeof(constants));
}

void PbrSceneRenderer::write_temporal_draw_constants(std::size_t slot, const RasterTemporalDrawConstants& constants)
{
    std::memcpy(mapped_temporal_draw_constants_ + slot * temporal_draw_constant_stride_, &constants, sizeof(constants));
}

RasterDrawConstants PbrSceneRenderer::make_draw_constants(const PreparedRasterDraw& draw, const Mat4& world, float emissive_scale) const
{
    RasterDrawConstants constants{};
    constants.world = world;
    bool invertible = false;
    constants.normal_matrix = inverse_transpose(world, &invertible);
    if (!invertible)
        constants.normal_matrix = identity_matrix();
    constants.base_color_factor = draw.base_color_factor;
    const Vec3 emissive = draw.emissive_factor * emissive_scale;
    constants.emissive_metallic = {emissive.x, emissive.y, emissive.z, draw.metallic_factor};
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

ID3D12PipelineState* PbrSceneRenderer::pipeline_for(const PreparedRasterDraw& draw, bool geometry_buffer_pass) const noexcept
{
    const bool double_sided = draw.double_sided;
    const bool negative = draw.negative_determinant;
    if (mode_ == DiagnosticMode::overdraw)
    {
        std::size_t index = double_sided ? 2U : 0U;
        if (negative) index += 1U;
        return overdraw_pipelines_[index].Get();
    }
    const bool blend = draw.alpha_mode == AlphaMode::blend;
    if (geometry_buffer_pass && !blend)
    {
        std::size_t index = double_sided ? 2U : 0U;
        if (negative) index += 1U;
        return geometry_buffer_pipelines_[index].Get();
    }
    std::size_t index = blend ? 4U : 0U;
    if (double_sided) index += 2U;
    if (negative) index += 1U;
    return raster_pipelines_[index].Get();
}

std::uint32_t PbrSceneRenderer::srv_index_for(const PreparedTextureBinding& binding, bool srgb) const
{
    if (!binding.texture.has_value()) return srgb ? GpuSceneDescriptorLayout::kWhiteSrgb : GpuSceneDescriptorLayout::kWhiteLinear;
    const std::uint32_t index = binding.texture->value();
    return srgb ? resources_.layout().texture_srgb(index) : resources_.layout().texture_linear(index);
}

std::uint32_t PbrSceneRenderer::sampler_index_for(const PreparedTextureBinding& binding) const
{
    if (!binding.texture.has_value()) return resources_.layout().default_sampler();
    const std::uint32_t index = binding.texture->value();
    if (index >= scene_.textures.size())
        throw std::runtime_error("prepared draw references a sampler outside the uploaded texture table");
    const Texture& texture = scene_.textures[index];
    return texture.sampler.valid() ? resources_.layout().scene_sampler(texture.sampler.value()) : resources_.layout().default_sampler();
}

void PbrSceneRenderer::update_frame_constants(const RasterFrameInputs& inputs)
{
    if (inputs.frame_index >= D3D12Context::kFrameCount)
        throw std::runtime_error("frame index exceeds constant-buffer frame partition count");
    if (inputs.draw_world.size() != draw_items_.size() || inputs.previous_draw_world.size() != draw_items_.size())
        throw std::invalid_argument("raster frame inputs must provide one world transform per prepared draw");
    if (inputs.view_constants == 0U) throw std::invalid_argument("raster frame inputs require a view-constant buffer");

    RasterFrameConstants frame_constants{};
    frame_constants.view_projection = inputs.view_projection;
    frame_constants.shadow_view_projection = shadow_projection_.enabled ? shadow_projection_.view_projection : identity_matrix();
    frame_constants.camera_position = {inputs.camera_position.x, inputs.camera_position.y, inputs.camera_position.z, 0.0F};
    frame_constants.environment_shadow = {environment_intensity_, shadow_projection_.constant_bias,
                                          shadow_projection_.normal_bias, 1.0F / static_cast<float>(kShadowMapSize)};
    frame_constants.diagnostic_mode = shader_diagnostic_value(mode_);
    frame_constants.light_count = checked_uint(lights_.size(), "punctual light count");
    frame_constants.shadow_light_index = shadow_projection_.enabled
        ? checked_uint(shadow_projection_.light_index, "shadow light index") : 0xFFFFFFFFU;
    frame_constants.frame_flags = shadows_enabled_ && shadow_projection_.enabled ? kFrameFlagShadowsEnabled : 0U;
    std::memcpy(mapped_frame_constants_ + static_cast<std::size_t>(inputs.frame_index) * frame_constant_stride_,
                &frame_constants, sizeof(frame_constants));

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
    std::memcpy(mapped_light_constants_ + static_cast<std::size_t>(inputs.frame_index) * light_constant_stride_,
                &light_constants, sizeof(light_constants));
}

void PbrSceneRenderer::bind_raster_root(ID3D12GraphicsCommandList* command_list, const RasterFrameInputs& inputs)
{
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(viewport_width_);
    viewport.Height = static_cast<float>(viewport_height_);
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(viewport_width_), static_cast<LONG>(viewport_height_)};
    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor);
    command_list->SetGraphicsRootSignature(raster_root_signature_.Get());
    ID3D12DescriptorHeap* heaps[] = {resources_.srv_heap(), resources_.sampler_heap()};
    command_list->SetDescriptorHeaps(2, heaps);
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->SetGraphicsRootConstantBufferView(
        kRootFrameConstants,
        frame_constant_buffer_->GetGPUVirtualAddress() + static_cast<UINT64>(inputs.frame_index) * frame_constant_stride_);
    command_list->SetGraphicsRootConstantBufferView(kRootLightConstants, light_constants_address(inputs.frame_index));
    command_list->SetGraphicsRootConstantBufferView(kRootViewConstants, inputs.view_constants);
}

void PbrSceneRenderer::record_draw(ID3D12GraphicsCommandList* command_list,
                                   const RasterFrameInputs& inputs,
                                   std::size_t order_index,
                                   std::size_t frame_slot_base,
                                   bool geometry_buffer_pass)
{
    const std::size_t draw_index = draw_order_[order_index];
    const PreparedRasterDraw& draw = draw_items_[draw_index];
    const GpuSceneResources::GpuPrimitive& primitive = resources_.primitives()[draw.primitive_index];
    const std::size_t slot = frame_slot_base + order_index;
    write_draw_constants(slot, make_draw_constants(draw, inputs.draw_world[draw_index], inputs.emissive_scale));

    RasterTemporalDrawConstants temporal{};
    temporal.previous_world = inputs.previous_draw_world[draw_index];
    temporal.instance_id = checked_uint(draw_index, "stable instance identifier");
    temporal.material_table_index = draw.material_table_index;
    if (inputs.hybrid_composite && draw.alpha_mode != AlphaMode::blend) temporal.flags |= kTemporalDrawFlagHybridEligible;
    const std::size_t temporal_slot = static_cast<std::size_t>(inputs.frame_index) * temporal_slots_per_frame_ + draw_index;
    write_temporal_draw_constants(temporal_slot, temporal);

    command_list->SetPipelineState(pipeline_for(draw, geometry_buffer_pass));
    command_list->SetGraphicsRootConstantBufferView(
        kRootDrawConstants, draw_constant_buffer_->GetGPUVirtualAddress() + slot * draw_constant_stride_);
    command_list->SetGraphicsRootConstantBufferView(
        kRootTemporalDrawConstants,
        temporal_draw_constant_buffer_->GetGPUVirtualAddress() + temporal_slot * temporal_draw_constant_stride_);

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
        command_list->SetGraphicsRootDescriptorTable(kRootSrvBase + binding, resources_.srv_gpu(srvs[binding]));
        command_list->SetGraphicsRootDescriptorTable(kRootSamplerBase + binding, resources_.sampler_gpu(samplers[binding]));
    }
    command_list->SetGraphicsRootDescriptorTable(kRootShadowSrv, resources_.srv_gpu(shadow_srv_index_));
    command_list->SetGraphicsRootDescriptorTable(kRootShadowSampler, resources_.sampler_gpu(resources_.layout().shadow_sampler()));
    command_list->IASetVertexBuffers(0, 1, &primitive.vertex_view);
    command_list->IASetIndexBuffer(&primitive.index_view);
    command_list->DrawIndexedInstanced(primitive.index_count, 1, 0, 0, 0);
}

void PbrSceneRenderer::record_opaque(ID3D12GraphicsCommandList* command_list, const RasterFrameInputs& inputs, GpuTimer& timer)
{
    if (inputs.geometry_buffer && !geometry_buffer_enabled_)
        throw std::logic_error("G-buffer output requested before the raster G-buffer was created");
    update_frame_constants(inputs);
    draw_order_ = build_raster_draw_order(draw_items_, inputs.camera_position);

    const std::size_t frame_slot_base = static_cast<std::size_t>(inputs.frame_index) * draw_slots_per_frame_;
    const std::size_t shadow_slot_base = frame_slot_base + draw_items_.size() + 1U;
    record_shadow_pass(command_list, inputs, shadow_slot_base);
    timer.mark(command_list, GpuPass::shadow);

    BarrierBatch barriers;
    barriers.transition(hdr_target_, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (inputs.geometry_buffer)
        for (GpuTexture& target : geometry_buffer_) barriers.transition(target, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers.flush(command_list);

    const float zero[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    command_list->ClearRenderTargetView(hdr_target_.rtv, zero, 0, nullptr);
    if (inputs.geometry_buffer)
        for (GpuTexture& target : geometry_buffer_) command_list->ClearRenderTargetView(target.rtv, zero, 0, nullptr);
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    command_list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);
    // HDR and G-buffer RTVs are contiguous in rtv_heap_ (HDR first), so one range binds all six.
    const UINT target_count = inputs.geometry_buffer ? 1U + kGBufferCount : 1U;
    command_list->OMSetRenderTargets(target_count, &hdr_target_.rtv, TRUE, &dsv);
    bind_raster_root(command_list, inputs);

    for (std::size_t index = 0; index < draw_order_.size(); ++index)
    {
        if (draw_items_[draw_order_[index]].alpha_mode == AlphaMode::blend) break;
        record_draw(command_list, inputs, index, frame_slot_base, inputs.geometry_buffer);
    }
    timer.mark(command_list, GpuPass::opaque);

    barriers.transition(hdr_target_, kShaderReadState);
    if (inputs.geometry_buffer)
        for (GpuTexture& target : geometry_buffer_) barriers.transition(target, kShaderReadState);
    barriers.flush(command_list);
}

void PbrSceneRenderer::record_transparent(ID3D12GraphicsCommandList* command_list, const RasterFrameInputs& inputs, GpuTexture& target)
{
    if (!target.valid() || target.rtv.ptr == 0U) throw std::invalid_argument("transparent pass target needs a render-target view");
    if (target.width != viewport_width_ || target.height != viewport_height_)
        throw std::invalid_argument("transparent pass target does not match the raster render resolution");
    BarrierBatch barriers;
    barriers.transition(target, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers.flush(command_list);
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    command_list->OMSetRenderTargets(1, &target.rtv, FALSE, &dsv);
    bind_raster_root(command_list, inputs);

    const std::size_t frame_slot_base = static_cast<std::size_t>(inputs.frame_index) * draw_slots_per_frame_;
    for (std::size_t index = 0; index < draw_order_.size(); ++index)
        if (draw_items_[draw_order_[index]].alpha_mode == AlphaMode::blend)
            record_draw(command_list, inputs, index, frame_slot_base, false);

    if (mode_ == DiagnosticMode::bounds && bounds_vertex_count_ != 0U)
    {
        const std::size_t slot = frame_slot_base + draw_order_.size();
        RasterDrawConstants constants{};
        constants.world = identity_matrix();
        constants.normal_matrix = identity_matrix();
        constants.base_color_factor = {1.0F, 0.8F, 0.1F, 1.0F};
        constants.flags = kRasterFlagBoundsOverlay;
        write_draw_constants(slot, constants);
        RasterTemporalDrawConstants temporal{};
        temporal.previous_world = identity_matrix();
        temporal.instance_id = 0xFFFFFFFFU;
        const std::size_t temporal_slot = static_cast<std::size_t>(inputs.frame_index) * temporal_slots_per_frame_ + draw_items_.size();
        write_temporal_draw_constants(temporal_slot, temporal);
        command_list->SetPipelineState(line_pipeline_.Get());
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        command_list->SetGraphicsRootConstantBufferView(
            kRootDrawConstants, draw_constant_buffer_->GetGPUVirtualAddress() + slot * draw_constant_stride_);
        command_list->SetGraphicsRootConstantBufferView(
            kRootTemporalDrawConstants,
            temporal_draw_constant_buffer_->GetGPUVirtualAddress() + temporal_slot * temporal_draw_constant_stride_);
        for (std::uint32_t binding = 0; binding < 5U; ++binding)
        {
            command_list->SetGraphicsRootDescriptorTable(
                kRootSrvBase + binding,
                resources_.srv_gpu(binding == 0U || binding == 4U ? GpuSceneDescriptorLayout::kWhiteSrgb : GpuSceneDescriptorLayout::kWhiteLinear));
            command_list->SetGraphicsRootDescriptorTable(kRootSamplerBase + binding, resources_.sampler_gpu(resources_.layout().default_sampler()));
        }
        command_list->SetGraphicsRootDescriptorTable(kRootShadowSrv, resources_.srv_gpu(shadow_srv_index_));
        command_list->SetGraphicsRootDescriptorTable(kRootShadowSampler, resources_.sampler_gpu(resources_.layout().shadow_sampler()));
        command_list->IASetVertexBuffers(0, 1, &bounds_vertex_view_);
        command_list->IASetIndexBuffer(nullptr);
        command_list->DrawInstanced(bounds_vertex_count_, 1, 0, 0);
    }

    barriers.transition(target, kShaderReadState);
    barriers.flush(command_list);
}

std::uint64_t PbrSceneRenderer::committed_allocation_bytes() const noexcept
{
    std::uint64_t total = 0;
    auto add = [&](ID3D12Resource* resource) noexcept
    {
        if (resource == nullptr) return;
        const D3D12_RESOURCE_DESC description = resource->GetDesc();
        const D3D12_RESOURCE_ALLOCATION_INFO info = device_->GetResourceAllocationInfo(0, 1, &description);
        if (info.SizeInBytes != std::numeric_limits<std::uint64_t>::max() &&
            total <= std::numeric_limits<std::uint64_t>::max() - info.SizeInBytes)
            total += info.SizeInBytes;
    };
    add(hdr_target_.resource.Get());
    for (const GpuTexture& target : geometry_buffer_) add(target.resource.Get());
    add(depth_buffer_.Get());
    add(shadow_map_.Get());
    add(frame_constant_buffer_.Get());
    add(light_constant_buffer_.Get());
    add(draw_constant_buffer_.Get());
    add(temporal_draw_constant_buffer_.Get());
    add(shadow_frame_constant_buffer_.Get());
    add(bounds_vertex_buffer_.Get());
    return total;
}

std::uint64_t PbrSceneRenderer::render_target_count() const noexcept
{
    std::uint64_t count = hdr_target_.valid() ? 1U : 0U;
    for (const GpuTexture& target : geometry_buffer_) count += target.valid() ? 1U : 0U;
    return count;
}
}
