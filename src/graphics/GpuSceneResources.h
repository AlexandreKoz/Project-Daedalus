#pragma once

#ifndef _WIN32
#error GpuSceneResources is available only on Windows.
#endif

#include "graphics/D3D12Context.h"
#include "graphics/D3D12Helpers.h"
#include "rendering/GpuSceneLayout.h"
#include "scene/Scene.h"

#include <cstdint>
#include <span>
#include <vector>

namespace daedalus
{
// Shared GPU-side scene ownership boundary (Campaign D). Raster, hybrid DXR, the path tracer and
// temporal passes all reference these geometry buffers, textures and descriptor heaps instead of
// uploading private copies. The canonical scene remains the authoritative CPU data; nothing here
// is written back into it.
//
// Resource states (fixed for the object's lifetime):
//   vertex buffers : VERTEX_AND_CONSTANT_BUFFER | NON_PIXEL_SHADER_RESOURCE (IA + BLAS input + raw SRV)
//   index buffers  : INDEX_BUFFER | NON_PIXEL_SHADER_RESOURCE                (IA + BLAS input + raw SRV)
//   textures       : PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE       (raster PS + DXR/compute)
// Lifetime: callers must prove GPU idle (D3D12Context::wait_for_gpu) before destroying an instance;
// Application does so on reload, and D3D12Context::prepare_for_shutdown on exit.
class GpuSceneResources final
{
public:
    struct GpuPrimitive
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> index_buffer;
        D3D12_VERTEX_BUFFER_VIEW vertex_view{};
        D3D12_INDEX_BUFFER_VIEW index_view{};
        std::uint32_t vertex_count = 0;
        std::uint32_t index_count = 0;
    };

    static constexpr D3D12_RESOURCE_STATES kVertexBufferState =
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    static constexpr D3D12_RESOURCE_STATES kIndexBufferState =
        D3D12_RESOURCE_STATE_INDEX_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    static constexpr D3D12_RESOURCE_STATES kTextureState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    GpuSceneResources(D3D12Context& context, const CanonicalScene& scene);
    ~GpuSceneResources() = default;

    GpuSceneResources(const GpuSceneResources&) = delete;
    GpuSceneResources& operator=(const GpuSceneResources&) = delete;
    GpuSceneResources(GpuSceneResources&&) = delete;
    GpuSceneResources& operator=(GpuSceneResources&&) = delete;

    [[nodiscard]] const GpuSceneDescriptorLayout& layout() const noexcept { return layout_; }
    [[nodiscard]] ID3D12DescriptorHeap* srv_heap() const noexcept { return srv_heap_.Get(); }
    [[nodiscard]] ID3D12DescriptorHeap* sampler_heap() const noexcept { return sampler_heap_.Get(); }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu(std::uint32_t index) const;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu(std::uint32_t index) const;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE sampler_gpu(std::uint32_t index) const;
    [[nodiscard]] std::uint32_t block_base(DescriptorBlock block) const;

    void write_texture_srv(std::uint32_t index, ID3D12Resource* resource, DXGI_FORMAT format) const;
    void write_texture_uav(std::uint32_t index, ID3D12Resource* resource, DXGI_FORMAT format) const;
    void write_null_texture_srv(std::uint32_t index) const;
    void write_null_texture_uav(std::uint32_t index) const;
    void write_structured_buffer_srv(std::uint32_t index, ID3D12Resource* resource, std::uint32_t element_count,
                                     std::uint32_t element_stride) const;
    void write_raw_buffer_srv(std::uint32_t index, ID3D12Resource* resource, std::uint64_t byte_size) const;
    void write_raw_buffer_uav(std::uint32_t index, ID3D12Resource* resource, std::uint64_t byte_size) const;

    [[nodiscard]] const std::vector<GpuPrimitive>& primitives() const noexcept { return primitives_; }
    [[nodiscard]] std::size_t texture_resource_count() const noexcept { return textures_.size(); }

    // Synchronous default-heap upload (load/recreation paths only; waits for the GPU).
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Resource> upload_buffer(const void* data,
                                                                      std::uint64_t byte_size,
                                                                      D3D12_RESOURCE_STATES final_state,
                                                                      D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
                                                                      std::wstring_view debug_name = {});

    [[nodiscard]] std::uint64_t committed_allocation_bytes() const noexcept;
    [[nodiscard]] std::uint64_t descriptor_count() const noexcept;

private:
    class UploadBatch;

    void create_heaps();
    void create_textures(UploadBatch& batch);
    void create_samplers();
    void create_geometry(UploadBatch& batch);
    void check_srv_index(std::uint32_t index) const;

    D3D12Context& context_;
    ID3D12Device* device_ = nullptr;
    const CanonicalScene& scene_;
    GpuSceneDescriptorLayout layout_{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sampler_heap_;
    std::uint32_t srv_increment_ = 0;
    std::uint32_t sampler_increment_ = 0;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> textures_;
    std::vector<GpuPrimitive> primitives_;
};
}
