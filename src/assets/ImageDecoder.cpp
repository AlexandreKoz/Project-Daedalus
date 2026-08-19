#include "assets/ImageDecoder.h"

#include <algorithm>
#include <csetjmp>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>

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
#include <jpeglib.h>
#include <png.h>
#endif

namespace daedalus
{
namespace
{
[[nodiscard]] std::uint64_t checked_rgba_size(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0) throw ImageDecodeError("decoded image has zero dimensions");
    constexpr std::uint64_t channels = 4;
    const std::uint64_t row = static_cast<std::uint64_t>(width) * channels;
    if (height > std::numeric_limits<std::uint64_t>::max() / row)
        throw ImageDecodeError("decoded image byte size overflows");
    const std::uint64_t bytes = row * static_cast<std::uint64_t>(height);
    if (bytes > std::numeric_limits<std::size_t>::max())
        throw ImageDecodeError("decoded image exceeds process address range");
    return bytes;
}

#if defined(_WIN32)
[[nodiscard]] std::string hresult_text(HRESULT result)
{
    std::ostringstream stream;
    stream << "HRESULT 0x" << std::hex << std::uppercase << static_cast<unsigned long>(result);
    return stream.str();
}

void require_hresult(HRESULT result, std::string_view operation)
{
    if (FAILED(result)) throw ImageDecodeError(std::string(operation) + " failed: " + hresult_text(result));
}

class ScopedComInitialization final
{
public:
    ScopedComInitialization()
    {
        result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(result_) && result_ != RPC_E_CHANGED_MODE)
            throw ImageDecodeError("CoInitializeEx failed: " + hresult_text(result_));
    }
    ~ScopedComInitialization()
    {
        if (result_ == S_OK || result_ == S_FALSE) CoUninitialize();
    }
    ScopedComInitialization(const ScopedComInitialization&) = delete;
    ScopedComInitialization& operator=(const ScopedComInitialization&) = delete;
private:
    HRESULT result_ = E_FAIL;
};

[[nodiscard]] DecodedImage decode_wic(std::span<const std::byte> encoded)
{
    ScopedComInitialization com;
    if (encoded.empty() || encoded.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()))
        throw ImageDecodeError("image payload is empty or exceeds WIC memory-stream limits");

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    require_hresult(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&factory)),
                    "CoCreateInstance(CLSID_WICImagingFactory2)");

    Microsoft::WRL::ComPtr<IWICStream> stream;
    require_hresult(factory->CreateStream(&stream), "IWICImagingFactory::CreateStream");
    require_hresult(stream->InitializeFromMemory(
                        reinterpret_cast<BYTE*>(const_cast<std::byte*>(encoded.data())),
                        static_cast<DWORD>(encoded.size())),
                    "IWICStream::InitializeFromMemory");

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    require_hresult(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder),
                    "IWICImagingFactory::CreateDecoderFromStream");

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    require_hresult(decoder->GetFrame(0, &frame), "IWICBitmapDecoder::GetFrame");
    UINT width = 0;
    UINT height = 0;
    require_hresult(frame->GetSize(&width, &height), "IWICBitmapFrameDecode::GetSize");

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    require_hresult(factory->CreateFormatConverter(&converter), "IWICImagingFactory::CreateFormatConverter");
    require_hresult(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                          WICBitmapDitherTypeNone, nullptr, 0.0,
                                          WICBitmapPaletteTypeCustom),
                    "IWICFormatConverter::Initialize");

    const std::uint64_t byte_count = checked_rgba_size(width, height);
    const std::uint64_t row_stride = static_cast<std::uint64_t>(width) * 4ULL;
    if (row_stride > std::numeric_limits<UINT>::max() || byte_count > std::numeric_limits<UINT>::max())
        throw ImageDecodeError("decoded image exceeds WIC CopyPixels limits");

    DecodedImage result;
    result.width = width;
    result.height = height;
    result.source_components = 4;
    result.row_stride = row_stride;
    result.rgba8.resize(static_cast<std::size_t>(byte_count));
    require_hresult(converter->CopyPixels(nullptr, static_cast<UINT>(row_stride),
                                          static_cast<UINT>(byte_count),
                                          reinterpret_cast<BYTE*>(result.rgba8.data())),
                    "IWICBitmapSource::CopyPixels");
    return result;
}
#else
[[nodiscard]] DecodedImage decode_png(std::span<const std::byte> encoded)
{
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (encoded.empty() || png_image_begin_read_from_memory(&image, encoded.data(), encoded.size()) == 0)
    {
        const std::string message = image.message[0] == '\0' ? "libpng rejected the image header" : image.message;
        png_image_free(&image);
        throw ImageDecodeError(message);
    }

    const std::uint32_t source_components = PNG_IMAGE_SAMPLE_CHANNELS(image.format);
    image.format = PNG_FORMAT_RGBA;
    const std::uint64_t byte_count = checked_rgba_size(image.width, image.height);

    DecodedImage result;
    result.width = image.width;
    result.height = image.height;
    result.source_components = source_components;
    result.row_stride = static_cast<std::uint64_t>(image.width) * 4ULL;
    result.rgba8.resize(static_cast<std::size_t>(byte_count));
    if (png_image_finish_read(&image, nullptr, result.rgba8.data(), 0, nullptr) == 0)
    {
        const std::string message = image.message[0] == '\0' ? "libpng failed to decode pixel data" : image.message;
        png_image_free(&image);
        throw ImageDecodeError(message);
    }
    png_image_free(&image);
    return result;
}

struct JpegErrorManager
{
    jpeg_error_mgr base{};
    std::jmp_buf jump{};
    char message[JMSG_LENGTH_MAX]{};
};

extern "C" void jpeg_error_exit_bridge(j_common_ptr common)
{
    auto* manager = reinterpret_cast<JpegErrorManager*>(common->err);
    (*common->err->format_message)(common, manager->message);
    std::longjmp(manager->jump, 1);
}

extern "C" void jpeg_emit_message_bridge(j_common_ptr common, int message_level)
{
    if (message_level >= 0) return;
    auto* manager = reinterpret_cast<JpegErrorManager*>(common->err);
    (*common->err->format_message)(common, manager->message);
    std::longjmp(manager->jump, 1);
}

struct JpegDecodeBuffer
{
    unsigned char* rgba = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t source_components = 0;
    char error[JMSG_LENGTH_MAX]{};
};

template <std::size_t N>
void copy_error_message(char (&destination)[N], const char* source) noexcept
{
    static_assert(N > 0);
    if (source == nullptr)
    {
        destination[0] = '\0';
        return;
    }
    const std::size_t count = std::min<std::size_t>(std::strlen(source), N - 1U);
    std::memcpy(destination, source, count);
    destination[count] = '\0';
}

// Keeps libjpeg's setjmp/longjmp boundary entirely inside POD/C-allocation state so a decoder
// error never jumps across a live C++ object with a non-trivial destructor.
[[nodiscard]] bool decode_jpeg_to_c_buffer(std::span<const std::byte> encoded, JpegDecodeBuffer& output) noexcept
{
    jpeg_decompress_struct decoder{};
    JpegErrorManager errors{};
    bool decoder_created = false;
    unsigned char* scanline = nullptr;
    decoder.err = jpeg_std_error(&errors.base);
    errors.base.error_exit = jpeg_error_exit_bridge;
    errors.base.emit_message = jpeg_emit_message_bridge;

    if (setjmp(errors.jump) != 0)
    {
        if (errors.message[0] != '\0') copy_error_message(output.error, errors.message);
        std::free(scanline);
        std::free(output.rgba);
        output.rgba = nullptr;
        if (decoder_created) jpeg_destroy_decompress(&decoder);
        return false;
    }

    jpeg_create_decompress(&decoder);
    decoder_created = true;
    jpeg_mem_src(&decoder,
                 reinterpret_cast<const unsigned char*>(encoded.data()),
                 static_cast<unsigned long>(encoded.size()));
    if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK)
    {
        copy_error_message(output.error, "libjpeg rejected the image header");
        jpeg_destroy_decompress(&decoder);
        return false;
    }
    output.source_components = static_cast<std::uint32_t>(decoder.num_components);
    if (output.source_components != 1U && output.source_components != 3U)
    {
        copy_error_message(output.error, "Campaign B supports only grayscale or three-component JPEG images");
        jpeg_destroy_decompress(&decoder);
        return false;
    }
    decoder.out_color_space = output.source_components == 1U ? JCS_GRAYSCALE : JCS_RGB;
    if (jpeg_start_decompress(&decoder) == FALSE)
    {
        copy_error_message(output.error, "libjpeg failed to start decompression");
        jpeg_destroy_decompress(&decoder);
        return false;
    }

    output.width = static_cast<std::uint32_t>(decoder.output_width);
    output.height = static_cast<std::uint32_t>(decoder.output_height);
    const std::uint32_t output_components = static_cast<std::uint32_t>(decoder.output_components);
    const std::uint64_t rgba_row_u64 = static_cast<std::uint64_t>(output.width) * 4ULL;
    const std::uint64_t source_row_u64 = static_cast<std::uint64_t>(output.width) * output_components;
    if (output.width == 0U || output.height == 0U ||
        output.height > std::numeric_limits<std::uint64_t>::max() / rgba_row_u64)
    {
        copy_error_message(output.error, "JPEG decoded dimensions exceed process address range");
        jpeg_destroy_decompress(&decoder);
        return false;
    }
    const std::uint64_t rgba_bytes_u64 = rgba_row_u64 * output.height;
    if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t))
    {
        if (rgba_bytes_u64 > std::numeric_limits<std::size_t>::max() ||
            source_row_u64 > std::numeric_limits<std::size_t>::max())
        {
            copy_error_message(output.error, "JPEG decoded dimensions exceed process address range");
            jpeg_destroy_decompress(&decoder);
            return false;
        }
    }

    const std::size_t rgba_bytes = static_cast<std::size_t>(rgba_bytes_u64);
    const std::size_t source_row = static_cast<std::size_t>(source_row_u64);
    output.rgba = static_cast<unsigned char*>(std::malloc(rgba_bytes));
    scanline = static_cast<unsigned char*>(std::malloc(source_row));
    if (output.rgba == nullptr || scanline == nullptr)
    {
        copy_error_message(output.error, "JPEG decode allocation failed");
        std::free(scanline);
        std::free(output.rgba);
        output.rgba = nullptr;
        jpeg_destroy_decompress(&decoder);
        return false;
    }

    while (decoder.output_scanline < decoder.output_height)
    {
        JSAMPROW row_pointer = scanline;
        if (jpeg_read_scanlines(&decoder, &row_pointer, 1) != 1)
        {
            copy_error_message(output.error, "libjpeg returned an incomplete scanline");
            std::free(scanline);
            std::free(output.rgba);
            output.rgba = nullptr;
            jpeg_destroy_decompress(&decoder);
            return false;
        }
        const std::size_t y = static_cast<std::size_t>(decoder.output_scanline - 1U);
        unsigned char* destination = output.rgba + y * static_cast<std::size_t>(output.width) * 4U;
        for (std::size_t x = 0; x < output.width; ++x)
        {
            if (output_components == 1U)
            {
                const unsigned char value = scanline[x];
                destination[x * 4U + 0U] = value;
                destination[x * 4U + 1U] = value;
                destination[x * 4U + 2U] = value;
            }
            else
            {
                destination[x * 4U + 0U] = scanline[x * 3U + 0U];
                destination[x * 4U + 1U] = scanline[x * 3U + 1U];
                destination[x * 4U + 2U] = scanline[x * 3U + 2U];
            }
            destination[x * 4U + 3U] = 0xFFU;
        }
    }
    std::free(scanline);
    scanline = nullptr;
    if (jpeg_finish_decompress(&decoder) == FALSE)
    {
        copy_error_message(output.error, "libjpeg failed to finish decompression");
        std::free(output.rgba);
        output.rgba = nullptr;
        jpeg_destroy_decompress(&decoder);
        return false;
    }
    jpeg_destroy_decompress(&decoder);
    return true;
}

[[nodiscard]] DecodedImage decode_jpeg(std::span<const std::byte> encoded)
{
    if (encoded.empty() || encoded.size() > std::numeric_limits<unsigned long>::max())
        throw ImageDecodeError("JPEG payload is empty or exceeds libjpeg memory-source limits");

    JpegDecodeBuffer decoded;
    if (!decode_jpeg_to_c_buffer(encoded, decoded))
        throw ImageDecodeError(decoded.error[0] == '\0' ? "libjpeg failed to decode image" : decoded.error);

    const std::uint64_t byte_count = checked_rgba_size(decoded.width, decoded.height);
    DecodedImage result;
    result.width = decoded.width;
    result.height = decoded.height;
    result.source_components = decoded.source_components;
    result.row_stride = static_cast<std::uint64_t>(decoded.width) * 4ULL;
    result.rgba8.resize(static_cast<std::size_t>(byte_count));
    std::memcpy(result.rgba8.data(), decoded.rgba, result.rgba8.size());
    std::free(decoded.rgba);
    return result;
}
#endif
}

DecodedImage decode_image_rgba8(std::span<const std::byte> encoded, std::string_view mime_type)
{
    if (mime_type != "image/png" && mime_type != "image/jpeg")
        throw ImageDecodeError("unsupported image MIME type: " + std::string(mime_type));
#if defined(_WIN32)
    return decode_wic(encoded);
#else
    return mime_type == "image/png" ? decode_png(encoded) : decode_jpeg(encoded);
#endif
}
}
