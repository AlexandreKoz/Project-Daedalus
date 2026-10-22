#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace daedalus
{
// Single source of truth for the shader-visible CBV/SRV/UAV and sampler heap layout owned by
// GpuSceneResources. Raster descriptor tables, DXR bindless tables and the CPU-built DXR material
// table all derive their indices from this structure, so they cannot diverge.
//
// CBV/SRV/UAV heap:
//   [0]                         white RGBA8 linear SRV
//   [1]                         white RGBA8 sRGB SRV
//   [2 + 2t], [3 + 2t]          scene texture t: linear SRV, sRGB SRV
//   [raw_base + 2p]             primitive p vertex buffer as ByteAddressBuffer (raw) SRV
//   [raw_base + 2p + 1]         primitive p index buffer as ByteAddressBuffer (raw) SRV
//                               (DXR binds a table starting AT raw_base; shaders index it with the
//                               table-relative 2p / 2p + 1 from *_table_index(), never raw_base + ...)
//   [dynamic_base, +dynamic)    fixed-size blocks reserved by renderer components
// Sampler heap:
//   [0]                         default glTF sampler (linear, repeat)
//   [1 + s]                     scene sampler s
//   [samplers + 1]              raster shadow comparison sampler
//   [samplers + 2]              linear clamp sampler (resampling/upscaling)
struct GpuSceneDescriptorLayout
{
    static constexpr std::uint32_t kWhiteLinear = 0U;
    static constexpr std::uint32_t kWhiteSrgb = 1U;
    static constexpr std::uint32_t kTextureBase = 2U;
    static constexpr std::uint32_t kDynamicBlockSize = 128U;
    static constexpr std::uint32_t kDynamicBlockCount = 12U;

    std::uint32_t texture_count = 0;
    std::uint32_t primitive_count = 0;
    std::uint32_t sampler_count = 0;

    [[nodiscard]] static GpuSceneDescriptorLayout create(std::uint64_t textures, std::uint64_t primitives, std::uint64_t samplers)
    {
        constexpr std::uint64_t limit = 1000000U; // D3D12 tier-1 shader-visible heap size
        const std::uint64_t total = 2U + textures * 2U + primitives * 2U +
                                    static_cast<std::uint64_t>(kDynamicBlockSize) * kDynamicBlockCount;
        if (textures > limit || primitives > limit || total > limit)
            throw std::runtime_error("scene requires " + std::to_string(total) +
                                     " shader-visible descriptors; the D3D12 limit used by Daedalus is 1000000");
        if (samplers + 3U > 2048U)
            throw std::runtime_error("scene requires more shader-visible samplers than D3D12 permits");
        GpuSceneDescriptorLayout layout;
        layout.texture_count = static_cast<std::uint32_t>(textures);
        layout.primitive_count = static_cast<std::uint32_t>(primitives);
        layout.sampler_count = static_cast<std::uint32_t>(samplers);
        return layout;
    }

    [[nodiscard]] std::uint32_t texture_linear(std::uint32_t texture) const
    {
        check(texture < texture_count, "texture index outside the descriptor layout");
        return kTextureBase + texture * 2U;
    }
    [[nodiscard]] std::uint32_t texture_srgb(std::uint32_t texture) const { return texture_linear(texture) + 1U; }
    [[nodiscard]] std::uint32_t raw_base() const noexcept { return kTextureBase + texture_count * 2U; }
    [[nodiscard]] std::uint32_t vertex_buffer(std::uint32_t primitive) const
    {
        check(primitive < primitive_count, "primitive index outside the descriptor layout");
        return raw_base() + primitive * 2U;
    }
    [[nodiscard]] std::uint32_t index_buffer(std::uint32_t primitive) const { return vertex_buffer(primitive) + 1U; }
    // Indices RELATIVE to raw_base(), i.e. into a descriptor table whose GPU base handle is
    // srv_gpu(raw_base()) (the DXR g_buffers[] table). Absolute heap indices from vertex_buffer()/
    // index_buffer() must never be used to index such a table: the raw-region offset would be
    // applied twice (audit D-H-STATIC-001).
    [[nodiscard]] std::uint32_t vertex_buffer_table_index(std::uint32_t primitive) const
    {
        check(primitive < primitive_count, "primitive index outside the descriptor layout");
        return primitive * 2U;
    }
    [[nodiscard]] std::uint32_t index_buffer_table_index(std::uint32_t primitive) const { return vertex_buffer_table_index(primitive) + 1U; }
    [[nodiscard]] std::uint32_t raw_buffer_table_size() const noexcept { return primitive_count * 2U; }
    [[nodiscard]] std::uint32_t dynamic_base() const noexcept { return raw_base() + primitive_count * 2U; }
    [[nodiscard]] std::uint32_t dynamic_block(std::uint32_t block) const
    {
        check(block < kDynamicBlockCount, "dynamic descriptor block index out of range");
        return dynamic_base() + block * kDynamicBlockSize;
    }
    [[nodiscard]] std::uint32_t total_descriptors() const noexcept
    {
        return dynamic_base() + kDynamicBlockSize * kDynamicBlockCount;
    }

    [[nodiscard]] std::uint32_t default_sampler() const noexcept { return 0U; }
    [[nodiscard]] std::uint32_t scene_sampler(std::uint32_t sampler) const
    {
        check(sampler < sampler_count, "sampler index outside the descriptor layout");
        return sampler + 1U;
    }
    [[nodiscard]] std::uint32_t shadow_sampler() const noexcept { return sampler_count + 1U; }
    [[nodiscard]] std::uint32_t linear_clamp_sampler() const noexcept { return sampler_count + 2U; }
    [[nodiscard]] std::uint32_t total_samplers() const noexcept { return sampler_count + 3U; }

private:
    static void check(bool condition, const char* message)
    {
        if (!condition) throw std::out_of_range(message);
    }
};

// Fixed dynamic block assignment. Each block holds kDynamicBlockSize descriptors and is owned by
// exactly one component for the lifetime of a GpuSceneResources instance.
enum class DescriptorBlock : std::uint32_t
{
    raster_targets = 0,
    display_output = 1,
    hybrid_ray_tracing = 2,
    hybrid_composite = 3,
    path_tracing = 4,
    path_trace_composite = 5,
    reflection_denoiser = 6,
    reflection_denoiser_filter = 7,
    path_trace_denoiser = 8,
    path_trace_denoiser_filter = 9,
    upscaler = 10,
    temporal_debug = 11
};
}
