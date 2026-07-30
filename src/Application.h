#pragma once

#include "core/CommandLine.h"

#include <memory>

namespace daedalus
{
class Win32Window;
class D3D12Context;
class TriangleRenderer;

class Application final
{
public:
    explicit Application(CommandLineOptions options);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    [[nodiscard]] static int execute(const CommandLineOptions& options);
    [[nodiscard]] int run();

private:
    void initialize();
    void shutdown() noexcept;

    CommandLineOptions options_;
    std::unique_ptr<Win32Window> window_;
    std::unique_ptr<D3D12Context> graphics_;
    std::unique_ptr<TriangleRenderer> renderer_;
    bool initialized_ = false;
};
}
