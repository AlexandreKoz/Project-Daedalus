#include "graphics/dlss/DlssBackend.h"

#include "core/Error.h"
#include "core/Log.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace daedalus
{
namespace
{
constexpr D3D12_RESOURCE_STATES kReadState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

[[nodiscard]] std::string escape(const std::string& value)
{
    std::string result;
    for (const char c : value)
    {
        if (c == '"' || c == '\\') result.push_back('\\');
        if (c == '\n') { result += "\\n"; continue; }
        result.push_back(c);
    }
    return result;
}
}

std::string UpscalerReport::to_json() const
{
    std::ostringstream stream;
    stream << "{\"requested\": \"" << to_string(requested) << "\", \"active\": \"" << to_string(active)
           << "\", \"status\": \"" << escape(status) << "\", \"reason\": \"" << escape(reason)
           << "\", \"dlss_quality\": \"" << to_string(quality) << "\", \"vendor_glue_compiled\": "
           << (vendor_glue_compiled ? "true" : "false") << ", \"runtime\": \"" << escape(runtime)
           << "\", \"render_resolution\": [" << render_width << ", " << render_height << "], \"display_resolution\": ["
           << display_width << ", " << display_height << "], \"evaluations\": " << evaluations
           << ", \"resets\": " << resets << ", \"feature_creations\": " << feature_creations << '}';
    return stream.str();
}

bool DlssBackend::vendor_glue_compiled() noexcept
{
#if defined(DAEDALUS_DLSS_VENDOR_GLUE)
    return true;
#else
    return false;
#endif
}

std::unique_ptr<DlssBackend> DlssBackend::try_create(D3D12Context& context,
                                                     GpuSceneResources& resources,
                                                     const std::filesystem::path& application_directory,
                                                     DlssQualityMode quality,
                                                     std::uint32_t display_width,
                                                     std::uint32_t display_height,
                                                     UpscalerReport& report)
{
    report.requested = UpscalerMode::dlss;
    report.active = UpscalerMode::none;
    report.quality = quality;
    report.vendor_glue_compiled = vendor_glue_compiled();
    report.display_width = display_width;
    report.display_height = display_height;
#if defined(DAEDALUS_DLSS_VENDOR_GLUE)
    std::string failure;
    std::unique_ptr<DlssVendorRuntime> runtime =
        create_dlss_vendor_runtime(context.device(), context.command_queue(), context.adapter(), application_directory, failure);
    if (runtime == nullptr)
    {
        report.status = "FALLBACK";
        report.reason = "DLSS runtime initialization failed: " + failure;
        return nullptr;
    }
    std::string unavailable;
    if (!runtime->super_resolution_available(unavailable))
    {
        report.status = "FALLBACK";
        report.reason = "DLSS Super Resolution unavailable: " + unavailable;
        return nullptr;
    }
    if (!runtime->optimal_render_resolution(quality, display_width, display_height).has_value())
    {
        report.status = "FALLBACK";
        report.reason = std::string("DLSS quality mode '") + std::string(to_string(quality)) + "' is not supported by the SDK/runtime";
        return nullptr;
    }
    report.runtime = runtime->runtime_description();
    std::unique_ptr<DlssBackend> backend(new DlssBackend(context, resources, std::move(runtime), quality, report));
    backend->configure(display_width, display_height);
    report = backend->report_;
    return backend;
#else
    static_cast<void>(context);
    static_cast<void>(resources);
    static_cast<void>(application_directory);
    report.status = "BLOCKED";
    report.reason = "This build has no DLSS vendor glue (DAEDALUS_ENABLE_DLSS=OFF or no SDK supplied). The NVIDIA SDK "
                    "integration is BLOCKED pending the exact SDK version/headers; rendering continues at native resolution.";
    return nullptr;
#endif
}

DlssBackend::DlssBackend(D3D12Context& context, GpuSceneResources& resources, std::unique_ptr<DlssVendorRuntime> runtime,
                         DlssQualityMode quality, UpscalerReport report)
    : context_(context), resources_(resources), runtime_(std::move(runtime)), quality_(quality), report_(std::move(report))
{
}

DlssBackend::~DlssBackend()
{
    if (runtime_ != nullptr && feature_created_) runtime_->release_feature();
}

void DlssBackend::configure(std::uint32_t display_width, std::uint32_t display_height)
{
    const std::optional<DlssRenderResolution> resolution = runtime_->optimal_render_resolution(quality_, display_width, display_height);
    if (!resolution.has_value() || resolution->width == 0U || resolution->height == 0U ||
        resolution->width > display_width || resolution->height > display_height)
        throw std::runtime_error("DLSS runtime returned an invalid render resolution for the requested quality mode");
    render_resolution_ = *resolution;
    if (feature_created_)
    {
        runtime_->release_feature();
        feature_created_ = false;
    }
    output_ = create_texture(context_.device(), display_width, display_height, DXGI_FORMAT_R16G16B16A16_FLOAT,
                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, kReadState, nullptr, L"Daedalus DLSS output");
    output_.srv = resources_.block_base(DescriptorBlock::upscaler);
    resources_.write_texture_srv(output_.srv, output_.resource.Get(), output_.format);
    DlssFeatureDescription description;
    description.quality = quality_;
    description.render_width = render_resolution_.width;
    description.render_height = render_resolution_.height;
    description.display_width = display_width;
    description.display_height = display_height;
    context_.execute_immediate([&](ID3D12GraphicsCommandList* command_list) { runtime_->create_feature(command_list, description); });
    feature_created_ = true;
    ++report_.feature_creations;
    report_.active = UpscalerMode::dlss;
    report_.status = "ACTIVE";
    report_.reason.clear();
    report_.render_width = render_resolution_.width;
    report_.render_height = render_resolution_.height;
    report_.display_width = display_width;
    report_.display_height = display_height;
    std::ostringstream stream;
    stream << "DLSS Super Resolution configured: quality=" << to_string(quality_) << " render=" << render_resolution_.width
           << 'x' << render_resolution_.height << " display=" << display_width << 'x' << display_height
           << " runtime=\"" << report_.runtime << '"';
    Log::info(stream.str());
}

void DlssBackend::record(ID3D12GraphicsCommandList* command_list,
                         GpuTexture& color,
                         ID3D12Resource* hardware_depth,
                         GpuTexture* linear_depth,
                         GpuTexture& motion,
                         float jitter_x,
                         float jitter_y,
                         float exposure_ev,
                         float frame_time_ms,
                         bool reset,
                         GpuTimer& timer)
{
    if (!feature_created_) throw std::logic_error("DLSS evaluate requested before the feature was created");
    if (color.width != render_resolution_.width || color.height != render_resolution_.height)
        throw std::logic_error("DLSS colour input does not match the SDK render resolution");
    BarrierBatch barriers;
    barriers.transition(color, kReadState);
    barriers.transition(motion, kReadState);
    if (linear_depth != nullptr) barriers.transition(*linear_depth, kReadState);
    barriers.transition(output_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers.flush(command_list);
    // Hardware depth (if any) stays in DEPTH_WRITE in Daedalus; the glue receives it read-only and
    // is documented to transition it itself if the SDK requires a shader-resource state.
    DlssEvaluateInputs inputs;
    inputs.command_list = command_list;
    inputs.color = color.resource.Get();
    inputs.hardware_depth = hardware_depth;
    inputs.linear_depth = linear_depth != nullptr ? linear_depth->resource.Get() : nullptr;
    inputs.motion_vectors = motion.resource.Get();
    inputs.output = output_.resource.Get();
    inputs.jitter_x = jitter_x;
    inputs.jitter_y = jitter_y;
    inputs.exposure_ev = exposure_ev;
    inputs.frame_time_ms = frame_time_ms;
    inputs.reset = reset;
    inputs.render_width = render_resolution_.width;
    inputs.render_height = render_resolution_.height;
    runtime_->evaluate(inputs);
    ++report_.evaluations;
    if (reset) ++report_.resets;
    barriers.uav(output_.resource.Get());
    barriers.transition(output_, kReadState);
    barriers.flush(command_list);
    timer.mark(command_list, GpuPass::upscale);
}
}
