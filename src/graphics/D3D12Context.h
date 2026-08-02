#pragma once

#ifndef _WIN32
#error D3D12Context is available only on Windows.
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "platform/UniqueWin32Handle.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace daedalus
{
struct FrameRecordingContext
{
    ID3D12GraphicsCommandList* command_list = nullptr;
    ID3D12Resource* back_buffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE render_target{};
    std::uint32_t frame_index = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

class D3D12Context final
{
public:
    static constexpr std::uint32_t kFrameCount = 2;
    static constexpr DXGI_FORMAT kRenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12Context(HWND window, std::uint32_t width, std::uint32_t height, bool use_warp);
    ~D3D12Context();

    D3D12Context(const D3D12Context&) = delete;
    D3D12Context& operator=(const D3D12Context&) = delete;
    D3D12Context(D3D12Context&&) = delete;
    D3D12Context& operator=(D3D12Context&&) = delete;

    [[nodiscard]] FrameRecordingContext begin_frame();
    void end_frame();
    void resize(std::uint32_t width, std::uint32_t height);
    void wait_for_gpu();
    void execute_immediate(const std::function<void(ID3D12GraphicsCommandList*)>& record);
    [[nodiscard]] bool prepare_for_shutdown() noexcept;
    void shutdown() noexcept;

    [[nodiscard]] ID3D12Device* device() const noexcept;
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] const std::string& adapter_name() const noexcept;
    [[nodiscard]] std::uint64_t dedicated_video_memory() const noexcept;
    [[nodiscard]] D3D_FEATURE_LEVEL feature_level() const noexcept;
    [[nodiscard]] std::string feature_level_name() const;
    [[nodiscard]] bool debug_layer_enabled() const noexcept;
    [[nodiscard]] bool using_warp() const noexcept;

private:
    struct FrameResource
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> back_buffer;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator;
        std::uint64_t fence_value = 0;
    };

    void enable_debug_layer();
    void create_factory();
    void select_adapter();
    void create_device();
    void configure_information_queue();
    void create_command_objects();
    void create_swap_chain();
    void create_render_targets();
    void wait_for_fence(std::uint64_t value);
    [[nodiscard]] bool try_wait_for_gpu() noexcept;
    void abandon_resources() noexcept;
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle(std::uint32_t index) const noexcept;

    HWND window_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool request_warp_ = false;
    bool using_warp_ = false;
    bool debug_layer_enabled_ = false;
    bool shutdown_complete_ = false;
    bool gpu_idle_proven_ = true;
    UINT factory_flags_ = 0;
    UINT rtv_descriptor_size_ = 0;
    std::uint32_t current_frame_index_ = 0;
    std::uint64_t next_fence_value_ = 1;
    UniqueWin32Handle fence_event_;
    std::string adapter_name_;
    std::uint64_t dedicated_video_memory_ = 0;
    D3D_FEATURE_LEVEL feature_level_ = D3D_FEATURE_LEVEL_11_0;

    Microsoft::WRL::ComPtr<IDXGIFactory4> factory_;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    std::array<FrameResource, kFrameCount> frames_{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
};
}
