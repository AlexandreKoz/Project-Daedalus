#pragma once

#ifndef _WIN32
#error PbrSceneRenderer is available only on Windows.
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "core/CommandLine.h"
#include "graphics/D3D12Context.h"
#include "rendering/OrbitCamera.h"
#include "rendering/RasterPreparation.h"
#include "rendering/RasterShaderContract.h"
#include "scene/Scene.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace daedalus
{
class PbrSceneRenderer final
{
public:
    PbrSceneRenderer(D3D12Context& context,
                     const CanonicalScene& scene,
                     DiagnosticMode mode,
                     float exposure_ev,
                     const std::filesystem::path& raster_vertex_shader,
                     const std::filesystem::path& raster_pixel_shader,
                     const std::filesystem::path& tone_vertex_shader,
                     const std::filesystem::path& tone_pixel_shader);
    ~PbrSceneRenderer();

    PbrSceneRenderer(const PbrSceneRenderer&) = delete;
    PbrSceneRenderer& operator=(const PbrSceneRenderer&) = delete;
    PbrSceneRenderer(PbrSceneRenderer&&) = delete;
    PbrSceneRenderer& operator=(PbrSceneRenderer&&) = delete;

    void record(const FrameRecordingContext& frame);
    void resize(std::uint32_t width, std::uint32_t height);
    void apply_input(const OrbitInput& input);

private:
    struct GpuPrimitive
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> index_buffer;
        D3D12_VERTEX_BUFFER_VIEW vertex_view{};
        D3D12_INDEX_BUFFER_VIEW index_view{};
        std::uint32_t index_count = 0;
    };

    enum class PsoIndex : std::size_t
    {
        opaque_single_positive = 0,
        opaque_single_negative,
        opaque_double_positive,
        opaque_double_negative,
        blend_single_positive,
        blend_single_negative,
        blend_double_positive,
        blend_double_negative,
        count
    };

    void create_root_signatures();
    void create_pipeline_states(const std::filesystem::path& raster_vertex_shader,
                                const std::filesystem::path& raster_pixel_shader,
                                const std::filesystem::path& tone_vertex_shader,
                                const std::filesystem::path& tone_pixel_shader);
    void create_descriptor_heaps();
    void create_textures_and_samplers();
    void create_geometry();
    void create_draw_items();
    void create_constant_buffers();
    void create_depth_buffer(std::uint32_t width, std::uint32_t height);
    void create_hdr_target(std::uint32_t width, std::uint32_t height);
    void create_bounds_geometry();

    void write_frame_constants(std::uint32_t frame_index, const RasterFrameConstants& constants);
    void write_light_constants(std::uint32_t frame_index, const RasterLightConstants& constants);
    void write_draw_constants(std::size_t slot, const RasterDrawConstants& constants);
    [[nodiscard]] RasterDrawConstants make_draw_constants(const PreparedRasterDraw& draw) const;
    [[nodiscard]] ID3D12PipelineState* pipeline_for(const PreparedRasterDraw& draw) const noexcept;

    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Resource> upload_buffer(
        const void* data,
        std::size_t byte_size,
        D3D12_RESOURCE_STATES final_state);
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Resource> upload_texture_rgba8(
        std::uint32_t width,
        std::uint32_t height,
        const std::byte* rgba8);

    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu_handle(std::uint32_t index) const noexcept;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE sampler_gpu_handle(std::uint32_t index) const noexcept;
    [[nodiscard]] std::uint32_t srv_index_for(const PreparedTextureBinding& binding, bool srgb) const;
    [[nodiscard]] std::uint32_t sampler_index_for(const PreparedTextureBinding& binding) const;

    D3D12Context& context_;
    ID3D12Device* device_ = nullptr;
    const CanonicalScene& scene_;
    DiagnosticMode mode_ = DiagnosticMode::shaded;
    float exposure_ev_ = 0.0F;
    OrbitCamera camera_;
    std::uint32_t viewport_width_ = 0;
    std::uint32_t viewport_height_ = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> raster_root_signature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> tone_root_signature_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<std::size_t>(PsoIndex::count)> raster_pipelines_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> line_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> tone_pipeline_;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sampler_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> hdr_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    std::uint32_t srv_increment_ = 0;
    std::uint32_t sampler_increment_ = 0;
    std::uint32_t hdr_srv_index_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> hdr_target_;
    Microsoft::WRL::ComPtr<ID3D12Resource> depth_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> frame_constant_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> light_constant_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> draw_constant_buffer_;
    std::byte* mapped_frame_constants_ = nullptr;
    std::byte* mapped_light_constants_ = nullptr;
    std::byte* mapped_draw_constants_ = nullptr;
    std::size_t frame_constant_stride_ = 256;
    std::size_t light_constant_stride_ = 0;
    std::size_t draw_constant_stride_ = 256;
    std::size_t draw_slots_per_frame_ = 1;

    std::vector<GpuPrimitive> primitives_;
    std::vector<PreparedRasterDraw> draw_items_;
    std::vector<PreparedPunctualLight> lights_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> textures_;
    std::vector<std::uint32_t> texture_linear_srv_indices_;
    std::vector<std::uint32_t> texture_srgb_srv_indices_;
    std::vector<std::uint32_t> texture_sampler_indices_;

    Microsoft::WRL::ComPtr<ID3D12Resource> bounds_vertex_buffer_;
    D3D12_VERTEX_BUFFER_VIEW bounds_vertex_view_{};
    std::uint32_t bounds_vertex_count_ = 0;
};
}
