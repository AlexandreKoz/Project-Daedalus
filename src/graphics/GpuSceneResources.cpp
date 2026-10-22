#include "graphics/GpuSceneResources.h"

#include "core/Error.h"
#include "core/Log.h"

#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace daedalus
{
namespace
{
constexpr std::uint64_t kUploadBatchBudgetBytes = 256ULL * 1024ULL * 1024ULL;

[[nodiscard]] D3D12_TEXTURE_ADDRESS_MODE address_mode(WrapMode mode) noexcept
{
    switch (mode)
    {
    case WrapMode::clamp_to_edge: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case WrapMode::mirrored_repeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case WrapMode::repeat: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
}

[[nodiscard]] D3D12_FILTER d3d_filter(const Sampler& sampler) noexcept
{
    const bool mag_linear = sampler.mag_filter != FilterMode::nearest;
    bool min_linear = true;
    switch (sampler.min_filter)
    {
    case FilterMode::nearest:
    case FilterMode::nearest_mipmap_nearest:
    case FilterMode::nearest_mipmap_linear:
        min_linear = false;
        break;
    case FilterMode::linear:
    case FilterMode::linear_mipmap_nearest:
    case FilterMode::linear_mipmap_linear:
    case FilterMode::unspecified:
        min_linear = true;
        break;
    }
    if (!min_linear && !mag_linear) return D3D12_FILTER_MIN_MAG_MIP_POINT;
    if (!min_linear && mag_linear) return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
    if (min_linear && !mag_linear) return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
}
}

class GpuSceneResources::UploadBatch final
{
public:
    UploadBatch(D3D12Context& context, ID3D12Device* device) : context_(context), device_(device) {}
    UploadBatch(const UploadBatch&) = delete;
    UploadBatch& operator=(const UploadBatch&) = delete;

    void buffer(ID3D12Resource* destination, const void* data, std::uint64_t byte_size, D3D12_RESOURCE_STATES final_state)
    {
        if (destination == nullptr || data == nullptr || byte_size == 0U) throw std::invalid_argument("buffer upload requires nonempty data");
        if (byte_size > std::numeric_limits<std::size_t>::max()) throw std::overflow_error("buffer upload exceeds the address space");
        reserve(byte_size);
        Pending pending;
        pending.upload = create_buffer(device_, byte_size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                                       D3D12_RESOURCE_FLAG_NONE, L"Daedalus upload staging");
        void* mapped = nullptr;
        const D3D12_RANGE read_range{0, 0};
        DAEDALUS_THROW_IF_FAILED(pending.upload->Map(0, &read_range, &mapped));
        std::memcpy(mapped, data, static_cast<std::size_t>(byte_size));
        pending.upload->Unmap(0, nullptr);
        pending.destination = destination;
        pending.byte_size = byte_size;
        pending.final_state = final_state;
        pending_.push_back(std::move(pending));
        pending_bytes_ += byte_size;
    }

    void texture(ID3D12Resource* destination, std::uint32_t width, std::uint32_t height, const std::byte* rgba8,
                 D3D12_RESOURCE_STATES final_state)
    {
        if (destination == nullptr || rgba8 == nullptr || width == 0U || height == 0U)
            throw std::invalid_argument("texture upload requires nonempty RGBA8 data");
        const D3D12_RESOURCE_DESC description = destination->GetDesc();
        Pending pending;
        UINT rows = 0;
        UINT64 upload_size = 0;
        device_->GetCopyableFootprints(&description, 0, 1, 0, &pending.footprint, &rows, nullptr, &upload_size);
        const std::size_t source_pitch = static_cast<std::size_t>(width) * 4U;
        if (rows != height || pending.footprint.Footprint.RowPitch < source_pitch || upload_size == 0U)
            throw std::runtime_error("D3D12 returned an invalid RGBA8 copy footprint");
        reserve(upload_size);
        pending.upload = create_buffer(device_, upload_size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                                       D3D12_RESOURCE_FLAG_NONE, L"Daedalus texture staging");
        std::byte* mapped = nullptr;
        const D3D12_RANGE read_range{0, 0};
        DAEDALUS_THROW_IF_FAILED(pending.upload->Map(0, &read_range, reinterpret_cast<void**>(&mapped)));
        for (UINT row = 0; row < rows; ++row)
            std::memcpy(mapped + static_cast<std::size_t>(pending.footprint.Offset) + static_cast<std::size_t>(row) * pending.footprint.Footprint.RowPitch,
                        rgba8 + static_cast<std::size_t>(row) * source_pitch, source_pitch);
        pending.upload->Unmap(0, nullptr);
        pending.destination = destination;
        pending.is_texture = true;
        pending.final_state = final_state;
        pending_.push_back(std::move(pending));
        pending_bytes_ += upload_size;
    }

    void flush()
    {
        if (pending_.empty()) return;
        context_.execute_immediate([&](ID3D12GraphicsCommandList* command_list)
        {
            std::vector<D3D12_RESOURCE_BARRIER> barriers;
            barriers.reserve(pending_.size());
            for (const Pending& pending : pending_)
            {
                if (pending.is_texture)
                {
                    D3D12_TEXTURE_COPY_LOCATION destination{};
                    destination.pResource = pending.destination;
                    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    destination.SubresourceIndex = 0;
                    D3D12_TEXTURE_COPY_LOCATION source{};
                    source.pResource = pending.upload.Get();
                    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    source.PlacedFootprint = pending.footprint;
                    command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
                }
                else
                {
                    command_list->CopyBufferRegion(pending.destination, 0, pending.upload.Get(), 0, pending.byte_size);
                }
                barriers.push_back(transition_barrier(pending.destination, D3D12_RESOURCE_STATE_COPY_DEST, pending.final_state));
            }
            command_list->ResourceBarrier(checked_uint(barriers.size(), "upload barrier count"), barriers.data());
        });
        // execute_immediate waited for completion, so the staging buffers may be released now.
        pending_.clear();
        pending_bytes_ = 0;
    }

private:
    struct Pending
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> upload;
        ID3D12Resource* destination = nullptr;
        std::uint64_t byte_size = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        D3D12_RESOURCE_STATES final_state = D3D12_RESOURCE_STATE_COMMON;
        bool is_texture = false;
    };

    void reserve(std::uint64_t bytes)
    {
        if (!pending_.empty() && pending_bytes_ + bytes > kUploadBatchBudgetBytes) flush();
    }

    D3D12Context& context_;
    ID3D12Device* device_ = nullptr;
    std::vector<Pending> pending_;
    std::uint64_t pending_bytes_ = 0;
};

GpuSceneResources::GpuSceneResources(D3D12Context& context, const CanonicalScene& scene)
    : context_(context), device_(context.device()), scene_(scene)
{
    if (device_ == nullptr) throw std::invalid_argument("GpuSceneResources requires a D3D12 device");
    layout_ = GpuSceneDescriptorLayout::create(scene_.textures.size(), scene_.primitives.size(), scene_.samplers.size());
    create_heaps();
    UploadBatch batch(context_, device_);
    create_textures(batch);
    create_geometry(batch);
    batch.flush();
    create_samplers();
    for (std::uint32_t index = layout_.dynamic_base(); index < layout_.total_descriptors(); ++index)
        write_null_texture_srv(index);
}

void GpuSceneResources::create_heaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC srv_description{};
    srv_description.NumDescriptors = layout_.total_descriptors();
    srv_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    DAEDALUS_THROW_IF_FAILED(device_->CreateDescriptorHeap(&srv_description, IID_PPV_ARGS(&srv_heap_)));
    set_debug_name(srv_heap_.Get(), L"Daedalus shared CBV/SRV/UAV heap");
    srv_increment_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC sampler_description{};
    sampler_description.NumDescriptors = layout_.total_samplers();
    sampler_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampler_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    DAEDALUS_THROW_IF_FAILED(device_->CreateDescriptorHeap(&sampler_description, IID_PPV_ARGS(&sampler_heap_)));
    set_debug_name(sampler_heap_.Get(), L"Daedalus shared sampler heap");
    sampler_increment_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

void GpuSceneResources::create_textures(UploadBatch& batch)
{
    textures_.reserve(scene_.textures.size() + 1U);
    auto make_texture = [&](std::uint32_t width, std::uint32_t height, std::wstring_view name)
    {
        const D3D12_HEAP_PROPERTIES heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
        const D3D12_RESOURCE_DESC description = texture2d_description(width, height, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D12_RESOURCE_FLAG_NONE);
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        DAEDALUS_THROW_IF_FAILED(device_->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)));
        set_debug_name(texture.Get(), name);
        return texture;
    };

    static const std::array<std::byte, 4> white{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
    textures_.push_back(make_texture(1U, 1U, L"Daedalus white texture"));
    batch.texture(textures_.back().Get(), 1U, 1U, white.data(), kTextureState);
    write_texture_srv(GpuSceneDescriptorLayout::kWhiteLinear, textures_.back().Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
    write_texture_srv(GpuSceneDescriptorLayout::kWhiteSrgb, textures_.back().Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

    for (std::size_t index = 0; index < scene_.textures.size(); ++index)
    {
        const Texture& texture = scene_.textures[index];
        if (!texture.image.valid() || texture.image.value() >= scene_.images.size())
            throw std::runtime_error("canonical texture references an invalid image");
        const Image& image = scene_.images[texture.image.value()];
        const std::uint64_t expected_bytes = checked_multiply_u64(
            checked_multiply_u64(image.width, image.height, "canonical decoded image"), 4U, "canonical decoded image");
        if (image.width == 0U || image.height == 0U || image.decoded_rgba8.size() != expected_bytes ||
            image.row_stride != static_cast<std::uint64_t>(image.width) * 4ULL)
            throw std::runtime_error("canonical image does not contain validated tightly packed RGBA8 pixels");
        if (texture.sampler.valid() && texture.sampler.value() >= scene_.samplers.size())
            throw std::runtime_error("canonical texture references a sampler outside the sampler table");
        textures_.push_back(make_texture(image.width, image.height, L"Daedalus scene texture"));
        batch.texture(textures_.back().Get(), image.width, image.height, image.decoded_rgba8.data(), kTextureState);
        const auto texture_index = static_cast<std::uint32_t>(index);
        write_texture_srv(layout_.texture_linear(texture_index), textures_.back().Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
        write_texture_srv(layout_.texture_srgb(texture_index), textures_.back().Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    }
}

void GpuSceneResources::create_samplers()
{
    auto write_sampler = [&](std::uint32_t descriptor_index, const D3D12_SAMPLER_DESC& description)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = sampler_heap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(descriptor_index) * sampler_increment_;
        device_->CreateSampler(&description, handle);
    };
    auto scene_sampler = [](const Sampler& sampler)
    {
        D3D12_SAMPLER_DESC description{};
        description.Filter = d3d_filter(sampler);
        description.AddressU = address_mode(sampler.wrap_s);
        description.AddressV = address_mode(sampler.wrap_t);
        description.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        description.MipLODBias = 0.0F;
        description.MaxAnisotropy = 1;
        description.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        // Exactly one mip is uploaded; LOD is clamped to zero so mipmapped glTF filters cannot address
        // nonexistent levels. DXR uses SampleLevel(..., 0) which matches this clamp exactly.
        description.MinLOD = 0.0F;
        description.MaxLOD = 0.0F;
        return description;
    };
    write_sampler(layout_.default_sampler(), scene_sampler(Sampler{}));
    for (std::size_t index = 0; index < scene_.samplers.size(); ++index)
        write_sampler(layout_.scene_sampler(static_cast<std::uint32_t>(index)), scene_sampler(scene_.samplers[index]));

    D3D12_SAMPLER_DESC shadow{};
    shadow.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    shadow.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadow.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadow.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadow.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    shadow.BorderColor[0] = 1.0F;
    shadow.BorderColor[1] = 1.0F;
    shadow.BorderColor[2] = 1.0F;
    shadow.BorderColor[3] = 1.0F;
    shadow.MinLOD = 0.0F;
    shadow.MaxLOD = 0.0F;
    write_sampler(layout_.shadow_sampler(), shadow);

    D3D12_SAMPLER_DESC linear_clamp{};
    linear_clamp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    linear_clamp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linear_clamp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linear_clamp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linear_clamp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    linear_clamp.MaxAnisotropy = 1;
    linear_clamp.MinLOD = 0.0F;
    linear_clamp.MaxLOD = 0.0F;
    write_sampler(layout_.linear_clamp_sampler(), linear_clamp);
}

void GpuSceneResources::create_geometry(UploadBatch& batch)
{
    primitives_.reserve(scene_.primitives.size());
    for (std::size_t index = 0; index < scene_.primitives.size(); ++index)
    {
        const Primitive& primitive = scene_.primitives[index];
        if (primitive.vertices.empty() || primitive.indices.empty())
            throw std::runtime_error("canonical primitive is empty");
        const std::uint64_t vertex_bytes = checked_multiply_u64(primitive.vertices.size(), sizeof(Vertex), "vertex buffer");
        const std::uint64_t index_bytes = checked_multiply_u64(primitive.indices.size(), sizeof(std::uint32_t), "index buffer");

        GpuPrimitive gpu;
        gpu.vertex_buffer = create_buffer(device_, vertex_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                                          D3D12_RESOURCE_FLAG_NONE, L"Daedalus vertex buffer");
        gpu.index_buffer = create_buffer(device_, index_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                                         D3D12_RESOURCE_FLAG_NONE, L"Daedalus index buffer");
        batch.buffer(gpu.vertex_buffer.Get(), primitive.vertices.data(), vertex_bytes, kVertexBufferState);
        batch.buffer(gpu.index_buffer.Get(), primitive.indices.data(), index_bytes, kIndexBufferState);
        gpu.vertex_view.BufferLocation = gpu.vertex_buffer->GetGPUVirtualAddress();
        gpu.vertex_view.SizeInBytes = checked_uint(vertex_bytes, "vertex buffer view");
        gpu.vertex_view.StrideInBytes = checked_uint(sizeof(Vertex), "vertex stride");
        gpu.index_view.BufferLocation = gpu.index_buffer->GetGPUVirtualAddress();
        gpu.index_view.SizeInBytes = checked_uint(index_bytes, "index buffer view");
        gpu.index_view.Format = DXGI_FORMAT_R32_UINT;
        gpu.vertex_count = checked_uint(primitive.vertices.size(), "vertex count");
        gpu.index_count = checked_uint(primitive.indices.size(), "index count");
        const auto primitive_index = static_cast<std::uint32_t>(index);
        write_raw_buffer_srv(layout_.vertex_buffer(primitive_index), gpu.vertex_buffer.Get(), vertex_bytes);
        write_raw_buffer_srv(layout_.index_buffer(primitive_index), gpu.index_buffer.Get(), index_bytes);
        primitives_.push_back(std::move(gpu));
    }
}

void GpuSceneResources::check_srv_index(std::uint32_t index) const
{
    if (index >= layout_.total_descriptors())
        throw std::out_of_range("descriptor index " + std::to_string(index) + " is outside the shared heap");
}

D3D12_CPU_DESCRIPTOR_HANDLE GpuSceneResources::srv_cpu(std::uint32_t index) const
{
    check_srv_index(index);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = srv_heap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * srv_increment_;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE GpuSceneResources::srv_gpu(std::uint32_t index) const
{
    check_srv_index(index);
    D3D12_GPU_DESCRIPTOR_HANDLE handle = srv_heap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * srv_increment_;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE GpuSceneResources::sampler_gpu(std::uint32_t index) const
{
    if (index >= layout_.total_samplers()) throw std::out_of_range("sampler descriptor index is outside the shared heap");
    D3D12_GPU_DESCRIPTOR_HANDLE handle = sampler_heap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * sampler_increment_;
    return handle;
}

std::uint32_t GpuSceneResources::block_base(DescriptorBlock block) const
{
    return layout_.dynamic_block(static_cast<std::uint32_t>(block));
}

void GpuSceneResources::write_texture_srv(std::uint32_t index, ID3D12Resource* resource, DXGI_FORMAT format) const
{
    D3D12_SHADER_RESOURCE_VIEW_DESC description{};
    description.Format = format;
    description.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    description.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    description.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(resource, &description, srv_cpu(index));
}

void GpuSceneResources::write_texture_uav(std::uint32_t index, ID3D12Resource* resource, DXGI_FORMAT format) const
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC description{};
    description.Format = format;
    description.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device_->CreateUnorderedAccessView(resource, nullptr, &description, srv_cpu(index));
}

void GpuSceneResources::write_null_texture_srv(std::uint32_t index) const
{
    write_texture_srv(index, nullptr, DXGI_FORMAT_R8G8B8A8_UNORM);
}

void GpuSceneResources::write_null_texture_uav(std::uint32_t index) const
{
    write_texture_uav(index, nullptr, DXGI_FORMAT_R32_FLOAT);
}

void GpuSceneResources::write_structured_buffer_srv(std::uint32_t index, ID3D12Resource* resource,
                                                    std::uint32_t element_count, std::uint32_t element_stride) const
{
    D3D12_SHADER_RESOURCE_VIEW_DESC description{};
    description.Format = DXGI_FORMAT_UNKNOWN;
    description.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    description.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    description.Buffer.NumElements = element_count;
    description.Buffer.StructureByteStride = element_stride;
    device_->CreateShaderResourceView(resource, &description, srv_cpu(index));
}

void GpuSceneResources::write_raw_buffer_srv(std::uint32_t index, ID3D12Resource* resource, std::uint64_t byte_size) const
{
    if (byte_size % 4U != 0U) throw std::invalid_argument("raw buffer views require a multiple of four bytes");
    D3D12_SHADER_RESOURCE_VIEW_DESC description{};
    description.Format = DXGI_FORMAT_R32_TYPELESS;
    description.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    description.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    description.Buffer.NumElements = checked_uint(byte_size / 4U, "raw buffer element count");
    description.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    device_->CreateShaderResourceView(resource, &description, srv_cpu(index));
}

void GpuSceneResources::write_raw_buffer_uav(std::uint32_t index, ID3D12Resource* resource, std::uint64_t byte_size) const
{
    if (byte_size % 4U != 0U) throw std::invalid_argument("raw buffer views require a multiple of four bytes");
    D3D12_UNORDERED_ACCESS_VIEW_DESC description{};
    description.Format = DXGI_FORMAT_R32_TYPELESS;
    description.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    description.Buffer.NumElements = checked_uint(byte_size / 4U, "raw buffer element count");
    description.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device_->CreateUnorderedAccessView(resource, nullptr, &description, srv_cpu(index));
}

Microsoft::WRL::ComPtr<ID3D12Resource> GpuSceneResources::upload_buffer(const void* data,
                                                                        std::uint64_t byte_size,
                                                                        D3D12_RESOURCE_STATES final_state,
                                                                        D3D12_RESOURCE_FLAGS flags,
                                                                        std::wstring_view debug_name)
{
    Microsoft::WRL::ComPtr<ID3D12Resource> destination =
        create_buffer(device_, byte_size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, flags, debug_name);
    UploadBatch batch(context_, device_);
    batch.buffer(destination.Get(), data, byte_size, final_state);
    batch.flush();
    return destination;
}

std::uint64_t GpuSceneResources::committed_allocation_bytes() const noexcept
{
    std::uint64_t total = 0;
    auto add = [&](ID3D12Resource* resource) noexcept
    {
        if (resource == nullptr) return;
        const D3D12_RESOURCE_DESC description = resource->GetDesc();
        const D3D12_RESOURCE_ALLOCATION_INFO info = device_->GetResourceAllocationInfo(0, 1, &description);
        if (info.SizeInBytes != std::numeric_limits<std::uint64_t>::max() &&
            total <= std::numeric_limits<std::uint64_t>::max() - info.SizeInBytes)
            total += info.SizeInBytes;
    };
    for (const GpuPrimitive& primitive : primitives_)
    {
        add(primitive.vertex_buffer.Get());
        add(primitive.index_buffer.Get());
    }
    for (const auto& texture : textures_) add(texture.Get());
    return total;
}

std::uint64_t GpuSceneResources::descriptor_count() const noexcept
{
    return static_cast<std::uint64_t>(layout_.total_descriptors()) + layout_.total_samplers();
}
}
