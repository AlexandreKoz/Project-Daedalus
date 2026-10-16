#pragma once

#ifndef _WIN32
#error D3D12Helpers is available only on Windows.
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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace daedalus
{
inline constexpr std::uint32_t kInvalidDescriptor = std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] std::vector<std::byte> read_binary_file(const std::filesystem::path& path);
[[nodiscard]] D3D12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE type) noexcept;
[[nodiscard]] D3D12_RESOURCE_DESC buffer_description(std::uint64_t byte_size,
                                                     D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) noexcept;
[[nodiscard]] D3D12_RESOURCE_DESC texture2d_description(std::uint32_t width, std::uint32_t height, DXGI_FORMAT format,
                                                        D3D12_RESOURCE_FLAGS flags) noexcept;
[[nodiscard]] D3D12_RESOURCE_BARRIER transition_barrier(ID3D12Resource* resource,
                                                        D3D12_RESOURCE_STATES before,
                                                        D3D12_RESOURCE_STATES after) noexcept;
[[nodiscard]] D3D12_RESOURCE_BARRIER uav_barrier(ID3D12Resource* resource) noexcept;

[[nodiscard]] UINT checked_uint(std::uint64_t value, std::string_view description);
[[nodiscard]] std::uint64_t checked_multiply_u64(std::uint64_t left, std::uint64_t right, std::string_view description);
[[nodiscard]] std::uint64_t checked_add_u64(std::uint64_t left, std::uint64_t right, std::string_view description);
[[nodiscard]] std::uint64_t align_up_u64(std::uint64_t value, std::uint64_t alignment, std::string_view description);

// Serializes a version-1.0 root signature and creates it, retaining the serializer error blob text
// in the thrown ResultError so root-signature mistakes are actionable.
[[nodiscard]] Microsoft::WRL::ComPtr<ID3D12RootSignature> create_root_signature(ID3D12Device* device,
                                                                               const D3D12_ROOT_SIGNATURE_DESC& description,
                                                                               std::string_view label);

[[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Resource> create_buffer(ID3D12Device* device,
                                                                  std::uint64_t byte_size,
                                                                  D3D12_HEAP_TYPE heap,
                                                                  D3D12_RESOURCE_STATES initial_state,
                                                                  D3D12_RESOURCE_FLAGS flags,
                                                                  std::wstring_view debug_name);

void set_debug_name(ID3D12Object* object, std::wstring_view name) noexcept;

// A 2D texture with CPU-side state tracking. All recording happens on one command list per frame,
// so the tracked state is authoritative; every pass transitions through GpuTexture to keep it so.
struct GpuTexture
{
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t srv = kInvalidDescriptor; // index in the shared shader-visible heap
    std::uint32_t uav = kInvalidDescriptor;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};

    [[nodiscard]] bool valid() const noexcept { return resource != nullptr; }
    void reset() noexcept { *this = GpuTexture{}; }
};

[[nodiscard]] GpuTexture create_texture(ID3D12Device* device,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        DXGI_FORMAT format,
                                        D3D12_RESOURCE_FLAGS flags,
                                        D3D12_RESOURCE_STATES initial_state,
                                        const D3D12_CLEAR_VALUE* clear_value,
                                        std::wstring_view debug_name);

// Collects barriers and submits them in one ResourceBarrier call.
class BarrierBatch final
{
public:
    void transition(GpuTexture& texture, D3D12_RESOURCE_STATES after);
    void transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
    void uav(ID3D12Resource* resource);
    void flush(ID3D12GraphicsCommandList* command_list);
    [[nodiscard]] bool empty() const noexcept { return barriers_.empty(); }

private:
    std::vector<D3D12_RESOURCE_BARRIER> barriers_;
};

// Shader bytecode loader that returns an actionable error naming the missing shader and build step.
[[nodiscard]] std::vector<std::byte> load_shader(const std::filesystem::path& directory, std::string_view file_name);
}
