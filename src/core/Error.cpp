#include "core/Error.h"

#include <iomanip>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace daedalus
{
#ifdef _WIN32
namespace
{
[[nodiscard]] std::string trim_message(std::string message)
{
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' '))
    {
        message.pop_back();
    }
    return message;
}
}
#endif

bool result_failed(ResultCode result) noexcept
{
    return result < 0;
}

std::string format_result_code(ResultCode result)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
           << static_cast<std::uint32_t>(result);

#ifdef _WIN32
    char* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageA(
        flags,
        nullptr,
        static_cast<DWORD>(result),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&buffer),
        0,
        nullptr);
    if (length != 0 && buffer != nullptr)
    {
        stream << " (" << trim_message(std::string(buffer, length)) << ')';
        LocalFree(buffer);
    }
#endif

    return stream.str();
}

std::string format_failure_message(
    ResultCode result,
    std::string_view operation,
    const std::source_location& location)
{
    std::ostringstream stream;
    stream << operation << " failed with " << format_result_code(result) << " at " << location.file_name() << ':'
           << location.line();
    return stream.str();
}

ResultError::ResultError(ResultCode result, std::string operation, const std::source_location& location)
    : std::runtime_error(format_failure_message(result, operation, location)), result_(result), operation_(std::move(operation))
{
}

ResultCode ResultError::result() const noexcept
{
    return result_;
}

const std::string& ResultError::operation() const noexcept
{
    return operation_;
}

void throw_if_failed(ResultCode result, std::string_view operation, const std::source_location& location)
{
    if (result_failed(result))
    {
        throw ResultError(result, std::string(operation), location);
    }
}
}
