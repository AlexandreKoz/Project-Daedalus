// Campaign D-H command-line and renderer-settings tests (source only in the D-H drop; not executed).
#include "TestHarness.h"
#include "core/CommandLine.h"
#include "core/RenderSettings.h"

#include <string_view>
#include <vector>

namespace
{
using namespace daedalus;
using namespace daedalus::tests;

CommandLineOptions parse(std::vector<std::wstring_view> arguments)
{
    return parse_command_line(arguments);
}

void test_defaults_preserve_campaign_c()
{
    const CommandLineOptions options = parse({});
    require(options.renderer.mode == RendererMode::raster, "raster stays the default renderer");
    require(options.renderer.upscaler.mode == UpscalerMode::none, "no upscaler by default");
    require(!options.renderer.temporal.jitter, "jitter is off by default");
    require(options.renderer.path_trace.preset == PathTracePreset::reference, "reference preset by default");
    require(path_trace_is_unbiased_reference(options.renderer.path_trace), "default path tracer is the unbiased reference");
}

void test_renderer_flags()
{
    const CommandLineOptions options = parse({L"--renderer", L"pathtrace", L"--pt-spp", L"4", L"--pt-max-depth", L"12",
                                              L"--pt-rr-depth", L"5", L"--pt-seed", L"99", L"--capture-samples", L"256",
                                              L"--capture-hdr", L"out.pfm", L"--builtin-scene", L"cornell-box"});
    require(options.renderer.mode == RendererMode::pathtrace, "--renderer pathtrace");
    require(options.renderer.path_trace.samples_per_frame == 4U, "--pt-spp");
    require(options.renderer.path_trace.max_depth == 12U, "--pt-max-depth");
    require(options.renderer.path_trace.min_russian_roulette_depth == 5U, "--pt-rr-depth");
    require(options.renderer.path_trace.seed == 99U, "--pt-seed");
    require(options.capture_samples == 256U, "--capture-samples");
    require(options.builtin_scene == "cornell-box", "--builtin-scene");

    const CommandLineOptions hybrid = parse({L"--renderer", L"hybrid", L"--rt-shadows", L"off", L"--rt-reflection-max-roughness", L"0.3",
                                             L"--jitter", L"on", L"--temporal-debug", L"motion", L"--camera-path", L"orbit:0.5",
                                             L"--animate-node", L"2:rotate-y:0.25"});
    require(hybrid.renderer.mode == RendererMode::hybrid, "--renderer hybrid");
    require(!hybrid.renderer.hybrid.shadows, "--rt-shadows off");
    require_near(hybrid.renderer.hybrid.reflection_max_roughness, 0.3F, 1.0e-6F, "--rt-reflection-max-roughness");
    require(hybrid.renderer.temporal.jitter, "--jitter on");
    require(!parse({L"--jitter", L"off"}).renderer.temporal.jitter, "--jitter off");
    require(hybrid.renderer.temporal.debug_view == TemporalDebugView::motion, "--temporal-debug motion");
    require(hybrid.renderer.camera_script.kind == CameraScriptKind::orbit, "--camera-path orbit");
    require(hybrid.renderer.node_animations.size() == 1U && hybrid.renderer.node_animations[0].node == 2U, "--animate-node");
}

void test_reference_preset_rejects_bias()
{
    require_throws<CommandLineError>([] { (void)parse({L"--renderer", L"pathtrace", L"--pt-denoise", L"on"}); },
                                     "denoising the reference preset is rejected");
    require_throws<CommandLineError>([] { (void)parse({L"--renderer", L"pathtrace", L"--pt-clamp", L"10"}); },
                                     "firefly clamping the reference preset is rejected");
    const CommandLineOptions interactive = parse({L"--renderer", L"pathtrace", L"--pt-preset", L"interactive"});
    require(!path_trace_is_unbiased_reference(interactive.renderer.path_trace), "interactive preset is declared biased");
    require_throws<CommandLineError>([] { (void)parse({L"--pt-rr-depth", L"9", L"--pt-max-depth", L"4"}); }, "RR depth above max depth rejected");
    require_throws<CommandLineError>([] { (void)parse({L"--capture-samples", L"16"}); }, "--capture-samples requires pathtrace");
}

void test_mode_and_asset_constraints()
{
    require_throws<CommandLineError>([] { (void)parse({L"--renderer", L"hybrid", L"--diagnostic", L"normals"}); },
                                     "raster diagnostics are not silently ignored in hybrid mode");
    require_throws<CommandLineError>([] { (void)parse({L"--builtin-scene", L"cornell-box", L"--asset", L"a.glb"}); },
                                     "--builtin-scene and --asset are exclusive");
    require_throws<CommandLineError>([] { (void)parse({L"--builtin-scene", L"teapot"}); }, "unknown built-in scenes rejected");
    require_throws<CommandLineError>([] { (void)parse({L"--renderer", L"dlss"}); }, "unknown renderer rejected");
    require_throws<CommandLineError>([] { (void)parse({L"--blender-timeout", L"999999"}); }, "Blender timeout bounded");
    const CommandLineOptions assets = parse({L"--asset", L"model.fbx", L"--asset-cache", L"cache", L"--texture-search-root", L"maps",
                                             L"--texture-search-root", L"more", L"--blender", L"blender.exe", L"--blender-timeout", L"60"});
    require(assets.asset_cache_directory.has_value() && assets.texture_search_roots.size() == 2U, "asset cache and search roots");
    require(assets.blender_executable.has_value() && assets.blender_timeout_seconds == 60U, "Blender options");
}

void test_upscaler_settings()
{
    const CommandLineOptions bilinear = parse({L"--upscaler", L"bilinear", L"--render-scale", L"0.5"});
    require(bilinear.renderer.upscaler.mode == UpscalerMode::bilinear, "--upscaler bilinear");
    require_near(bilinear.renderer.upscaler.render_scale, 0.5F, 1.0e-6F, "--render-scale");
    const CommandLineOptions dlss = parse({L"--upscaler", L"dlss", L"--dlss-quality", L"performance"});
    require(dlss.renderer.upscaler.mode == UpscalerMode::dlss, "--upscaler dlss parses (availability is decided at runtime)");
    require(dlss.renderer.upscaler.dlss_quality == DlssQualityMode::performance, "--dlss-quality");
    require(parse_dlss_quality("ultra-performance").has_value() || parse_dlss_quality("ultra_performance").has_value(),
            "ultra performance quality is accepted");
}
}

int main()
{
    return daedalus::tests::run({
        {"defaults preserve Campaign C", test_defaults_preserve_campaign_c},
        {"renderer flags", test_renderer_flags},
        {"reference preset rejects biased options", test_reference_preset_rejects_bias},
        {"mode and asset constraints", test_mode_and_asset_constraints},
        {"upscaler settings", test_upscaler_settings}});
}
