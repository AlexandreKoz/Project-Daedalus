#include "graphics/raytracing/ReferencePathTracer.h"

#include "core/Error.h"
#include "core/Log.h"

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <stdexcept>

namespace daedalus
{
namespace
{
constexpr D3D12_RESOURCE_STATES kReadState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr std::uint32_t kCounterSlot = 11U;
}

ReferencePathTracer::ReferencePathTracer(D3D12Context& context,
                                         GpuSceneResources& resources,
                                         RaytracingScene& scene,
                                         ID3D12RootSignature* compute_root_signature,
                                         const std::filesystem::path& shader_directory,
                                         std::uint32_t width,
                                         std::uint32_t height)
    : context_(context), resources_(resources), scene_(scene), device_(context.device())
{
    RaytracingPipelineDescription description;
    description.library = load_shader(shader_directory, "PathTracing.dxil");
    description.ray_generation = L"PathTraceRayGen";
    description.miss = {L"RadianceMiss", L"VisibilityMiss"};
    description.hit_groups = {{L"RadianceHitGroup", L"RadianceClosestHit", L"RadianceAnyHit"},
                              {L"VisibilityHitGroup", L"", L"VisibilityAnyHit"}};
    description.global_root_signature = scene_.global_root_signature();
    description.max_payload_bytes = kMaxPayloadBytes;
    description.max_attribute_bytes = kMaxAttributeBytes;
    description.max_recursion_depth = kMaxTraceRecursionDepth;
    pipeline_ = std::make_unique<RaytracingPipeline>(context_.device5(), resources_, description, L"Daedalus path-tracing pipeline");
    remodulate_ = std::make_unique<ComputePipeline>(device_, compute_root_signature, shader_directory, "PathTraceCompositeCS.dxil");
    counters_ = std::make_unique<RayTracingCounters>(device_);

    const std::uint32_t block = resources_.block_base(DescriptorBlock::path_tracing);
    ray_inputs_base_ = block;
    ray_outputs_base_ = block + RaytracingScene::kPassInputCount;
    for (std::uint32_t slot = 0; slot < RaytracingScene::kPassInputCount; ++slot) resources_.write_null_texture_srv(ray_inputs_base_ + slot);
    for (std::uint32_t slot = 0; slot < RaytracingScene::kPassOutputCount; ++slot) resources_.write_null_texture_uav(ray_outputs_base_ + slot);
    resources_.write_raw_buffer_uav(ray_outputs_base_ + kCounterSlot, counters_->buffer(), kRtCounterBytes);
    remodulate_bindings_ = ComputeBindingSet(resources_, resources_.block_base(DescriptorBlock::path_trace_composite));
    create_targets(width, height);
}

std::array<GpuTexture*, 11> ReferencePathTracer::uav_targets() noexcept
{
    return {&accumulation_, &moments_, &direct_accumulation_, &output_, &noisy_, &albedo_, &primary_emission_,
            &normal_roughness_, &position_depth_, &identifiers_, &motion_};
}

void ReferencePathTracer::create_targets(std::uint32_t width, std::uint32_t height)
{
    if (width == 0U || height == 0U) throw std::invalid_argument("path-tracer targets require nonzero dimensions");
    width_ = width;
    height_ = height;
    const D3D12_RESOURCE_FLAGS uav = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto make = [&](DXGI_FORMAT format, std::wstring_view name)
    {
        return create_texture(device_, width, height, format, uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, name);
    };
    accumulation_ = make(DXGI_FORMAT_R32G32B32A32_FLOAT, L"Daedalus PT accumulation");
    moments_ = make(DXGI_FORMAT_R32G32B32A32_FLOAT, L"Daedalus PT moments");
    direct_accumulation_ = make(DXGI_FORMAT_R32G32B32A32_FLOAT, L"Daedalus PT direct accumulation");
    output_ = make(DXGI_FORMAT_R32G32B32A32_FLOAT, L"Daedalus PT output");
    noisy_ = make(DXGI_FORMAT_R16G16B16A16_FLOAT, L"Daedalus PT demodulated signal");
    albedo_ = make(DXGI_FORMAT_R16G16B16A16_FLOAT, L"Daedalus PT primary albedo");
    primary_emission_ = make(DXGI_FORMAT_R16G16B16A16_FLOAT, L"Daedalus PT primary emission");
    normal_roughness_ = make(DXGI_FORMAT_R16G16B16A16_FLOAT, L"Daedalus PT normal roughness");
    position_depth_ = make(DXGI_FORMAT_R32G32B32A32_FLOAT, L"Daedalus PT position depth");
    identifiers_ = make(DXGI_FORMAT_R32G32B32A32_UINT, L"Daedalus PT identifiers");
    motion_ = make(DXGI_FORMAT_R16G16B16A16_FLOAT, L"Daedalus PT motion");
    denoised_output_ = make(DXGI_FORMAT_R32G32B32A32_FLOAT, L"Daedalus PT denoised output");

    const std::array<GpuTexture*, 11> targets = uav_targets();
    for (std::uint32_t slot = 0; slot < targets.size(); ++slot)
        resources_.write_texture_uav(ray_outputs_base_ + slot, targets[slot]->resource.Get(), targets[slot]->format);
    // Shader-visible SRVs for display/capture/denoiser consumers, after the composite binding set.
    std::uint32_t srv = remodulate_bindings_.base() + kComputeBindingSetSize;
    for (GpuTexture* target : targets)
    {
        target->srv = srv++;
        resources_.write_texture_srv(target->srv, target->resource.Get(), target->format);
    }
    denoised_output_.srv = srv++;
    resources_.write_texture_srv(denoised_output_.srv, denoised_output_.resource.Get(), denoised_output_.format);
    remodulate_bindings_.input(1U, albedo_);
    remodulate_bindings_.input(2U, primary_emission_);
    remodulate_bindings_.output(0U, denoised_output_);
    accumulated_samples_ = 0;
}

void ReferencePathTracer::resize(std::uint32_t width, std::uint32_t height)
{
    create_targets(width, height);
}

void ReferencePathTracer::bind_filtered_signal(const GpuTexture& filtered)
{
    remodulate_bindings_.input(0U, filtered);
}

void ReferencePathTracer::record(ID3D12GraphicsCommandList4* command_list, const PathTraceFrameInputs& inputs, GpuTimer& timer)
{
    if (command_list == nullptr) throw std::invalid_argument("path tracing requires ID3D12GraphicsCommandList4");
    if (inputs.samples_this_dispatch == 0U || inputs.samples_this_dispatch > 64U)
        throw std::invalid_argument("path-tracer samples per dispatch must be in [1, 64]");
    if (inputs.reset_accumulation) accumulated_samples_ = 0;
    if (accumulated_samples_ > std::numeric_limits<std::uint32_t>::max() - inputs.samples_this_dispatch)
        throw std::overflow_error("path-tracer sample index exceeds the 32-bit RNG stream key");
    counters_->begin(command_list, inputs.frame_index);
    BarrierBatch barriers;
    for (GpuTexture* target : uav_targets()) barriers.transition(*target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers.flush(command_list);

    const bool punctual = inputs.light_count > 0U;
    const bool emissive = scene_.emissive_triangle_count() > 0U;
    const bool environment = inputs.environment_intensity > 0.0F;
    const float categories = static_cast<float>(punctual) + static_cast<float>(emissive) + static_cast<float>(environment);
    RtPassConstants constants{};
    constants.light_count = inputs.light_count;
    constants.instance_count = static_cast<std::uint32_t>(scene_.tables().instances.size());
    constants.emissive_triangle_count = scene_.emissive_triangle_count();
    constants.flags = kRtPassAccumulate;
    if (punctual) constants.flags |= kRtPassPunctualNee;
    if (emissive) constants.flags |= kRtPassEmissiveNee;
    if (environment) constants.flags |= kRtPassEnvironmentNee;
    if (inputs.settings.denoise) constants.flags |= kRtPassDenoiserOutputs;
    constants.sample_base = static_cast<std::uint32_t>(accumulated_samples_);
    constants.samples_per_dispatch = inputs.samples_this_dispatch;
    constants.max_depth = inputs.settings.max_depth;
    constants.min_russian_roulette_depth = inputs.settings.min_russian_roulette_depth;
    constants.seed = inputs.settings.seed;
    constants.debug_view = static_cast<std::uint32_t>(inputs.settings.debug_view);
    constants.frame_index = inputs.frame_index;
    constants.accumulation_reset = accumulated_samples_ == 0U ? 1U : 0U;
    constants.firefly_clamp = inputs.settings.firefly_clamp;
    constants.ray_extent = inputs.ray_extent;
    constants.environment_sun_probability = 0.5F;
    // Light-category selection: uniform over the categories that can contribute. Any fixed choice
    // is unbiased; the probabilities enter every NEE and MIS pdf consistently.
    constants.probability_punctual = categories > 0.0F && punctual ? 1.0F / categories : 0.0F;
    constants.probability_emissive = categories > 0.0F && emissive ? 1.0F / categories : 0.0F;
    constants.probability_environment = categories > 0.0F && environment ? 1.0F / categories : 0.0F;
    constants.display_sample_target = inputs.settings.target_samples.has_value()
        ? static_cast<std::uint32_t>(std::min<std::uint64_t>(*inputs.settings.target_samples, std::numeric_limits<std::uint32_t>::max()))
        : 1024U;
    scene_.write_pass_constants(inputs.frame_index, RtPassSlot::path_trace, constants);
    scene_.bind(command_list, inputs.frame_index, RtPassSlot::path_trace, inputs.view_constants, inputs.light_constants,
                resources_.srv_gpu(ray_inputs_base_), resources_.srv_gpu(ray_outputs_base_));
    pipeline_->dispatch(command_list, width_, height_);
    for (GpuTexture* target : uav_targets()) barriers.uav(target->resource.Get());
    barriers.flush(command_list);
    accumulated_samples_ += inputs.samples_this_dispatch;

    barriers.transition(output_, kReadState);
    for (GpuTexture* guide : {&noisy_, &albedo_, &primary_emission_, &normal_roughness_, &position_depth_, &identifiers_, &motion_})
        barriers.transition(*guide, kReadState);
    barriers.flush(command_list);
    counters_->end(command_list, inputs.frame_index);
    timer.mark(command_list, GpuPass::ray_dispatch);
}

void ReferencePathTracer::record_remodulate(ID3D12GraphicsCommandList* command_list, D3D12_GPU_VIRTUAL_ADDRESS view_constants,
                                            GpuTexture& filtered, GpuTimer& timer)
{
    BarrierBatch barriers;
    barriers.transition(filtered, kReadState);
    barriers.transition(albedo_, kReadState);
    barriers.transition(primary_emission_, kReadState);
    barriers.transition(denoised_output_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers.flush(command_list);
    remodulate_->dispatch(command_list, resources_, view_constants, ComputeConstants{}, remodulate_bindings_, width_, height_);
    barriers.uav(denoised_output_.resource.Get());
    barriers.transition(denoised_output_, kReadState);
    barriers.flush(command_list);
    timer.mark(command_list, GpuPass::composite);
}

std::uint64_t ReferencePathTracer::committed_allocation_bytes() const noexcept
{
    std::uint64_t total = pipeline_->committed_allocation_bytes();
    auto add = [&](const GpuTexture& texture) noexcept
    {
        if (!texture.valid()) return;
        const D3D12_RESOURCE_DESC description = texture.resource->GetDesc();
        const D3D12_RESOURCE_ALLOCATION_INFO info = device_->GetResourceAllocationInfo(0, 1, &description);
        if (info.SizeInBytes != std::numeric_limits<std::uint64_t>::max()) total += info.SizeInBytes;
    };
    for (const GpuTexture* texture : {&accumulation_, &moments_, &direct_accumulation_, &output_, &noisy_, &albedo_,
                                      &primary_emission_, &normal_roughness_, &position_depth_, &identifiers_, &motion_,
                                      &denoised_output_})
        add(*texture);
    return total;
}
}
