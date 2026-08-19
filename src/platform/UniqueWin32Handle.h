#pragma once

#ifndef _WIN32
#error UniqueWin32Handle is available only on Windows.
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <utility>

namespace daedalus
{
class UniqueWin32Handle final
{
public:
    UniqueWin32Handle() noexcept = default;
    explicit UniqueWin32Handle(HANDLE handle) noexcept : handle_(handle)
    {
    }

    ~UniqueWin32Handle()
    {
        reset();
    }

    UniqueWin32Handle(const UniqueWin32Handle&) = delete;
    UniqueWin32Handle& operator=(const UniqueWin32Handle&) = delete;

    UniqueWin32Handle(UniqueWin32Handle&& other) noexcept : handle_(other.release())
    {
    }

    UniqueWin32Handle& operator=(UniqueWin32Handle&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept
    {
        return handle_;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return is_valid(handle_);
    }

    [[nodiscard]] HANDLE release() noexcept
    {
        return std::exchange(handle_, nullptr);
    }

    void reset(HANDLE replacement = nullptr) noexcept
    {
        if (handle_ == replacement)
        {
            return;
        }
        if (is_valid(handle_))
        {
            CloseHandle(handle_);
        }
        handle_ = replacement;
    }

private:
    [[nodiscard]] static bool is_valid(HANDLE handle) noexcept
    {
        return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    }

    HANDLE handle_ = nullptr;
};
}
