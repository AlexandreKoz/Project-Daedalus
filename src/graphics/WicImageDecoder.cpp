#include "graphics/WicImageDecoder.h"

#include "core/Error.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <limits>
#include <stdexcept>

namespace daedalus
{
DecodedImage decode_image_wic(std::span<const std::byte> encoded)
{
    if (encoded.empty() || encoded.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()))
    {
        throw std::runtime_error("WIC image payload is empty or exceeds the supported size");
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    DAEDALUS_THROW_IF_FAILED(CoCreateInstance(
        CLSID_WICImagingFactory2,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)));

    Microsoft::WRL::ComPtr<IWICStream> stream;
    DAEDALUS_THROW_IF_FAILED(factory->CreateStream(&stream));
    DAEDALUS_THROW_IF_FAILED(stream->InitializeFromMemory(
        reinterpret_cast<BYTE*>(const_cast<std::byte*>(encoded.data())),
        static_cast<DWORD>(encoded.size())));

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    DAEDALUS_THROW_IF_FAILED(factory->CreateDecoderFromStream(
        stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder));

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    DAEDALUS_THROW_IF_FAILED(decoder->GetFrame(0, &frame));
    UINT width = 0;
    UINT height = 0;
    DAEDALUS_THROW_IF_FAILED(frame->GetSize(&width, &height));
    if (width == 0 || height == 0)
    {
        throw std::runtime_error("WIC decoded an image with zero dimensions");
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    DAEDALUS_THROW_IF_FAILED(factory->CreateFormatConverter(&converter));
    DAEDALUS_THROW_IF_FAILED(converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom));

    const std::uint64_t row_pitch = static_cast<std::uint64_t>(width) * 4ULL;
    const std::uint64_t byte_count = row_pitch * static_cast<std::uint64_t>(height);
    if (row_pitch > std::numeric_limits<UINT>::max() || byte_count > std::numeric_limits<UINT>::max())
    {
        throw std::runtime_error("decoded image exceeds WIC CopyPixels limits");
    }

    DecodedImage result;
    result.width = width;
    result.height = height;
    result.rgba8.resize(static_cast<std::size_t>(byte_count));
    DAEDALUS_THROW_IF_FAILED(converter->CopyPixels(
        nullptr,
        static_cast<UINT>(row_pitch),
        static_cast<UINT>(byte_count),
        reinterpret_cast<BYTE*>(result.rgba8.data())));
    return result;
}
}
