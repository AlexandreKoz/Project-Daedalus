#include "graphics/temporal/TemporalDenoiser.h"

#include "core/Error.h"
#include "core/Log.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace daedalus
{
namespace
{
constexpr D3D12_RESOURCE_STATES kReadState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr float kNormalThreshold = 0.8F;  // cos(~37 deg): tolerant of rigid rotation between frames
constexpr float kDepthTolerance = 0.05F;  // relative previous-depth mismatch
constexpr float kSigmaLuminance = 4.0F;
constexpr float kSigmaNormal = 128.0F;
constexpr float kSigmaDepth = 1.0F;
}

TemporalDenoiser::TemporalDenoiser(ID3D12Device* device,
                                   GpuSceneResources& resources,
                                   ID3D12RootSignature* compute_root_signature,
                                   const std::filesystem::path& shader_directory,
                                   DescriptorBlock reproject_block,
                                   DescriptorBlock filter_block,
                                   std::uint32_t width,
                                   std::uint32_t height,
                                   std::wstring name)
    : device_(device), resources_(resources), name_(std::move(name))
{
    reproject_ = std::make_unique<ComputePipeline>(device_, compute_root_signature, shader_directory, "TemporalReprojectCS.dxil");
    atrous_ = std::make_unique<ComputePipeline>(device_, compute_root_signature, shader_directory, "AtrousFilterCS.dxil");
    const std::uint32_t reproject_base = resources_.block_base(reproject_block);
    const std::uint32_t filter_base = resources_.block_base(filter_block);
    reproject_sets_[0] = ComputeBindingSet(resources_, reproject_base);
    reproject_sets_[1] = ComputeBindingSet(resources_, reproject_base + kComputeBindingSetSize);
    srv_base_ = reproject_base + 2U * kComputeBindingSetSize;
    atrous_a_to_b_ = ComputeBindingSet(resources_, filter_base);
    atrous_b_to_a_ = ComputeBindingSet(resources_, filter_base + kComputeBindingSetSize);
    atrous_a_to_output_ = ComputeBindingSet(resources_, filter_base + 2U * kComputeBindingSetSize);
    atrous_b_to_output_ = ComputeBindingSet(resources_, filter_base + 3U * kComputeBindingSetSize);
    static_assert(4U * kComputeBindingSetSize <= GpuSceneDescriptorLayout::kDynamicBlockSize);
    static_assert(2U * kComputeBindingSetSize + 4U <= GpuSceneDescriptorLayout::kDynamicBlockSize);
    create_targets(width, height);
}

void TemporalDenoiser::create_targets(std::uint32_t width, std::uint32_t height)
{
    if (width == 0U || height == 0U) throw std::invalid_argument("denoiser targets require nonzero dimensions");
    width_ = width;
    height_ = height;
    const D3D12_RESOURCE_FLAGS uav = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    for (std::uint32_t index = 0; index < 2U; ++index)
    {
        history_color_[index] = create_texture(device_, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav, kReadState, nullptr,
                                               name_ + L" history colour");
        history_moments_[index] = create_texture(device_, width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, uav, kReadState, nullptr,
                                                 name_ + L" history moments");
    }
    previous_normal_roughness_ = create_texture(device_, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_NONE,
                                                kReadState, nullptr, name_ + L" previous normal");
    previous_position_depth_ = create_texture(device_, width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
                                              kReadState, nullptr, name_ + L" previous position depth");
    previous_identifiers_ = create_texture(device_, width, height, DXGI_FORMAT_R32G32B32A32_UINT, D3D12_RESOURCE_FLAG_NONE,
                                           kReadState, nullptr, name_ + L" previous identifiers");
    // Variance can exceed half-float range for HDR inputs, so the filter chain is float32.
    filter_a_ = create_texture(device_, width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, uav, kReadState, nullptr, name_ + L" filter A");
    filter_b_ = create_texture(device_, width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, uav, kReadState, nullptr, name_ + L" filter B");
    output_ = create_texture(device_, width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, uav, kReadState, nullptr, name_ + L" output");
    debug_ = create_texture(device_, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav, kReadState, nullptr, name_ + L" debug");
    output_.srv = srv_base_;
    debug_.srv = srv_base_ + 1U;
    resources_.write_texture_srv(output_.srv, output_.resource.Get(), output_.format);
    resources_.write_texture_srv(debug_.srv, debug_.resource.Get(), debug_.format);
    history_moments_[0].srv = srv_base_ + 2U;
    history_moments_[1].srv = srv_base_ + 3U;
    resources_.write_texture_srv(history_moments_[0].srv, history_moments_[0].resource.Get(), history_moments_[0].format);
    resources_.write_texture_srv(history_moments_[1].srv, history_moments_[1].resource.Get(), history_moments_[1].format);
    history_initialized_ = false;
    bound_ = false;
    current_ = 0;
}

void TemporalDenoiser::resize(std::uint32_t width, std::uint32_t height)
{
    create_targets(width, height);
}

void TemporalDenoiser::bind(const GpuTexture& signal, const TemporalGuide& guide)
{
    if (!signal.valid() || guide.normal_roughness == nullptr || guide.position_depth == nullptr ||
        guide.identifiers == nullptr || guide.motion == nullptr)
        throw std::invalid_argument("temporal denoiser requires a signal and a complete guide set");
    if (signal.width != width_ || signal.height != height_ || guide.normal_roughness->width != width_ ||
        guide.normal_roughness->height != height_)
        throw std::invalid_argument("temporal denoiser inputs do not match its resolution");
    if (guide.normal_roughness->format != previous_normal_roughness_.format ||
        guide.position_depth->format != previous_position_depth_.format ||
        guide.identifiers->format != previous_identifiers_.format)
        throw std::invalid_argument("temporal denoiser guide formats do not match the renderer-wide G-buffer contract");
    for (std::uint32_t parity = 0; parity < 2U; ++parity)
    {
        ComputeBindingSet& set = reproject_sets_[parity];
        const std::uint32_t previous = 1U - parity;
        set.clear();
        set.input(0U, signal);
        set.input(1U, *guide.normal_roughness);
        set.input(2U, *guide.position_depth);
        set.input(3U, *guide.identifiers);
        set.input(4U, *guide.motion);
        set.input(5U, previous_normal_roughness_);
        set.input(6U, previous_position_depth_);
        set.input(7U, previous_identifiers_);
        set.input(8U, history_color_[previous]);
        set.input(9U, history_moments_[previous]);
        set.output(0U, history_color_[parity]);
        set.output(1U, history_moments_[parity]);
        set.output(2U, filter_a_);
        set.output(3U, debug_);
    }
    auto atrous = [&](ComputeBindingSet& set, const GpuTexture& source, const GpuTexture& destination)
    {
        set.clear();
        set.input(0U, source);
        set.input(1U, *guide.normal_roughness);
        set.input(2U, *guide.position_depth);
        set.input(3U, *guide.identifiers);
        set.output(0U, destination);
    };
    atrous(atrous_a_to_b_, filter_a_, filter_b_);
    atrous(atrous_b_to_a_, filter_b_, filter_a_);
    atrous(atrous_a_to_output_, filter_a_, output_);
    atrous(atrous_b_to_output_, filter_b_, output_);
    bound_ = true;
    history_initialized_ = false;
}

void TemporalDenoiser::record(ID3D12GraphicsCommandList* command_list,
                              D3D12_GPU_VIRTUAL_ADDRESS view_constants,
                              const TemporalSettings& settings,
                              bool history_valid,
                              GpuTexture& signal,
                              const TemporalGuide& guide,
                              GpuTimer& timer)
{
    if (!bound_) throw std::logic_error("temporal denoiser recorded before its inputs were bound");
    const bool valid = history_valid && history_initialized_;
    if (!valid) ++stats_.history_resets;
    ++stats_.frames;
    current_ = 1U - current_;

    BarrierBatch barriers;
    barriers.transition(signal, kReadState);
    barriers.transition(*guide.normal_roughness, kReadState);
    barriers.transition(*guide.position_depth, kReadState);
    barriers.transition(*guide.identifiers, kReadState);
    barriers.transition(*guide.motion, kReadState);
    barriers.transition(history_color_[1U - current_], kReadState);
    barriers.transition(history_moments_[1U - current_], kReadState);
    barriers.transition(history_color_[current_], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers.transition(history_moments_[current_], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers.transition(filter_a_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers.transition(debug_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers.flush(command_list);

    ComputeConstants reproject_constants;
    reproject_constants.set_uint(0, std::max(settings.max_history_length, 1U));
    reproject_constants.set_float(1, settings.history_clip_gamma);
    reproject_constants.set_float(2, kNormalThreshold);
    reproject_constants.set_float(3, kDepthTolerance);
    reproject_constants.set_uint(4, valid ? 1U : 0U);
    reproject_->dispatch(command_list, resources_, view_constants, reproject_constants, reproject_sets_[current_], width_, height_);
    barriers.uav(history_color_[current_].resource.Get());
    barriers.uav(history_moments_[current_].resource.Get());
    barriers.transition(history_color_[current_], kReadState);
    barriers.transition(history_moments_[current_], kReadState);
    barriers.transition(filter_a_, kReadState);
    barriers.transition(debug_, kReadState);
    barriers.flush(command_list);

    const std::uint32_t iterations = std::min(settings.atrous_iterations, 5U);
    if (iterations == 0U)
    {
        barriers.transition(filter_a_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        barriers.transition(output_, D3D12_RESOURCE_STATE_COPY_DEST);
        barriers.flush(command_list);
        command_list->CopyResource(output_.resource.Get(), filter_a_.resource.Get());
    }
    else
    {
        bool source_is_a = true;
        for (std::uint32_t iteration = 0; iteration < iterations; ++iteration)
        {
            const bool last = iteration + 1U == iterations;
            GpuTexture& destination = last ? output_ : (source_is_a ? filter_b_ : filter_a_);
            GpuTexture& source = source_is_a ? filter_a_ : filter_b_;
            barriers.transition(source, kReadState);
            barriers.transition(destination, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            barriers.flush(command_list);
            ComputeConstants constants;
            constants.set_uint(0, 1U << iteration);
            constants.set_float(1, kSigmaLuminance);
            constants.set_float(2, kSigmaNormal);
            constants.set_float(3, kSigmaDepth);
            const ComputeBindingSet& set = last ? (source_is_a ? atrous_a_to_output_ : atrous_b_to_output_)
                                                : (source_is_a ? atrous_a_to_b_ : atrous_b_to_a_);
            atrous_->dispatch(command_list, resources_, view_constants, constants, set, width_, height_);
            barriers.uav(destination.resource.Get());
            barriers.flush(command_list);
            source_is_a = !source_is_a;
        }
    }

    // Keep the current guides for next frame's rejection tests.
    barriers.transition(output_, kReadState);
    barriers.transition(filter_a_, kReadState);
    barriers.transition(filter_b_, kReadState);
    barriers.transition(*guide.normal_roughness, D3D12_RESOURCE_STATE_COPY_SOURCE);
    barriers.transition(*guide.position_depth, D3D12_RESOURCE_STATE_COPY_SOURCE);
    barriers.transition(*guide.identifiers, D3D12_RESOURCE_STATE_COPY_SOURCE);
    barriers.transition(previous_normal_roughness_, D3D12_RESOURCE_STATE_COPY_DEST);
    barriers.transition(previous_position_depth_, D3D12_RESOURCE_STATE_COPY_DEST);
    barriers.transition(previous_identifiers_, D3D12_RESOURCE_STATE_COPY_DEST);
    barriers.flush(command_list);
    command_list->CopyResource(previous_normal_roughness_.resource.Get(), guide.normal_roughness->resource.Get());
    command_list->CopyResource(previous_position_depth_.resource.Get(), guide.position_depth->resource.Get());
    command_list->CopyResource(previous_identifiers_.resource.Get(), guide.identifiers->resource.Get());
    barriers.transition(*guide.normal_roughness, kReadState);
    barriers.transition(*guide.position_depth, kReadState);
    barriers.transition(*guide.identifiers, kReadState);
    barriers.transition(previous_normal_roughness_, kReadState);
    barriers.transition(previous_position_depth_, kReadState);
    barriers.transition(previous_identifiers_, kReadState);
    barriers.flush(command_list);
    history_initialized_ = true;
    timer.mark(command_list, GpuPass::denoise);
}

std::uint64_t TemporalDenoiser::committed_allocation_bytes() const noexcept
{
    std::uint64_t total = 0;
    auto add = [&](const GpuTexture& texture) noexcept
    {
        if (!texture.valid()) return;
        const D3D12_RESOURCE_DESC description = texture.resource->GetDesc();
        const D3D12_RESOURCE_ALLOCATION_INFO info = device_->GetResourceAllocationInfo(0, 1, &description);
        if (info.SizeInBytes != std::numeric_limits<std::uint64_t>::max()) total += info.SizeInBytes;
    };
    for (const GpuTexture& texture : history_color_) add(texture);
    for (const GpuTexture& texture : history_moments_) add(texture);
    add(previous_normal_roughness_);
    add(previous_position_depth_);
    add(previous_identifiers_);
    add(filter_a_);
    add(filter_b_);
    add(output_);
    add(debug_);
    return total;
}
}
