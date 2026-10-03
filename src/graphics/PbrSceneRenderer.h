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
#include "rendering/CampaignC2Math.h"
#include "rendering/OrbitCamera.h"
#include "rendering/RasterPreparation.h"
#include "rendering/RasterShaderContract.h"
#include "scene/Scene.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace daedalus
{

struct GpuPassTimings
{
    bool valid = false;
    double shadow_ms = 0.0;
    double opaque_ms = 0.0;
    double transparent_ms = 0.0;
    double tone_map_ms = 0.0;
    double total_ms = 0.0;
};



struct RendererCameraState
{
    Vec3 eye{};
    Vec3 target{};
    float vertical_fov_degrees = 50.0F;
};

struct RendererResourceStats
{
    std::uint64_t vertex_buffer_count = 0;
    std::uint64_t index_buffer_count = 0;
    std::uint64_t texture_count = 0;
    std::uint64_t material_count = 0;
    std::uint64_t light_count = 0;
    std::uint64_t material_buffer_count = 0;
    std::uint64_t light_buffer_count = 0;
    std::uint64_t hdr_target_count = 0;
    std::uint64_t depth_target_count = 0;
    std::uint64_t shadow_map_count = 0;
    std::uint64_t diagnostic_resource_count = 0;
    std::uint64_t descriptor_count = 0;
    std::uint64_t logical_geometry_bytes = 0;
    std::uint64_t canonical_retained_bytes = 0;
    std::uint64_t committed_allocation_bytes = 0;
};

struct RendererCaptureResult
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::byte> rgba8;
    std::uint64_t hdr_nan_count = 0;
    std::uint64_t hdr_infinity_count = 0;
};

class PbrSceneRenderer final
{
public:
    PbrSceneRenderer(D3D12Context& context,
                     const CanonicalScene& scene,
                     DiagnosticMode mode,
                     float exposure_ev,
                     float environment_intensity,
                     bool shadows_enabled,
                     float shadow_constant_bias,
                     float shadow_normal_bias,
                     const std::filesystem::path& raster_vertex_shader,
                     const std::filesystem::path& raster_pixel_shader,
                     const std::filesystem::path& tone_vertex_shader,
                     const std::filesystem::path& tone_pixel_shader,
                     const std::filesystem::path& shadow_vertex_shader,
                     const std::filesystem::path& shadow_pixel_shader);
    ~PbrSceneRenderer();

    PbrSceneRenderer(const PbrSceneRenderer&) = delete;
    PbrSceneRenderer& operator=(const PbrSceneRenderer&) = delete;
    PbrSceneRenderer(PbrSceneRenderer&&) = delete;
    PbrSceneRenderer& operator=(PbrSceneRenderer&&) = delete;

    void record(const FrameRecordingContext& frame);
    void resize(std::uint32_t width, std::uint32_t height);
    void apply_input(const OrbitInput& input);
    void request_validation_capture(bool capture_ldr, bool validate_hdr);
    [[nodiscard]] RendererCaptureResult finalize_validation_capture();
    [[nodiscard]] GpuPassTimings gpu_timings() const noexcept;
    [[nodiscard]] RendererResourceStats resource_stats() const noexcept;
    [[nodiscard]] RendererCameraState camera_state() const noexcept;

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
    void create_shadow_resources();
    void create_pipeline_states(const std::filesystem::path& raster_vertex_shader,
                                const std::filesystem::path& raster_pixel_shader,
                                const std::filesystem::path& tone_vertex_shader,
                                const std::filesystem::path& tone_pixel_shader,
                                const std::filesystem::path& shadow_vertex_shader,
                                const std::filesystem::path& shadow_pixel_shader);
    void create_descriptor_heaps();
    void create_textures_and_samplers();
    void create_geometry();
    void create_draw_items();
    void create_constant_buffers();
    void create_depth_buffer(std::uint32_t width, std::uint32_t height);
    void create_hdr_target(std::uint32_t width, std::uint32_t height);
    void create_bounds_geometry();
    void prepare_capture_resources(bool capture_ldr, bool validate_hdr);
    void create_timing_resources();
    void consume_timing(std::uint32_t frame_index) noexcept;

    void write_frame_constants(std::uint32_t frame_index, const RasterFrameConstants& constants);
    void write_light_constants(std::uint32_t frame_index, const RasterLightConstants& constants);
    void write_draw_constants(std::size_t slot, const RasterDrawConstants& constants);
    void write_shadow_frame_constants(std::uint32_t frame_index, const ShadowFrameConstants& constants);
    void record_shadow_pass(ID3D12GraphicsCommandList* command_list, std::uint32_t frame_index, std::size_t frame_slot_base);
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
    float environment_intensity_ = 1.0F;
    bool shadows_enabled_ = true;
    ShadowProjection shadow_projection_{};
    OrbitCamera camera_;
    std::uint32_t viewport_width_ = 0;
    std::uint32_t viewport_height_ = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> raster_root_signature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> tone_root_signature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> shadow_root_signature_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<std::size_t>(PsoIndex::count)> raster_pipelines_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> line_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> tone_pipeline_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 4> shadow_pipelines_{};
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 4> overdraw_pipelines_{};

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sampler_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> hdr_rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    std::uint32_t srv_increment_ = 0;
    std::uint32_t sampler_increment_ = 0;
    std::uint32_t hdr_srv_index_ = 0;
    std::uint32_t shadow_srv_index_ = 0;
    std::uint32_t shadow_sampler_index_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> hdr_target_;
    Microsoft::WRL::ComPtr<ID3D12Resource> depth_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shadow_map_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shadow_dsv_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> frame_constant_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> light_constant_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> draw_constant_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shadow_frame_constant_buffer_;
    std::byte* mapped_frame_constants_ = nullptr;
    std::byte* mapped_light_constants_ = nullptr;
    std::byte* mapped_draw_constants_ = nullptr;
    std::byte* mapped_shadow_frame_constants_ = nullptr;
    std::size_t frame_constant_stride_ = 256;
    std::size_t light_constant_stride_ = 0;
    std::size_t draw_constant_stride_ = 256;
    std::size_t shadow_frame_constant_stride_ = 256;
    std::size_t draw_slots_per_frame_ = 1;

    std::vector<GpuPrimitive> primitives_;
    std::vector<PreparedRasterDraw> draw_items_;
    std::vector<PreparedPunctualLight> lights_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> textures_;
    std::vector<std::uint32_t> texture_linear_srv_indices_;
    std::vector<std::uint32_t> texture_srgb_srv_indices_;
    std::vector<std::uint32_t> texture_sampler_indices_;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestamp_query_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> timestamp_readback_;
    std::uint64_t* mapped_timestamps_ = nullptr;
    std::uint64_t timestamp_frequency_ = 0;
    std::array<bool, D3D12Context::kFrameCount> timestamp_frame_valid_{};
    GpuPassTimings latest_gpu_timings_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> ldr_capture_readback_;
    Microsoft::WRL::ComPtr<ID3D12Resource> hdr_capture_readback_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT ldr_capture_footprint_{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT hdr_capture_footprint_{};
    std::uint64_t ldr_capture_size_ = 0;
    std::uint64_t hdr_capture_size_ = 0;
    bool capture_ldr_requested_ = false;
    bool validate_hdr_requested_ = false;
    bool capture_recorded_ = false;

    Microsoft::WRL::ComPtr<ID3D12Resource> bounds_vertex_buffer_;
    D3D12_VERTEX_BUFFER_VIEW bounds_vertex_view_{};
    std::uint32_t bounds_vertex_count_ = 0;
};
}
