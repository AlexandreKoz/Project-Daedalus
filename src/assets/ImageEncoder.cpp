#include "assets/ImageEncoder.h"

#include <limits>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#else
#include <png.h>
#include <cstdio>
#endif

namespace daedalus
{
namespace
{
[[nodiscard]] std::size_t expected_size(std::uint32_t width, std::uint32_t height)
{
    if (width == 0U || height == 0U) throw std::invalid_argument("PNG dimensions must be nonzero");
    const std::uint64_t bytes = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL;
    if (bytes > std::numeric_limits<std::size_t>::max()) throw std::overflow_error("PNG byte size overflow");
    return static_cast<std::size_t>(bytes);
}
}

void encode_png_rgba8(const std::filesystem::path& path,
                      std::uint32_t width,
                      std::uint32_t height,
                      std::span<const std::byte> rgba8)
{
    if (rgba8.size() != expected_size(width, height))
        throw std::invalid_argument("PNG RGBA8 byte count does not match dimensions");
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
#if defined(_WIN32)
    std::error_code remove_error;
    std::filesystem::remove(path, remove_error);
    if (remove_error) throw std::runtime_error("WIC PNG encoder could not replace output file");

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr))
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) throw std::runtime_error("WIC PNG encoder factory creation failed");
    Microsoft::WRL::ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)))
        throw std::runtime_error("WIC PNG encoder could not open output file");
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
        throw std::runtime_error("WIC PNG encoder initialization failed");
    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    Microsoft::WRL::ComPtr<IPropertyBag2> properties;
    if (FAILED(encoder->CreateNewFrame(&frame, &properties)) || FAILED(frame->Initialize(properties.Get())) ||
        FAILED(frame->SetSize(width, height)))
        throw std::runtime_error("WIC PNG frame initialization failed");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
    if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppRGBA)
        throw std::runtime_error("WIC PNG encoder does not support RGBA8 output");
    const std::uint64_t stride64 = static_cast<std::uint64_t>(width) * 4ULL;
    if (stride64 > std::numeric_limits<UINT>::max() || rgba8.size() > std::numeric_limits<UINT>::max())
        throw std::overflow_error("PNG output exceeds WIC WritePixels limits");
    if (FAILED(frame->WritePixels(height, static_cast<UINT>(stride64), static_cast<UINT>(rgba8.size()),
                                  reinterpret_cast<BYTE*>(const_cast<std::byte*>(rgba8.data())))) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit()))
        throw std::runtime_error("WIC PNG encoder failed while writing pixels");
#else
    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) throw std::runtime_error("failed to open PNG output: " + path.string());
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png == nullptr) { std::fclose(file); throw std::runtime_error("png_create_write_struct failed"); }
    png_infop info = png_create_info_struct(png);
    if (info == nullptr) { png_destroy_write_struct(&png, nullptr); std::fclose(file); throw std::runtime_error("png_create_info_struct failed"); }
    if (setjmp(png_jmpbuf(png)) != 0)
    {
        png_destroy_write_struct(&png, &info);
        std::fclose(file);
        throw std::runtime_error("libpng failed while encoding PNG");
    }
    png_init_io(png, file);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (std::uint32_t y = 0; y < height; ++y)
    {
        auto* row = reinterpret_cast<png_bytep>(const_cast<std::byte*>(rgba8.data()) + static_cast<std::size_t>(y) * width * 4U);
        png_write_row(png, row);
    }
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    std::fclose(file);
#endif
}
}
