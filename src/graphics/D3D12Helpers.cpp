#include "graphics/D3D12Helpers.h"

#include "core/Error.h"

#include <fstream>
#include <stdexcept>

namespace daedalus
{
std::vector<std::byte> read_binary_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("unable to open shader bytecode: " + path.string());
    const std::streamoff size = input.tellg();
    if (size <= 0) throw std::runtime_error("shader bytecode is empty: " + path.string());
    if (static_cast<std::uintmax_t>(size) > std::numeric_limits<std::size_t>::max())
        throw std::overflow_error("shader bytecode is too large for this process: " + path.string());
    input.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input) throw std::runtime_error("unable to read shader bytecode: " + path.string());
    return bytes;
}

std::vector<std::byte> load_shader(const std::filesystem::path& directory, std::string_view file_name)
{
    const std::filesystem::path path = directory / std::filesystem::path(std::string(file_name));
    if (!std::filesystem::exists(path))
        throw std::runtime_error("shader bytecode '" + path.string() +
                                 "' is missing; rebuild the Daedalus target so the DXC custom commands regenerate and copy it");
    return read_binary_file(path);
}

D3D12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE type) noexcept
{
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

D3D12_RESOURCE_DESC buffer_description(std::uint64_t byte_size, D3D12_RESOURCE_FLAGS flags) noexcept
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = byte_size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = flags;
    return description;
}

D3D12_RESOURCE_DESC texture2d_description(std::uint32_t width, std::uint32_t height, DXGI_FORMAT format,
                                          D3D12_RESOURCE_FLAGS flags) noexcept
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    description.Flags = flags;
    return description;
}

D3D12_RESOURCE_BARRIER transition_barrier(ID3D12Resource* resource,
                                          D3D12_RESOURCE_STATES before,
                                          D3D12_RESOURCE_STATES after) noexcept
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

D3D12_RESOURCE_BARRIER uav_barrier(ID3D12Resource* resource) noexcept
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    return barrier;
}

UINT checked_uint(std::uint64_t value, std::string_view description)
{
    if (value > static_cast<std::uint64_t>(std::numeric_limits<UINT>::max()))
        throw std::overflow_error(std::string(description) + " exceeds the D3D12 UINT range");
    return static_cast<UINT>(value);
}

std::uint64_t checked_multiply_u64(std::uint64_t left, std::uint64_t right, std::string_view description)
{
    if (right != 0U && left > std::numeric_limits<std::uint64_t>::max() / right)
        throw std::overflow_error(std::string(description) + " size overflow");
    return left * right;
}

std::uint64_t checked_add_u64(std::uint64_t left, std::uint64_t right, std::string_view description)
{
    if (left > std::numeric_limits<std::uint64_t>::max() - right)
        throw std::overflow_error(std::string(description) + " size overflow");
    return left + right;
}

std::uint64_t align_up_u64(std::uint64_t value, std::uint64_t alignment, std::string_view description)
{
    if (alignment == 0U || (alignment & (alignment - 1U)) != 0U)
        throw std::invalid_argument(std::string(description) + " alignment must be a power of two");
    return checked_add_u64(value, alignment - 1U, description) & ~(alignment - 1U);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> create_root_signature(ID3D12Device* device,
                                                                 const D3D12_ROOT_SIGNATURE_DESC& description,
                                                                 std::string_view label)
{
    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT result = D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(result))
    {
        const std::string details = errors == nullptr ? std::string{} :
            std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        throw ResultError(static_cast<ResultCode>(result), "D3D12SerializeRootSignature(" + std::string(label) + "): " + details);
    }
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    const HRESULT create = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                                       IID_PPV_ARGS(&root_signature));
    if (FAILED(create))
        throw ResultError(static_cast<ResultCode>(create), "ID3D12Device::CreateRootSignature(" + std::string(label) + ")");
    return root_signature;
}

void set_debug_name(ID3D12Object* object, std::wstring_view name) noexcept
{
    if (object == nullptr || name.empty()) return;
    const std::wstring terminated(name);
    static_cast<void>(object->SetName(terminated.c_str()));
}

Microsoft::WRL::ComPtr<ID3D12Resource> create_buffer(ID3D12Device* device,
                                                    std::uint64_t byte_size,
                                                    D3D12_HEAP_TYPE heap,
                                                    D3D12_RESOURCE_STATES initial_state,
                                                    D3D12_RESOURCE_FLAGS flags,
                                                    std::wstring_view debug_name)
{
    if (byte_size == 0U) throw std::invalid_argument("buffer size must be nonzero");
    const D3D12_HEAP_PROPERTIES properties = heap_properties(heap);
    const D3D12_RESOURCE_DESC description = buffer_description(byte_size, flags);
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    DAEDALUS_THROW_IF_FAILED(device->CreateCommittedResource(
        &properties, D3D12_HEAP_FLAG_NONE, &description, initial_state, nullptr, IID_PPV_ARGS(&buffer)));
    set_debug_name(buffer.Get(), debug_name);
    return buffer;
}

GpuTexture create_texture(ID3D12Device* device,
                          std::uint32_t width,
                          std::uint32_t height,
                          DXGI_FORMAT format,
                          D3D12_RESOURCE_FLAGS flags,
                          D3D12_RESOURCE_STATES initial_state,
                          const D3D12_CLEAR_VALUE* clear_value,
                          std::wstring_view debug_name)
{
    if (width == 0U || height == 0U) throw std::invalid_argument("texture dimensions must be nonzero");
    const D3D12_HEAP_PROPERTIES properties = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_RESOURCE_DESC description = texture2d_description(width, height, format, flags);
    GpuTexture texture;
    DAEDALUS_THROW_IF_FAILED(device->CreateCommittedResource(
        &properties, D3D12_HEAP_FLAG_NONE, &description, initial_state, clear_value, IID_PPV_ARGS(&texture.resource)));
    set_debug_name(texture.resource.Get(), debug_name);
    texture.state = initial_state;
    texture.format = format;
    texture.width = width;
    texture.height = height;
    return texture;
}

void BarrierBatch::transition(GpuTexture& texture, D3D12_RESOURCE_STATES after)
{
    if (!texture.valid()) throw std::logic_error("transition requested for an empty GPU texture");
    if (texture.state == after) return;
    barriers_.push_back(transition_barrier(texture.resource.Get(), texture.state, after));
    texture.state = after;
}

void BarrierBatch::transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    if (resource == nullptr) throw std::logic_error("transition requested for a null resource");
    if (before == after) return;
    barriers_.push_back(transition_barrier(resource, before, after));
}

void BarrierBatch::uav(ID3D12Resource* resource)
{
    barriers_.push_back(uav_barrier(resource));
}

void BarrierBatch::flush(ID3D12GraphicsCommandList* command_list)
{
    if (barriers_.empty()) return;
    command_list->ResourceBarrier(checked_uint(barriers_.size(), "resource barrier count"), barriers_.data());
    barriers_.clear();
}
}
