#pragma once

#include "assets/ImportReport.h"
#include "core/CommandLine.h"
#include "scene/Scene.h"

#include <filesystem>
#include <memory>

namespace daedalus
{
class Win32Window;
class D3D12Context;
class DiagnosticSceneRenderer;

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
    void load_scene();
    void create_renderer();
    void reload_scene();
    void write_import_report() const;
    void shutdown() noexcept;

    CommandLineOptions options_;
    CanonicalScene scene_;
    ImportReport import_report_;
    std::filesystem::path shader_directory_;
    std::unique_ptr<Win32Window> window_;
    std::unique_ptr<D3D12Context> graphics_;
    std::unique_ptr<DiagnosticSceneRenderer> renderer_;
    bool com_initialized_ = false;
    bool initialized_ = false;
};
}
