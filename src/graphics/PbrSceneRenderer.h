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
#include "graphics/D3D12Helpers.h"
#include "graphics/GpuSceneResources.h"
#include "graphics/GpuTimer.h"
#include "rendering/CampaignC2Math.h"
#include "rendering/RasterPreparation.h"
#include "rendering/RasterShaderContract.h"
#include "rendering/TemporalContract.h"
#include "scene/Scene.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace daedalus
{
struct RasterFrameInputs
{
    std::uint32_t frame_index = 0;
    Mat4 view_projection = identity_matrix(); // jittered; used for SV_Position
    Vec3 camera_position{};
    D3D12_GPU_VIRTUAL_ADDRESS view_constants = 0; // GpuViewConstants (raster b4)
    std::span<const Mat4> draw_world;             // per prepared draw
    std::span<const Mat4> previous_draw_world;    // per prepared draw
    float emissive_scale = 1.0F;
    bool geometry_buffer = false;  // opaque pass writes the G-buffer MRT set
    bool hybrid_composite = false; // eligible opaque draws defer direct/specular terms to DXR
};

// Campaign C2 forward PBR raster passes (shadow map, opaque, transparent, bounds overlay), now
// drawing from GpuSceneResources. Output presentation (tone map, capture) moved to DisplayOutput;
// the raster path itself keeps the C2 shading, ordering and pipeline-state policy unchanged.
class PbrSceneRenderer final
{
public:
    PbrSceneRenderer(D3D12Context& context,
                     GpuSceneResources& resources,
                     const CanonicalScene& scene,
                     DiagnosticMode mode,
                     float environment_intensity,
                     bool shadows_enabled,
                     float shadow_constant_bias,
                     float shadow_normal_bias,
                     const std::filesystem::path& shader_directory,
                     std::uint32_t render_width,
                     std::uint32_t render_height);
    ~PbrSceneRenderer();

    PbrSceneRenderer(const PbrSceneRenderer&) = delete;
    PbrSceneRenderer& operator=(const PbrSceneRenderer&) = delete;
    PbrSceneRenderer(PbrSceneRenderer&&) = delete;
    PbrSceneRenderer& operator=(PbrSceneRenderer&&) = delete;

    // Caller must prove GPU idle before resizing or toggling the geometry buffer.
    void resize(std::uint32_t width, std::uint32_t height);
    void set_geometry_buffer_enabled(bool enabled);

    // Writes this frame slot's raster frame constants and the shared punctual-light constants
    // (also consumed by the DXR passes). record_opaque calls it; path tracing calls it directly.
    void update_frame_constants(const RasterFrameInputs& inputs);
    // Shadow pass + OPAQUE/MASK draws (+ bounds overlay) into the raster HDR target (and G-buffer).
    void record_opaque(ID3D12GraphicsCommandList* command_list, const RasterFrameInputs& inputs, GpuTimer& timer);
    // BLEND draws, far-to-near, onto `target` with depth testing against the opaque depth buffer.
    void record_transparent(ID3D12GraphicsCommandList* command_list, const RasterFrameInputs& inputs, GpuTexture& target);

    [[nodiscard]] GpuTexture& hdr_target() noexcept { return hdr_target_; }
    [[nodiscard]] std::array<GpuTexture, kGBufferCount>& geometry_buffer() noexcept { return geometry_buffer_; }
    [[nodiscard]] bool geometry_buffer_enabled() const noexcept { return geometry_buffer_enabled_; }
    [[nodiscard]] ID3D12Resource* depth_buffer() const noexcept { return depth_buffer_.Get(); }
    [[nodiscard]] const std::vector<PreparedRasterDraw>& draws() const noexcept { return draw_items_; }
    [[nodiscard]] const std::vector<PreparedPunctualLight>& lights() const noexcept { return lights_; }
    [[nodiscard]] const ShadowProjection& shadow_projection() const noexcept { return shadow_projection_; }
    [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS light_constants_address(std::uint32_t frame_index) const;
    [[nodiscard]] std::uint64_t committed_allocation_bytes() const noexcept;
    [[nodiscard]] std::uint64_t render_target_count() const noexcept;

private:
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
    void create_pipeline_states(const std::filesystem::path& shader_directory);
    void create_rtv_dsv_heaps();
    void create_draw_items();
    void create_shadow_resources();
    void create_constant_buffers();
    void create_depth_buffer(std::uint32_t width, std::uint32_t height);
    void create_hdr_target(std::uint32_t width, std::uint32_t height);
    void create_geometry_buffer(std::uint32_t width, std::uint32_t height);
    void create_bounds_geometry();

    void write_draw_constants(std::size_t slot, const RasterDrawConstants& constants);
    void write_temporal_draw_constants(std::size_t slot, const RasterTemporalDrawConstants& constants);
    void record_shadow_pass(ID3D12GraphicsCommandList* command_list, const RasterFrameInputs& inputs, std::size_t frame_slot_base);
    void record_draw(ID3D12GraphicsCommandList* command_list, const RasterFrameInputs& inputs, std::size_t order_index,
                     std::size_t frame_slot_base, bool geometry_buffer_pass);
    void bind_raster_root(ID3D12GraphicsCommandList* command_list, const RasterFrameInputs& inputs);
    [[nodiscard]] RasterDrawConstants make_draw_constants(const PreparedRasterDraw& draw, const Mat4& world, float emissive_scale) const;
    [[nodiscard]] ID3D12PipelineState* pipeline_for(const PreparedRasterDraw& draw, bool geometry_buffer_pass) const noexcept;
    [[nodiscard]] std::uint32_t srv_index_for(const PreparedTextureBinding& binding, bool srgb) const;
    [[nodiscard]] std::uint32_t sampler_index_for(const PreparedTextureBinding& binding) const;

    D3D12Context& context_;
    GpuSceneResources& resources_;
    ID3D12Device* device_ = nullptr;
    const CanonicalScene& scene_;
    DiagnosticMode mode_ = DiagnosticMode::shaded;
    float environment_intensity_ = 1.0F;
    bool shadows_enabled_ = true;
    ShadowProjection shadow_projection_{};
    std::uint32_t viewport_width_ = 0;
    std::uint32_t viewport_height_ = 0;
    bool geometry_buffer_enabled_ = false;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> raster_root_signature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> shadow_root_signature_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<std::size_t>(PsoIndex::count)> raster_pipelines_{};
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 4> geometry_buffer_pipelines_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> line_pipeline_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 4> shadow_pipelines_{};
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 4> overdraw_pipelines_{};

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shadow_dsv_heap_;
    std::uint32_t rtv_increment_ = 0;
    std::uint32_t hdr_srv_index_ = 0;
    std::uint32_t shadow_srv_index_ = 0;
    std::uint32_t geometry_buffer_srv_base_ = 0;

    GpuTexture hdr_target_;
    std::array<GpuTexture, kGBufferCount> geometry_buffer_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> depth_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shadow_map_;
    Microsoft::WRL::ComPtr<ID3D12Resource> frame_constant_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> light_constant_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> draw_constant_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> temporal_draw_constant_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shadow_frame_constant_buffer_;
    std::byte* mapped_frame_constants_ = nullptr;
    std::byte* mapped_light_constants_ = nullptr;
    std::byte* mapped_draw_constants_ = nullptr;
    std::byte* mapped_temporal_draw_constants_ = nullptr;
    std::byte* mapped_shadow_frame_constants_ = nullptr;
    std::size_t frame_constant_stride_ = 256;
    std::size_t light_constant_stride_ = 0;
    std::size_t draw_constant_stride_ = 256;
    std::size_t temporal_draw_constant_stride_ = 256;
    std::size_t shadow_frame_constant_stride_ = 256;
    std::size_t draw_slots_per_frame_ = 1;
    std::size_t temporal_slots_per_frame_ = 1;

    std::vector<PreparedRasterDraw> draw_items_;
    std::vector<PreparedPunctualLight> lights_;
    std::vector<std::size_t> draw_order_;

    Microsoft::WRL::ComPtr<ID3D12Resource> bounds_vertex_buffer_;
    D3D12_VERTEX_BUFFER_VIEW bounds_vertex_view_{};
    std::uint32_t bounds_vertex_count_ = 0;
};
}
