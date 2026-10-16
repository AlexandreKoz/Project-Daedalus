#include "graphics/raytracing/HybridRayTracer.h"

#include "core/Error.h"
#include "core/Log.h"

#include <limits>
#include <stdexcept>

namespace daedalus
{
namespace
{
constexpr DXGI_FORMAT kSignalFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr D3D12_RESOURCE_STATES kReadState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

[[nodiscard]] std::uint32_t hybrid_flags(const HybridSettings& settings) noexcept
{
    std::uint32_t flags = 0;
    if (settings.shadows) flags |= kRtPassShadows;
    if (settings.reflections) flags |= kRtPassReflections;
    return flags;
}
}

HybridRayTracer::HybridRayTracer(D3D12Context& context,
                                 GpuSceneResources& resources,
                                 RaytracingScene& scene,
                                 ID3D12RootSignature* compute_root_signature,
                                 const std::filesystem::path& shader_directory,
                                 std::uint32_t width,
                                 std::uint32_t height)
    : context_(context), resources_(resources), scene_(scene), device_(context.device())
{
    RaytracingPipelineDescription description;
    description.library = load_shader(shader_directory, "HybridRayTracing.dxil");
    description.ray_generation = L"HybridRayGen";
    description.miss = {L"RadianceMiss", L"VisibilityMiss"};
    description.hit_groups = {{L"RadianceHitGroup", L"RadianceClosestHit", L"RadianceAnyHit"},
                              {L"VisibilityHitGroup", L"", L"VisibilityAnyHit"}};
    description.global_root_signature = scene_.global_root_signature();
    description.max_payload_bytes = kMaxPayloadBytes;
    description.max_attribute_bytes = kMaxAttributeBytes;
    description.max_recursion_depth = kMaxTraceRecursionDepth;
    pipeline_ = std::make_unique<RaytracingPipeline>(context_.device5(), resources_, description, L"Daedalus hybrid DXR pipeline");
    composite_ = std::make_unique<ComputePipeline>(device_, compute_root_signature, shader_directory, "HybridCompositeCS.dxil");
    counters_ = std::make_unique<RayTracingCounters>(device_);

    D3D12_DESCRIPTOR_HEAP_DESC rtv{};
    rtv.NumDescriptors = 1;
    rtv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    DAEDALUS_THROW_IF_FAILED(device_->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&rtv_heap_)));

    const std::uint32_t ray_block = resources_.block_base(DescriptorBlock::hybrid_ray_tracing);
    ray_inputs_base_ = ray_block;
    ray_outputs_base_ = ray_block + RaytracingScene::kPassInputCount;
    for (std::uint32_t slot = 0; slot < RaytracingScene::kPassInputCount; ++slot) resources_.write_null_texture_srv(ray_inputs_base_ + slot);
    for (std::uint32_t slot = 0; slot < RaytracingScene::kPassOutputCount; ++slot) resources_.write_null_texture_uav(ray_outputs_base_ + slot);
    composite_bindings_ = ComputeBindingSet(resources_, resources_.block_base(DescriptorBlock::hybrid_composite));
    resources_.write_raw_buffer_uav(ray_outputs_base_ + 2U, counters_->buffer(), kRtCounterBytes);
    create_targets(width, height);
    Log::info("Hybrid DXR stage created (" + std::to_string(pipeline_->shader_table_layout().total_bytes) + "-byte shader table)");
}

void HybridRayTracer::create_targets(std::uint32_t width, std::uint32_t height)
{
    if (width == 0U || height == 0U) throw std::invalid_argument("hybrid targets require nonzero dimensions");
    width_ = width;
    height_ = height;
    direct_ = create_texture(device_, width, height, kSignalFormat, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, L"Daedalus hybrid RT direct");
    reflection_ = create_texture(device_, width, height, kSignalFormat, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, L"Daedalus hybrid RT reflection");
    D3D12_CLEAR_VALUE clear{};
    clear.Format = kSignalFormat;
    output_ = create_texture(device_, width, height, kSignalFormat,
                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                             kReadState, &clear, L"Daedalus hybrid composite HDR");
    output_.rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_RENDER_TARGET_VIEW_DESC rtv{};
    rtv.Format = kSignalFormat;
    rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device_->CreateRenderTargetView(output_.resource.Get(), &rtv, output_.rtv);
    // Display/capture read the composite through a dedicated SRV slot after the transparent pass.
    output_.srv = composite_bindings_.base() + kComputeBindingSetSize;
    resources_.write_texture_srv(output_.srv, output_.resource.Get(), kSignalFormat);
    reflection_.srv = output_.srv + 1U;
    resources_.write_texture_srv(reflection_.srv, reflection_.resource.Get(), kSignalFormat);
    direct_.srv = output_.srv + 2U;
    resources_.write_texture_srv(direct_.srv, direct_.resource.Get(), kSignalFormat);

    resources_.write_texture_uav(ray_outputs_base_ + 0U, direct_.resource.Get(), kSignalFormat);
    resources_.write_texture_uav(ray_outputs_base_ + 1U, reflection_.resource.Get(), kSignalFormat);
    composite_bindings_.input(1U, direct_);
    composite_bindings_.input(2U, reflection_);
    if (!filtered_bound_) composite_bindings_.input(3U, reflection_);
    composite_bindings_.output(0U, output_);
}

void HybridRayTracer::resize(std::uint32_t width, std::uint32_t height)
{
    filtered_bound_ = false;
    create_targets(width, height);
}

void HybridRayTracer::bind_inputs(std::array<GpuTexture, kGBufferCount>& geometry_buffer, const GpuTexture& raster_hdr,
                                  const GpuTexture* filtered_reflection)
{
    for (const GpuTexture& target : geometry_buffer)
        if (!target.valid()) throw std::logic_error("hybrid ray tracing requires the raster G-buffer");
    resources_.write_texture_srv(ray_inputs_base_ + 0U, geometry_buffer[kGBufferNormalRoughness].resource.Get(), geometry_buffer[kGBufferNormalRoughness].format);
    resources_.write_texture_srv(ray_inputs_base_ + 1U, geometry_buffer[kGBufferAlbedoMetallic].resource.Get(), geometry_buffer[kGBufferAlbedoMetallic].format);
    resources_.write_texture_srv(ray_inputs_base_ + 2U, geometry_buffer[kGBufferPositionDepth].resource.Get(), geometry_buffer[kGBufferPositionDepth].format);
    resources_.write_texture_srv(ray_inputs_base_ + 3U, geometry_buffer[kGBufferIdentifiers].resource.Get(), geometry_buffer[kGBufferIdentifiers].format);
    composite_bindings_.input(0U, raster_hdr);
    composite_bindings_.input(4U, geometry_buffer[kGBufferNormalRoughness]);
    composite_bindings_.input(5U, geometry_buffer[kGBufferAlbedoMetallic]);
    composite_bindings_.input(6U, geometry_buffer[kGBufferPositionDepth]);
    composite_bindings_.input(7U, geometry_buffer[kGBufferIdentifiers]);
    filtered_bound_ = filtered_reflection != nullptr && filtered_reflection->valid();
    composite_bindings_.input(3U, filtered_bound_ ? *filtered_reflection : reflection_);
}

void HybridRayTracer::record_rays(ID3D12GraphicsCommandList4* command_list, const HybridFrameInputs& inputs,
                                  std::array<GpuTexture, kGBufferCount>& geometry_buffer, GpuTimer& timer)
{
    if (command_list == nullptr) throw std::invalid_argument("hybrid ray dispatch requires ID3D12GraphicsCommandList4");
    if (geometry_buffer[kGBufferNormalRoughness].width != width_ || geometry_buffer[kGBufferNormalRoughness].height != height_)
        throw std::logic_error("hybrid targets do not match the G-buffer resolution");
    counters_->begin(command_list, inputs.frame_index);
    BarrierBatch barriers;
    for (GpuTexture& target : geometry_buffer) barriers.transition(target, kReadState);
    barriers.transition(direct_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers.transition(reflection_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers.flush(command_list);

    RtPassConstants constants{};
    constants.light_count = inputs.light_count;
    constants.instance_count = static_cast<std::uint32_t>(scene_.tables().instances.size());
    constants.flags = hybrid_flags(inputs.settings);
    constants.frame_index = inputs.frame_index;
    constants.reflection_max_roughness = inputs.settings.reflection_max_roughness;
    constants.ray_extent = inputs.ray_extent;
    scene_.write_pass_constants(inputs.frame_index, RtPassSlot::hybrid, constants);
    scene_.bind(command_list, inputs.frame_index, RtPassSlot::hybrid, inputs.view_constants, inputs.light_constants,
                resources_.srv_gpu(ray_inputs_base_), resources_.srv_gpu(ray_outputs_base_));
    pipeline_->dispatch(command_list, width_, height_);
    barriers.uav(direct_.resource.Get());
    barriers.uav(reflection_.resource.Get());
    barriers.flush(command_list);
    counters_->end(command_list, inputs.frame_index);
    timer.mark(command_list, GpuPass::ray_dispatch);
}

void HybridRayTracer::record_composite(ID3D12GraphicsCommandList* command_list, const HybridFrameInputs& inputs,
                                       GpuTexture& raster_hdr, std::array<GpuTexture, kGBufferCount>& geometry_buffer,
                                       GpuTexture* filtered_reflection, GpuTimer& timer)
{
    BarrierBatch barriers;
    barriers.transition(raster_hdr, kReadState);
    for (GpuTexture& target : geometry_buffer) barriers.transition(target, kReadState);
    barriers.transition(direct_, kReadState);
    barriers.transition(reflection_, kReadState);
    const bool use_filtered = filtered_reflection != nullptr && filtered_bound_ && inputs.settings.denoise_reflections;
    if (use_filtered) barriers.transition(*filtered_reflection, kReadState);
    barriers.transition(output_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers.flush(command_list);

    ComputeConstants constants;
    std::uint32_t flags = 0;
    if (inputs.settings.shadows) flags |= 1U;
    if (inputs.settings.reflections) flags |= 2U;
    if (use_filtered) flags |= 4U;
    constants.set_uint(0, flags);
    constants.set_uint(1, static_cast<std::uint32_t>(inputs.settings.debug_view));
    constants.set_float(2, inputs.ray_extent < 1.0e29F && inputs.ray_extent > 0.0F ? 4.0F / inputs.ray_extent : 0.1F);
    composite_->dispatch(command_list, resources_, inputs.view_constants, constants, composite_bindings_, width_, height_);
    barriers.uav(output_.resource.Get());
    barriers.transition(output_, kReadState);
    barriers.transition(direct_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers.transition(reflection_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers.flush(command_list);
    timer.mark(command_list, GpuPass::composite);
}

std::uint64_t HybridRayTracer::committed_allocation_bytes() const noexcept
{
    std::uint64_t total = pipeline_->committed_allocation_bytes();
    auto add = [&](ID3D12Resource* resource) noexcept
    {
        if (resource == nullptr) return;
        const D3D12_RESOURCE_DESC description = resource->GetDesc();
        const D3D12_RESOURCE_ALLOCATION_INFO info = device_->GetResourceAllocationInfo(0, 1, &description);
        if (info.SizeInBytes != std::numeric_limits<std::uint64_t>::max()) total += info.SizeInBytes;
    };
    add(direct_.resource.Get());
    add(reflection_.resource.Get());
    add(output_.resource.Get());
    return total;
}
}
