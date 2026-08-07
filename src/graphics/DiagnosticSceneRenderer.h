#pragma once

#ifndef _WIN32
#error DiagnosticSceneRenderer is available only on Windows.
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "core/CommandLine.h"
#include "graphics/D3D12Context.h"
#include "rendering/DiagnosticPreparation.h"
#include "rendering/OrbitCamera.h"
#include "scene/Scene.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace daedalus
{
class DiagnosticSceneRenderer final
{
public:
    DiagnosticSceneRenderer(D3D12Context& context,
                            const CanonicalScene& scene,
                            DiagnosticMode mode,
                            const std::filesystem::path& vertex_shader,
                            const std::filesystem::path& pixel_shader);
    ~DiagnosticSceneRenderer();

    DiagnosticSceneRenderer(const DiagnosticSceneRenderer&) = delete;
    DiagnosticSceneRenderer& operator=(const DiagnosticSceneRenderer&) = delete;
    DiagnosticSceneRenderer(DiagnosticSceneRenderer&&) = delete;
    DiagnosticSceneRenderer& operator=(DiagnosticSceneRenderer&&) = delete;

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
        MaterialId material;
        bool has_texcoord0 = false;
        bool has_texcoord1 = false;
        bool has_vertex_colors = false;
    };

    struct alignas(16) DrawConstants
    {
        Mat4 world_view_projection{};
        Mat4 world{};
        Mat4 normal_matrix{};
        Vec4 base_color_factor{};
        std::uint32_t diagnostic_mode = 0;
        std::uint32_t has_texture = 0;
        std::uint32_t use_vertex_color = 0;
        std::uint32_t texture_coord_set = 0;
        float world_handedness = 1.0F;
        std::uint32_t padding0 = 0;
        std::uint32_t padding1 = 0;
    };

    void create_root_signature();
    void create_pipeline_states(const std::filesystem::path& vertex_shader,
                                const std::filesystem::path& pixel_shader);
    void create_descriptor_heaps();
    void create_textures();
    void create_geometry();
    void create_draw_items();
    void create_constant_buffer();
    void create_depth_buffer(std::uint32_t width, std::uint32_t height);
    void create_bounds_geometry();
    void write_constants(std::size_t index, const DrawConstants& constants);

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

    D3D12Context& context_;
    ID3D12Device* device_ = nullptr;
    const CanonicalScene& scene_;
    DiagnosticMode mode_ = DiagnosticMode::shaded;
    OrbitCamera camera_;
    std::uint32_t viewport_width_ = 0;
    std::uint32_t viewport_height_ = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> triangle_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> line_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sampler_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> depth_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constant_buffer_;
    std::byte* mapped_constants_ = nullptr;
    std::uint32_t srv_increment_ = 0;
    std::uint32_t sampler_increment_ = 0;
    std::size_t constant_stride_ = 256;
    std::size_t draw_slots_per_frame_ = 1;

    std::vector<GpuPrimitive> primitives_;
    std::vector<PreparedDiagnosticDraw> draw_items_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> textures_;
    std::vector<std::uint32_t> texture_linear_srv_indices_;
    std::vector<std::uint32_t> texture_srgb_srv_indices_;
    std::vector<std::uint32_t> texture_sampler_indices_;
    Microsoft::WRL::ComPtr<ID3D12Resource> bounds_vertex_buffer_;
    D3D12_VERTEX_BUFFER_VIEW bounds_vertex_view_{};
    std::uint32_t bounds_vertex_count_ = 0;
};
}
