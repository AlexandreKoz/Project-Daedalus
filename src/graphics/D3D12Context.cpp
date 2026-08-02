#include "graphics/D3D12Context.h"

#include "core/Error.h"
#include "core/Log.h"
#include "graphics/AdapterPolicy.h"

#include <algorithm>
#include <array>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace daedalus
{
namespace
{
[[nodiscard]] std::string utf8_from_wide(const wchar_t* value)
{
    if (value == nullptr || *value == L'\0')
    {
        return {};
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1)
    {
        return {};
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr, nullptr);
    result.pop_back();
    return result;
}

[[nodiscard]] std::string bytes_as_mib(std::uint64_t bytes)
{
    std::ostringstream stream;
    stream << (bytes / (1024ULL * 1024ULL)) << " MiB";
    return stream.str();
}
}

D3D12Context::D3D12Context(HWND window, std::uint32_t width, std::uint32_t height, bool use_warp)
    : window_(window), width_(width), height_(height), request_warp_(use_warp)
{
    if (window_ == nullptr || width_ == 0 || height_ == 0)
    {
        throw std::invalid_argument("D3D12Context requires a valid window and nonzero dimensions");
    }

    enable_debug_layer();
    create_factory();
    select_adapter();
    create_device();
    configure_information_queue();
    create_command_objects();
    create_swap_chain();
    create_render_targets();
}

D3D12Context::~D3D12Context()
{
    shutdown();
}

FrameRecordingContext D3D12Context::begin_frame()
{
    FrameResource& frame = frames_.at(current_frame_index_);
    if (frame.fence_value != 0 && fence_->GetCompletedValue() < frame.fence_value)
    {
        wait_for_fence(frame.fence_value);
    }

    DAEDALUS_THROW_IF_FAILED(frame.command_allocator->Reset());
    DAEDALUS_THROW_IF_FAILED(command_list_->Reset(frame.command_allocator.Get(), nullptr));

    return FrameRecordingContext{
        .command_list = command_list_.Get(),
        .back_buffer = frame.back_buffer.Get(),
        .render_target = rtv_handle(current_frame_index_),
        .frame_index = current_frame_index_,
        .width = width_,
        .height = height_};
}

void D3D12Context::end_frame()
{
    DAEDALUS_THROW_IF_FAILED(command_list_->Close());
    ID3D12CommandList* command_lists[] = {command_list_.Get()};
    command_queue_->ExecuteCommandLists(1, command_lists);
    gpu_idle_proven_ = false;

    const HRESULT present_result = swap_chain_->Present(1, 0);
    if (FAILED(present_result))
    {
        const HRESULT removed_reason = device_->GetDeviceRemovedReason();
        if (FAILED(removed_reason))
        {
            throw ResultError(static_cast<ResultCode>(removed_reason), "ID3D12Device::GetDeviceRemovedReason after Present");
        }
        throw ResultError(static_cast<ResultCode>(present_result), "IDXGISwapChain::Present");
    }

    const std::uint64_t signal_value = next_fence_value_++;
    DAEDALUS_THROW_IF_FAILED(command_queue_->Signal(fence_.Get(), signal_value));
    frames_.at(current_frame_index_).fence_value = signal_value;
    current_frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
}

void D3D12Context::resize(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0 || (width == width_ && height == height_))
    {
        return;
    }

    wait_for_gpu();
    for (FrameResource& frame : frames_)
    {
        frame.back_buffer.Reset();
        frame.fence_value = 0;
    }

    const DXGI_SWAP_CHAIN_DESC description = [&]() {
        DXGI_SWAP_CHAIN_DESC value{};
        DAEDALUS_THROW_IF_FAILED(swap_chain_->GetDesc(&value));
        return value;
    }();

    DAEDALUS_THROW_IF_FAILED(
        swap_chain_->ResizeBuffers(kFrameCount, width, height, kRenderTargetFormat, description.Flags));

    width_ = width;
    height_ = height;
    current_frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
    create_render_targets();

    std::ostringstream stream;
    stream << "Swap chain resized to " << width_ << 'x' << height_;
    Log::info(stream.str());
}

void D3D12Context::execute_immediate(const std::function<void(ID3D12GraphicsCommandList*)>& record)
{
    if (!record)
    {
        throw std::invalid_argument("execute_immediate requires a recording callback");
    }

    wait_for_gpu();
    FrameResource& frame = frames_.at(current_frame_index_);
    DAEDALUS_THROW_IF_FAILED(frame.command_allocator->Reset());
    DAEDALUS_THROW_IF_FAILED(command_list_->Reset(frame.command_allocator.Get(), nullptr));
    record(command_list_.Get());
    DAEDALUS_THROW_IF_FAILED(command_list_->Close());
    ID3D12CommandList* command_lists[] = {command_list_.Get()};
    command_queue_->ExecuteCommandLists(1, command_lists);
    gpu_idle_proven_ = false;

    const std::uint64_t signal_value = next_fence_value_++;
    DAEDALUS_THROW_IF_FAILED(command_queue_->Signal(fence_.Get(), signal_value));
    wait_for_fence(signal_value);
    for (FrameResource& resource : frames_)
    {
        resource.fence_value = 0;
    }
    gpu_idle_proven_ = true;
}

void D3D12Context::wait_for_gpu()
{
    if (gpu_idle_proven_ || command_queue_ == nullptr || fence_ == nullptr || !fence_event_)
    {
        return;
    }

    const std::uint64_t signal_value = next_fence_value_++;
    DAEDALUS_THROW_IF_FAILED(command_queue_->Signal(fence_.Get(), signal_value));
    wait_for_fence(signal_value);
    for (FrameResource& frame : frames_)
    {
        frame.fence_value = 0;
    }
    gpu_idle_proven_ = true;
}

bool D3D12Context::prepare_for_shutdown() noexcept
{
    if (try_wait_for_gpu())
    {
        return true;
    }

    Log::warning("GPU idle could not be proven before renderer teardown.");
    return false;
}

void D3D12Context::shutdown() noexcept
{
    if (shutdown_complete_)
    {
        return;
    }
    shutdown_complete_ = true;

    if (!try_wait_for_gpu())
    {
        Log::error(
            "GPU idle could not be proven during final shutdown. D3D12 resources are intentionally being retained "
            "until process exit.");
        abandon_resources();
        return;
    }

    command_list_.Reset();
    for (FrameResource& frame : frames_)
    {
        frame.back_buffer.Reset();
        frame.command_allocator.Reset();
        frame.fence_value = 0;
    }
    rtv_heap_.Reset();
    swap_chain_.Reset();
    command_queue_.Reset();
    fence_.Reset();
    fence_event_.reset();
    device_.Reset();
    adapter_.Reset();
    factory_.Reset();
}

ID3D12Device* D3D12Context::device() const noexcept
{
    return device_.Get();
}

std::uint32_t D3D12Context::width() const noexcept
{
    return width_;
}

std::uint32_t D3D12Context::height() const noexcept
{
    return height_;
}

const std::string& D3D12Context::adapter_name() const noexcept
{
    return adapter_name_;
}

std::uint64_t D3D12Context::dedicated_video_memory() const noexcept
{
    return dedicated_video_memory_;
}

D3D_FEATURE_LEVEL D3D12Context::feature_level() const noexcept
{
    return feature_level_;
}

std::string D3D12Context::feature_level_name() const
{
    switch (feature_level_)
    {
    case D3D_FEATURE_LEVEL_12_1:
        return "12.1";
    case D3D_FEATURE_LEVEL_12_0:
        return "12.0";
    case D3D_FEATURE_LEVEL_11_1:
        return "11.1";
    case D3D_FEATURE_LEVEL_11_0:
        return "11.0";
    default:
        return "unknown";
    }
}

bool D3D12Context::debug_layer_enabled() const noexcept
{
    return debug_layer_enabled_;
}

bool D3D12Context::using_warp() const noexcept
{
    return using_warp_;
}

void D3D12Context::enable_debug_layer()
{
#if defined(DAEDALUS_DEBUG_BUILD)
    Microsoft::WRL::ComPtr<ID3D12Debug> debug_controller;
    const HRESULT result = D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller));
    if (SUCCEEDED(result))
    {
        debug_controller->EnableDebugLayer();
        debug_layer_enabled_ = true;
        factory_flags_ |= DXGI_CREATE_FACTORY_DEBUG;
        Log::info("Direct3D 12 debug layer enabled");
    }
    else
    {
        Log::warning(
            "Direct3D 12 debug layer is unavailable. Install the Windows Graphics Tools optional feature for validation. "
            "Continuing without the debug layer; HRESULT: " +
            format_result_code(static_cast<ResultCode>(result)));
    }
#else
    Log::info("Direct3D 12 debug layer not requested in this build configuration");
#endif
}

void D3D12Context::create_factory()
{
    HRESULT result = CreateDXGIFactory2(factory_flags_, IID_PPV_ARGS(&factory_));
    if (FAILED(result) && factory_flags_ != 0)
    {
        Log::warning(
            "DXGI debug factory creation failed; retrying without DXGI factory debugging. HRESULT: " +
            format_result_code(static_cast<ResultCode>(result)));
        factory_flags_ = 0;
        result = CreateDXGIFactory2(factory_flags_, IID_PPV_ARGS(&factory_));
    }
    throw_if_failed(static_cast<ResultCode>(result), "CreateDXGIFactory2");
}

void D3D12Context::select_adapter()
{
    if (request_warp_)
    {
        DAEDALUS_THROW_IF_FAILED(factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter_)));
        using_warp_ = true;
    }
    else
    {
        std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter1>> adapters;
        std::vector<AdapterCandidate> candidates;

        Microsoft::WRL::ComPtr<IDXGIFactory6> factory6;
        const bool modern_enumeration = SUCCEEDED(factory_.As(&factory6));
        for (UINT index = 0;; ++index)
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate_adapter;
            HRESULT result = DXGI_ERROR_NOT_FOUND;
            if (modern_enumeration)
            {
                result = factory6->EnumAdapterByGpuPreference(
                    index,
                    DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(&candidate_adapter));
            }
            else
            {
                result = factory_->EnumAdapters1(index, &candidate_adapter);
            }

            if (result == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }
            DAEDALUS_THROW_IF_FAILED(result);

            DXGI_ADAPTER_DESC1 description{};
            DAEDALUS_THROW_IF_FAILED(candidate_adapter->GetDesc1(&description));
            const bool software = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            const bool supported = SUCCEEDED(
                D3D12CreateDevice(candidate_adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr));

            adapters.push_back(candidate_adapter);
            candidates.push_back(AdapterCandidate{
                .supports_d3d12 = supported,
                .software = software,
                .dedicated_video_memory = static_cast<std::uint64_t>(description.DedicatedVideoMemory),
                .enumeration_order = index});
        }

        try
        {
            adapter_ = adapters.at(choose_adapter_index(candidates));
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(
                std::string(error.what()) + ". Hardware selection does not fall back silently; run with --warp for diagnostics.");
        }
    }

    DXGI_ADAPTER_DESC1 description{};
    DAEDALUS_THROW_IF_FAILED(adapter_->GetDesc1(&description));
    adapter_name_ = utf8_from_wide(description.Description);
    dedicated_video_memory_ = static_cast<std::uint64_t>(description.DedicatedVideoMemory);

    std::ostringstream stream;
    stream << "Selected adapter: " << adapter_name_ << " | dedicated video memory: "
           << bytes_as_mib(dedicated_video_memory_) << " | mode: " << (using_warp_ ? "WARP" : "hardware");
    Log::info(stream.str());
}

void D3D12Context::create_device()
{
    constexpr std::array levels{
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0};

    HRESULT last_result = E_FAIL;
    for (const D3D_FEATURE_LEVEL level : levels)
    {
        Microsoft::WRL::ComPtr<ID3D12Device> candidate;
        const HRESULT result = D3D12CreateDevice(adapter_.Get(), level, IID_PPV_ARGS(&candidate));
        if (SUCCEEDED(result))
        {
            device_ = std::move(candidate);
            feature_level_ = level;
            Log::info("Direct3D 12 device created at feature level " + feature_level_name());
            return;
        }
        last_result = result;
    }

    throw ResultError(static_cast<ResultCode>(last_result), "D3D12CreateDevice for all supported feature levels");
}

void D3D12Context::configure_information_queue()
{
#if defined(DAEDALUS_DEBUG_BUILD)
    if (!debug_layer_enabled_)
    {
        return;
    }

    Microsoft::WRL::ComPtr<ID3D12InfoQueue> information_queue;
    if (SUCCEEDED(device_.As(&information_queue)))
    {
        if (IsDebuggerPresent() != FALSE)
        {
            DAEDALUS_THROW_IF_FAILED(
                information_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
            DAEDALUS_THROW_IF_FAILED(information_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
        }
        Log::info("Direct3D 12 information queue configured without blanket message suppression");
    }
#endif
}

void D3D12Context::create_command_objects()
{
    D3D12_COMMAND_QUEUE_DESC queue_description{};
    queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_description.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queue_description.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    DAEDALUS_THROW_IF_FAILED(device_->CreateCommandQueue(&queue_description, IID_PPV_ARGS(&command_queue_)));

    for (FrameResource& frame : frames_)
    {
        DAEDALUS_THROW_IF_FAILED(
            device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.command_allocator)));
    }

    DAEDALUS_THROW_IF_FAILED(device_->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        frames_[0].command_allocator.Get(),
        nullptr,
        IID_PPV_ARGS(&command_list_)));
    DAEDALUS_THROW_IF_FAILED(command_list_->Close());

    DAEDALUS_THROW_IF_FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)));
    UniqueWin32Handle event(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!event)
    {
        throw ResultError(static_cast<ResultCode>(HRESULT_FROM_WIN32(GetLastError())), "CreateEventW for D3D12 fence");
    }
    fence_event_ = std::move(event);
}

void D3D12Context::create_swap_chain()
{
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = width_;
    description.Height = height_;
    description.Format = kRenderTargetFormat;
    description.Stereo = FALSE;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = kFrameCount;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    description.Flags = 0;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain1;
    DAEDALUS_THROW_IF_FAILED(factory_->CreateSwapChainForHwnd(
        command_queue_.Get(), window_, &description, nullptr, nullptr, &swap_chain1));
    DAEDALUS_THROW_IF_FAILED(factory_->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER));
    DAEDALUS_THROW_IF_FAILED(swap_chain1.As(&swap_chain_));
    current_frame_index_ = swap_chain_->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC heap_description{};
    heap_description.NumDescriptors = kFrameCount;
    heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    DAEDALUS_THROW_IF_FAILED(device_->CreateDescriptorHeap(&heap_description, IID_PPV_ARGS(&rtv_heap_)));
    rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    Log::info("Swap chain created with 2 flip-discard buffers and vertical synchronization enabled");
}

void D3D12Context::create_render_targets()
{
    for (std::uint32_t index = 0; index < kFrameCount; ++index)
    {
        DAEDALUS_THROW_IF_FAILED(swap_chain_->GetBuffer(index, IID_PPV_ARGS(&frames_[index].back_buffer)));
        device_->CreateRenderTargetView(frames_[index].back_buffer.Get(), nullptr, rtv_handle(index));
    }
}

void D3D12Context::wait_for_fence(std::uint64_t value)
{
    if (fence_->GetCompletedValue() >= value)
    {
        return;
    }

    DAEDALUS_THROW_IF_FAILED(fence_->SetEventOnCompletion(value, fence_event_.get()));
    const DWORD wait_result = WaitForSingleObject(fence_event_.get(), INFINITE);
    if (wait_result == WAIT_FAILED)
    {
        throw ResultError(
            static_cast<ResultCode>(HRESULT_FROM_WIN32(GetLastError())), "WaitForSingleObject for D3D12 fence");
    }
    if (wait_result != WAIT_OBJECT_0)
    {
        std::ostringstream stream;
        stream << "WaitForSingleObject returned unexpected status " << wait_result << " for the D3D12 fence";
        throw std::runtime_error(stream.str());
    }
}

bool D3D12Context::try_wait_for_gpu() noexcept
{
    if (gpu_idle_proven_ || command_queue_ == nullptr || fence_ == nullptr || !fence_event_)
    {
        return true;
    }

    const std::uint64_t signal_value = next_fence_value_++;
    const HRESULT signal_result = command_queue_->Signal(fence_.Get(), signal_value);
    if (FAILED(signal_result))
    {
        Log::error(format_failure_message(
            static_cast<ResultCode>(signal_result), "ID3D12CommandQueue::Signal during shutdown"));
        return false;
    }

    if (fence_->GetCompletedValue() < signal_value)
    {
        const HRESULT event_result = fence_->SetEventOnCompletion(signal_value, fence_event_.get());
        if (FAILED(event_result))
        {
            Log::error(format_failure_message(
                static_cast<ResultCode>(event_result), "ID3D12Fence::SetEventOnCompletion during shutdown"));
            return false;
        }

        const DWORD wait_result = WaitForSingleObject(fence_event_.get(), INFINITE);
        if (wait_result != WAIT_OBJECT_0)
        {
            if (wait_result == WAIT_FAILED)
            {
                Log::error(format_failure_message(
                    static_cast<ResultCode>(HRESULT_FROM_WIN32(GetLastError())),
                    "WaitForSingleObject for D3D12 fence during shutdown"));
            }
            else
            {
                std::ostringstream stream;
                stream << "WaitForSingleObject returned unexpected status " << wait_result
                       << " while waiting for the D3D12 fence during shutdown";
                Log::error(stream.str());
            }
            return false;
        }
    }

    for (FrameResource& frame : frames_)
    {
        frame.fence_value = 0;
    }
    gpu_idle_proven_ = true;
    return true;
}

void D3D12Context::abandon_resources() noexcept
{
    // Member destructors must not release these objects after an unsuccessful final queue flush.
    static_cast<void>(command_list_.Detach());
    for (FrameResource& frame : frames_)
    {
        static_cast<void>(frame.back_buffer.Detach());
        static_cast<void>(frame.command_allocator.Detach());
        frame.fence_value = 0;
    }
    static_cast<void>(rtv_heap_.Detach());
    static_cast<void>(swap_chain_.Detach());
    static_cast<void>(command_queue_.Detach());
    static_cast<void>(fence_.Detach());
    static_cast<void>(fence_event_.release());
    static_cast<void>(device_.Detach());
    static_cast<void>(adapter_.Detach());
    static_cast<void>(factory_.Detach());
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::rtv_handle(std::uint32_t index) const noexcept
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * rtv_descriptor_size_;
    return handle;
}
}
