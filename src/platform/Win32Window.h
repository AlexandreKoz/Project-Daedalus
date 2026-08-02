#pragma once

#ifndef _WIN32
#error Win32Window is available only on Windows.
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "rendering/OrbitCamera.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace daedalus
{
class Win32Window final
{
public:
    Win32Window(std::wstring title, std::uint32_t client_width, std::uint32_t client_height);
    ~Win32Window();

    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;
    Win32Window(Win32Window&&) = delete;
    Win32Window& operator=(Win32Window&&) = delete;

    void show(int command_show);
    [[nodiscard]] bool process_messages();
    [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> consume_resize();
    [[nodiscard]] OrbitInput consume_orbit_input() noexcept;
    [[nodiscard]] bool consume_reload_request() noexcept;
    [[nodiscard]] bool minimized() const noexcept;
    [[nodiscard]] bool close_requested() const noexcept;
    [[nodiscard]] HWND native_handle() const noexcept;
    [[nodiscard]] std::uint32_t client_width() const noexcept;
    [[nodiscard]] std::uint32_t client_height() const noexcept;

private:
    static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(HWND native_window, UINT message, WPARAM wparam, LPARAM lparam);
    void destroy() noexcept;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    std::wstring class_name_;
    std::uint32_t client_width_ = 0;
    std::uint32_t client_height_ = 0;
    bool minimized_ = false;
    bool close_requested_ = false;
    bool resize_pending_ = false;
    bool class_registered_ = false;
    bool left_dragging_ = false;
    bool right_dragging_ = false;
    POINT last_mouse_{};
    OrbitInput orbit_input_{};
    bool reload_requested_ = false;
};
}
