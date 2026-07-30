#pragma once

#ifndef _WIN32
#error TriangleRenderer is available only on Windows.
#endif

#include "graphics/D3D12Context.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>

namespace daedalus
{
class TriangleRenderer final
{
public:
    TriangleRenderer(
        ID3D12Device* device,
        const std::filesystem::path& vertex_shader_path,
        const std::filesystem::path& pixel_shader_path);

    TriangleRenderer(const TriangleRenderer&) = delete;
    TriangleRenderer& operator=(const TriangleRenderer&) = delete;
    TriangleRenderer(TriangleRenderer&&) = delete;
    TriangleRenderer& operator=(TriangleRenderer&&) = delete;

    void record(const FrameRecordingContext& frame) const;

private:
    struct Vertex
    {
        float position[3];
        float color[4];
    };

    void create_root_signature(ID3D12Device* device);
    void create_pipeline(
        ID3D12Device* device,
        const std::filesystem::path& vertex_shader_path,
        const std::filesystem::path& pixel_shader_path);
    void create_vertex_buffer(ID3D12Device* device);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer_;
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view_{};
};
}
