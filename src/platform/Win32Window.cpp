#include "platform/Win32Window.h"

#include "core/Error.h"
#include "core/Log.h"

#include <windowsx.h>

#include <sstream>
#include <stdexcept>

namespace daedalus
{
Win32Window::Win32Window(std::wstring title, std::uint32_t client_width, std::uint32_t client_height)
    : instance_(GetModuleHandleW(nullptr)),
      class_name_(L"ProjectDaedalusWindowClass"),
      client_width_(client_width),
      client_height_(client_height)
{
    if (instance_ == nullptr)
    {
        throw std::runtime_error("GetModuleHandleW returned null");
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = &Win32Window::window_procedure;
    window_class.hInstance = instance_;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = nullptr;
    window_class.lpszClassName = class_name_.c_str();

    const ATOM registered_class = RegisterClassExW(&window_class);
    if (registered_class == 0)
    {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS)
        {
            throw ResultError(static_cast<ResultCode>(HRESULT_FROM_WIN32(error)), "RegisterClassExW");
        }
    }
    else
    {
        class_registered_ = true;
    }

    try
    {
        RECT rectangle{0, 0, static_cast<LONG>(client_width), static_cast<LONG>(client_height)};
        const DWORD style = WS_OVERLAPPEDWINDOW;
        if (AdjustWindowRectEx(&rectangle, style, FALSE, 0) == FALSE)
        {
            throw ResultError(static_cast<ResultCode>(HRESULT_FROM_WIN32(GetLastError())), "AdjustWindowRectEx");
        }

        window_ = CreateWindowExW(
            0,
            class_name_.c_str(),
            title.c_str(),
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rectangle.right - rectangle.left,
            rectangle.bottom - rectangle.top,
            nullptr,
            nullptr,
            instance_,
            this);

        if (window_ == nullptr)
        {
            throw ResultError(static_cast<ResultCode>(HRESULT_FROM_WIN32(GetLastError())), "CreateWindowExW");
        }
    }
    catch (...)
    {
        destroy();
        throw;
    }
}

Win32Window::~Win32Window()
{
    destroy();
}

void Win32Window::show(int command_show)
{
    ShowWindow(window_, command_show);
    UpdateWindow(window_);
}

bool Win32Window::process_messages()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
    {
        if (message.message == WM_QUIT)
        {
            close_requested_ = true;
            return false;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return !close_requested_;
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> Win32Window::consume_resize()
{
    if (!resize_pending_ || minimized_ || client_width_ == 0 || client_height_ == 0)
    {
        return std::nullopt;
    }

    resize_pending_ = false;
    return std::pair{client_width_, client_height_};
}

OrbitInput Win32Window::consume_orbit_input() noexcept
{
    const OrbitInput result = orbit_input_;
    orbit_input_ = {};
    return result;
}

bool Win32Window::consume_reload_request() noexcept
{
    const bool result = reload_requested_;
    reload_requested_ = false;
    return result;
}

bool Win32Window::minimized() const noexcept
{
    return minimized_ || client_width_ == 0 || client_height_ == 0;
}

bool Win32Window::close_requested() const noexcept
{
    return close_requested_;
}

HWND Win32Window::native_handle() const noexcept
{
    return window_;
}

std::uint32_t Win32Window::client_width() const noexcept
{
    return client_width_;
}

std::uint32_t Win32Window::client_height() const noexcept
{
    return client_height_;
}

LRESULT CALLBACK Win32Window::window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    Win32Window* self = nullptr;

    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<Win32Window*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    }
    else
    {
        self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    if (self != nullptr)
    {
        return self->handle_message(window, message, wparam, lparam);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT Win32Window::handle_message(HWND native_window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_CLOSE:
        close_requested_ = true;
        if (DestroyWindow(native_window) == FALSE)
        {
            std::ostringstream stream;
            stream << "DestroyWindow failed with Win32 error " << GetLastError();
            Log::error(stream.str());
        }
        return 0;

    case WM_DESTROY:
        window_ = nullptr;
        close_requested_ = true;
        PostQuitMessage(0);
        return 0;

    case WM_NCDESTROY:
        if (window_ == native_window)
        {
            window_ = nullptr;
        }
        SetWindowLongPtrW(native_window, GWLP_USERDATA, 0);
        return DefWindowProcW(native_window, message, wparam, lparam);

    case WM_SIZE:
    {
        const auto new_width = static_cast<std::uint32_t>(LOWORD(lparam));
        const auto new_height = static_cast<std::uint32_t>(HIWORD(lparam));
        const bool was_minimized = minimized_;
        minimized_ = (wparam == SIZE_MINIMIZED) || new_width == 0 || new_height == 0;
        client_width_ = new_width;
        client_height_ = new_height;

        if (!minimized_ && (new_width != 0 && new_height != 0))
        {
            resize_pending_ = true;
            if (was_minimized)
            {
                Log::info("Window restored");
            }
        }
        else if (!was_minimized)
        {
            Log::info("Window minimized");
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
        left_dragging_ = true;
        last_mouse_ = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        SetCapture(native_window);
        return 0;

    case WM_LBUTTONUP:
        left_dragging_ = false;
        if (!right_dragging_) ReleaseCapture();
        return 0;

    case WM_RBUTTONDOWN:
        right_dragging_ = true;
        last_mouse_ = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        SetCapture(native_window);
        return 0;

    case WM_RBUTTONUP:
        right_dragging_ = false;
        if (!left_dragging_) ReleaseCapture();
        return 0;

    case WM_MOUSEMOVE:
    {
        const POINT current{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        const float delta_x = static_cast<float>(current.x - last_mouse_.x);
        const float delta_y = static_cast<float>(current.y - last_mouse_.y);
        if (left_dragging_)
        {
            orbit_input_.orbit_delta_x += delta_x;
            orbit_input_.orbit_delta_y += delta_y;
        }
        if (right_dragging_)
        {
            orbit_input_.pan_delta_x += delta_x;
            orbit_input_.pan_delta_y += delta_y;
        }
        last_mouse_ = current;
        return 0;
    }

    case WM_MOUSEWHEEL:
        orbit_input_.zoom_delta += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<float>(WHEEL_DELTA);
        return 0;

    case WM_KEYDOWN:
        if (wparam == 'R')
        {
            orbit_input_.reset = true;
            return 0;
        }
        if (wparam == VK_F5)
        {
            reload_requested_ = true;
            return 0;
        }
        break;

    case WM_CAPTURECHANGED:
        left_dragging_ = false;
        right_dragging_ = false;
        return 0;

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(native_window, message, wparam, lparam);
    }
    return DefWindowProcW(native_window, message, wparam, lparam);
}

void Win32Window::destroy() noexcept
{
    if (window_ != nullptr)
    {
        const HWND window = window_;
        if (DestroyWindow(window) == FALSE)
        {
            std::ostringstream stream;
            stream << "DestroyWindow failed during cleanup with Win32 error " << GetLastError();
            Log::warning(stream.str());
        }
        window_ = nullptr;
    }

    if (class_registered_ && instance_ != nullptr)
    {
        if (UnregisterClassW(class_name_.c_str(), instance_) == FALSE)
        {
            const DWORD error = GetLastError();
            if (error != ERROR_CLASS_DOES_NOT_EXIST)
            {
                std::ostringstream stream;
                stream << "UnregisterClassW failed with Win32 error " << error;
                Log::warning(stream.str());
            }
        }
        class_registered_ = false;
    }
}
}
