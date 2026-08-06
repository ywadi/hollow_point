// Device, context and swap chain (T0025). See hp/Render.hpp for the rules.
//
// This is the only translation unit in the engine that includes Diligent, and
// keeping it that way is what lets the link stay PRIVATE.
#include <hp/Render.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>
#include <hp/Window.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <utility>

// Must precede the factory headers: their LoadGraphicsEngine* helpers are
// inline and call LoadEngineDll, which lives here and is Windows-only.
#if defined(_WIN32)
#include <LoadEngineDll.h>
#endif

#include <EngineFactoryVk.h>

#include <MapHelper.hpp>
#include <RefCntAutoPtr.hpp>
#include <Texture.h>

namespace hp {
namespace {

const LogCategory kLog("render");
const LogCategory kDiligentLog("render.diligent");

/// Backend and adapter for the device-loss message.
///
/// File-scope because the Diligent callback is a plain function pointer with no
/// user-data parameter, and the fatal path must allocate nothing and look
/// nothing up. Written once at device creation, read only when everything is
/// already going wrong.
const char* g_activeBackendName = "unknown";
const char* g_activeAdapter = "unknown";

/// The fullscreen blit shaders (T0137). HLSL, compiled by Diligent for Vulkan.
///
/// **Self-contained on purpose.** DiligentFX ships `FullScreenTriangleVS.fx`
/// and every PostProcess effect uses it, so reusing it was the first thing
/// tried -- but it `#include`s `FullScreenTriangleVSOutput.fxh` and relies on
/// `NormalizedDeviceXYToTexUV` from Diligent's injected definitions, which means
/// wiring a shader-include factory onto DiligentFX's shader tree for six lines
/// of shader. Inline HLSL with no includes costs less and depends on nothing
/// outside this file.
///
/// **The V flip comes from `ClipSpace::yToV`, which the engine already owns.**
/// That is the better answer here rather than merely the cheaper one: the
/// disagreement between backends about which way texture space runs is a
/// documented engine concept (`hp/DepthConvention.hpp`), read from the device
/// rather than assumed, and its header warns in as many words that "a
/// render-to-texture pass that ignores this comes out vertically flipped on
/// exactly one of them". Deriving the flip from the same value the rest of the
/// engine uses means there is one answer to be wrong about, not two.
/// The stage-output variable is called `VSOut` in both shaders, matching
/// DiligentFX's own convention. It used to be load-bearing — the GL-era
/// HLSL-to-GLSL converter named varyings after the parameter variable, so
/// differing names failed to link on exactly one backend — and it is kept
/// because the two struct declarations are duplicated here and must stay
/// textually identical either way.
constexpr char kBlitVS[] = R"(
cbuffer BlitConstants { float4 g_Params; };   // x = yToV

struct BlitVSOutput { float4 pos : SV_POSITION; float2 uv : TEX_COORD; };

void main(in uint vid : SV_VertexID, out BlitVSOutput VSOut)
{
    // One oversized triangle rather than two -- no shared edge for the
    // rasteriser to crack, and three vertices instead of six.
    float2 corner = float2((vid << 1u) & 2u, vid & 2u);   // (0,0) (2,0) (0,2)
    float2 ndc    = corner * 2.0 - 1.0;                   // (-1,-1) (3,-1) (-1,3)

    VSOut.pos = float4(ndc, 0.0, 1.0);
    // v = 0.5 + ndcY * yToV, and yToV is **+-0.5**, not +-1 -- it is the whole
    // scale, not a sign. Diligent reports NDCAttribs{0, 1, -0.5} on Vulkan and
    // {0, 1, +0.5} on OpenGL, matching its own F3NDC_XYZ_TO_UVD_SCALE of
    // (0.5, -0.5, 1). Writing `0.5 * ndc.y * yToV` here instead squashes V into
    // the middle half of the texture, which reads as a stretched image rather
    // than as a bad constant.
    VSOut.uv  = float2(0.5 + 0.5 * ndc.x, 0.5 + ndc.y * g_Params.x);
}
)";

/// **No gamma maths here, and that is deliberate.** The source is an `_SRGB`
/// texture and the destination an `_SRGB` render target: sampling converts sRGB
/// to linear, and the ROP converts linear back to sRGB on write. The round trip
/// is exact. A `pow(2.2)` anywhere in this shader would be a second conversion
/// and the classic washed-out image.
constexpr char kBlitPS[] = R"(
Texture2D    g_Source;
SamplerState g_Source_sampler;

struct BlitVSOutput { float4 pos : SV_POSITION; float2 uv : TEX_COORD; };

float4 main(in BlitVSOutput VSOut) : SV_TARGET
{
    return g_Source.Sample(g_Source_sampler, VSOut.uv);
}
)";

/// Sends Diligent's diagnostics through the engine's log (T0054).
///
/// Without this they go straight to stderr with their own ANSI colouring, which
/// means the editor console (T0066), the log file and every other sink never
/// see them -- and a driver warning that only exists in a terminal nobody is
/// reading is not a diagnostic. It is also the volume control: device creation
/// dumps every instance extension the driver has at Info, which is exactly what
/// a category and a level are for.
/// Whether a backend message describes the device dying, rather than a normal
/// error (T0113).
///
/// Pattern-matching, and not by choice: **DiligentCore has no device-loss
/// handling at all** -- `VK_ERROR_DEVICE_LOST` appears only in the vendored
/// Vulkan headers, never in its source -- so there is no status to query and no
/// structured signal to subscribe to. The debug message callback is the only
/// hook that exists, which makes the text the only evidence available.
///
/// Deliberately broad. A false positive costs an abort on a run that was
/// already failing; a false negative costs the distinguishable message that is
/// the entire point of D20.
bool describesDeviceLoss(const char* text) {
    if (text == nullptr) {
        return false;
    }
    static constexpr const char* kNeedles[] = {
        "DEVICE_LOST", "device lost", "Device Lost", "device removed", "DEVICE_REMOVED",
    };
    for (const char* needle : kNeedles) {
        if (std::strstr(text, needle) != nullptr) {
            return true;
        }
    }
    return false;
}

/// The fatal path for a lost device (D20, T0113).
///
/// Named as a GPU/driver failure so a report of it is recognisable in seconds
/// rather than being triaged as memory corruption. Follows T0099's rules for a
/// failure path even though T0099 has not built the handler yet: nothing is
/// allocated here, the message is preformatted, the log is flushed, then abort.
[[noreturn]] void deviceLost(const char* backend, const char* adapter, const char* detail) {
    HP_LOG_FATAL(kLog,
                 "GPU DEVICE LOST -- this is a graphics driver or hardware failure, not an "
                 "engine crash. backend={} adapter='{}' detail={}. The device cannot be "
                 "recovered (D20): recreate-and-continue is deliberately not implemented. "
                 "Common causes are a driver update or reset while running, a GPU hang -- "
                 "including an infinite loop in a compute shader -- or a laptop switching "
                 "between integrated and discrete GPUs.",
                 backend, adapter, detail);
    logFlush();
    std::abort();
}

void DILIGENT_CALL_TYPE diligentMessage(Diligent::DEBUG_MESSAGE_SEVERITY severity,
                                        const Diligent::Char* message,
                                        const Diligent::Char* function, const Diligent::Char* file,
                                        int line) {
    const char* text = message != nullptr ? message : "";

    // Checked before the severity switch: a lost device is fatal whatever
    // severity the backend chose to report it at, and Diligent reports it at
    // whatever the failing call happened to use.
    if (describesDeviceLoss(text)) {
        deviceLost(g_activeBackendName, g_activeAdapter, text);
    }

    switch (severity) {
    case Diligent::DEBUG_MESSAGE_SEVERITY_FATAL_ERROR:
    case Diligent::DEBUG_MESSAGE_SEVERITY_ERROR:
        HP_LOG_ERROR(kDiligentLog, "{} ({}:{} in {})", text, file != nullptr ? file : "?", line,
                     function != nullptr ? function : "?");
        break;
    case Diligent::DEBUG_MESSAGE_SEVERITY_WARNING:
        HP_LOG_WARN(kDiligentLog, "{}", text);
        break;
    case Diligent::DEBUG_MESSAGE_SEVERITY_INFO:
        // Debug rather than Info on purpose: this is where the full extension
        // list lands, which is hundreds of lines and is diagnostic material
        // rather than something a normal run should print.
        HP_LOG_DEBUG(kDiligentLog, "{}", text);
        break;
    }
}

const char* backendName(RenderBackend backend) {
    switch (backend) {
    case RenderBackend::Vulkan:
        return "Vulkan";
    case RenderBackend::Default:
        return "default";
    }
    return "?";
}

/// Fills in the native window Diligent attaches to.
///
/// The two platforms want different structs, which is exactly why
/// `NativeWindowHandles` is a plain struct of `void*` -- so this is the only
/// place that needs to know, and no engine header drags in `Xlib.h` or
/// `windows.h`.
Diligent::NativeWindow nativeWindowFrom(const NativeWindowHandles& handles) {
#if defined(_WIN32)
    Diligent::Win32NativeWindow window;
    window.hWnd = handles.windowHandle;
    return window;
#else
    Diligent::LinuxNativeWindow window;
    window.WindowId = static_cast<Diligent::Uint32>(handles.windowId);
    window.pDisplay = handles.displayHandle;
    return window;
#endif
}

} // namespace

struct RenderLayer::Impl {
    RenderConfig config;
    Window* window = nullptr;
    RenderBackend active = RenderBackend::Default;
    std::string adapter;
    std::array<float, 4> clearColour{0.10F, 0.12F, 0.16F, 1.0F};

    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> swapChain;

    /// Dev present path (T0028). Not owned; see `setPresentSource`.
    Diligent::ITexture* presentSource = nullptr;

    /// So a failure is reported once rather than every frame.
    bool presentMismatchReported = false;

    // --- fullscreen blit (T0137) -----------------------------------------
    //
    // Built lazily on first use and kept for the device's lifetime. A PSO is
    // expensive to create and this one never varies, so creating it per frame
    // would be the most obvious possible waste.
    /// One pipeline per destination format.
    ///
    /// **Keyed by format rather than built once**, because a pipeline state
    /// bakes in the render-target format it writes to: the swap chain's is
    /// `BGRA8_UNORM_SRGB` on Vulkan here, while an offscreen `FrameTargets`
    /// colour target is `RGBA8_UNORM_SRGB`, and T0096's HDR chain will ask for
    /// `RGBA16F`. One PSO could serve only one of those. There are at most a
    /// handful of formats in a build, so the map never grows meaningfully.
    struct BlitPipeline {
        Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
        /// The variable the source texture binds to, resolved once.
        Diligent::IShaderResourceVariable* source = nullptr;
    };
    std::unordered_map<std::uint32_t, BlitPipeline> blitPipelines;

    Diligent::RefCntAutoPtr<Diligent::IBuffer> blitConstants;
    /// So a shader that will not compile is reported once, then given up on.
    bool blitFailed = false;

    /// Creates or returns the blit pipeline writing to `rtvFormat`.
    /// @param rtvFormat the destination render-target format.
    /// @returns the pipeline, or nullptr if it cannot be created.
    BlitPipeline* ensureBlitPipeline(Diligent::TEXTURE_FORMAT rtvFormat);

    /// Draws `source` over the whole of `destination`.
    /// @param source the texture to sample. Needs BIND_SHADER_RESOURCE.
    /// @param destination the render target to fill.
    /// @returns whether the draw was issued.
    bool blitTo(Diligent::ITexture* source, Diligent::ITextureView* destination);

    /// Fills `desc` from the config.
    void describeSwapChain(Diligent::SwapChainDesc& desc) const {
        desc.BufferCount = config.bufferCount;
    }

    bool createVulkan(const Diligent::NativeWindow& window, int width, int height);
    void describeAdapter();
};

RenderLayer::Impl::BlitPipeline* RenderLayer::Impl::ensureBlitPipeline(
    Diligent::TEXTURE_FORMAT rtvFormat) {
    const auto key = static_cast<std::uint32_t>(rtvFormat);
    if (const auto existing = blitPipelines.find(key); existing != blitPipelines.end()) {
        return &existing->second;
    }
    if (blitFailed || !device) {
        return nullptr;
    }

    Diligent::ShaderCreateInfo shaderInfo;
    shaderInfo.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    // Pairs `g_Source` with `g_Source_sampler` by name, so binding the texture
    // binds its sampler with it. A GL-era requirement originally; kept because
    // the pipeline below, its immutable sampler and its variable table are all
    // written against the combined name and there is nothing to gain from
    // splitting them.
    shaderInfo.Desc.UseCombinedTextureSamplers = true;

    Diligent::RefCntAutoPtr<Diligent::IShader> vs;
    shaderInfo.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shaderInfo.Desc.Name = "hp fullscreen blit VS";
    shaderInfo.EntryPoint = "main";
    shaderInfo.Source = kBlitVS;
    device->CreateShader(shaderInfo, &vs);

    Diligent::RefCntAutoPtr<Diligent::IShader> ps;
    shaderInfo.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shaderInfo.Desc.Name = "hp fullscreen blit PS";
    shaderInfo.Source = kBlitPS;
    device->CreateShader(shaderInfo, &ps);

    if (!vs || !ps) {
        HP_LOG_ERROR(kLog, "the fullscreen blit shaders would not compile; nothing will be "
                           "presented. This is an engine bug, not a driver one");
        blitFailed = true;
        return nullptr;
    }

    Diligent::BufferDesc constantsDesc;
    constantsDesc.Name = "hp blit constants";
    constantsDesc.Size = sizeof(float) * 4;
    constantsDesc.Usage = Diligent::USAGE_DYNAMIC;
    constantsDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    constantsDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    if (!blitConstants) {
        device->CreateBuffer(constantsDesc, nullptr, &blitConstants);
    }
    if (!blitConstants) {
        blitFailed = true;
        return nullptr;
    }

    Diligent::GraphicsPipelineStateCreateInfo pso;
    pso.PSODesc.Name = "hp fullscreen blit";
    pso.GraphicsPipeline.NumRenderTargets = 1;
    pso.GraphicsPipeline.RTVFormats[0] = rtvFormat;
    // No depth target is bound for the blit, so the pipeline declares none.
    pso.GraphicsPipeline.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;
    pso.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pso.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    // **Depth off, both test and write.** The blit covers every pixel and owes
    // nothing to what is already in the depth buffer -- and under reverse-Z
    // (D-convention, `kReverseZ`) a depth-tested fullscreen quad at z=0 would be
    // at the *far* plane and fail against a cleared buffer. Turning the test off
    // is correct rather than merely convenient.
    pso.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    pso.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
    pso.pVS = vs;
    pso.pPS = ps;
    // **Only the texture is MUTABLE; the constant buffer must stay STATIC, and
    // getting this wrong is silent.** Setting `DefaultVariableType` to MUTABLE
    // wholesale makes `BlitConstants` mutable too, so
    // `GetStaticVariableByName` returns null, the buffer is never bound, and
    // `g_Params` reads as zero -- which makes v a constant 0.5, so every output
    // pixel samples the source's middle row and the frame renders as vertical
    // bars of whatever that row happened to contain. It looks like a stretched
    // image, not like an unbound buffer, which is what made it worth this note.
    const Diligent::ShaderResourceVariableDesc variables[] = {
        {Diligent::SHADER_TYPE_PIXEL, "g_Source",
         Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
    };
    pso.PSODesc.ResourceLayout.Variables = variables;
    pso.PSODesc.ResourceLayout.NumVariables = 1;
    pso.PSODesc.ResourceLayout.DefaultVariableType =
        Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

    Diligent::SamplerDesc sampler;
    sampler.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    // Clamp, so a source and destination that differ in size sample the edge
    // rather than wrapping the opposite side of the image into view.
    sampler.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    const Diligent::ImmutableSamplerDesc immutableSamplers[] = {
        {Diligent::SHADER_TYPE_PIXEL, "g_Source", sampler},
    };
    pso.PSODesc.ResourceLayout.ImmutableSamplers = immutableSamplers;
    pso.PSODesc.ResourceLayout.NumImmutableSamplers = 1;

    BlitPipeline built;
    device->CreateGraphicsPipelineState(pso, &built.pso);
    if (!built.pso) {
        HP_LOG_ERROR(kLog, "the fullscreen blit pipeline could not be created");
        blitFailed = true;
        return nullptr;
    }

    // Not `if (auto* v = ...)`. A null here means the variable is not STATIC
    // after all, and the previous version skipped it silently -- which is
    // exactly how `g_Params` came to be zero with everything still "working".
    auto* constants =
        built.pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "BlitConstants");
    if (constants == nullptr) {
        HP_LOG_ERROR(kLog, "the blit constant buffer is not a static variable; the V flip "
                           "would silently be zero and the frame would render as vertical "
                           "bars. Refusing to present rather than showing that");
        blitFailed = true;
        return nullptr;
    }
    constants->Set(blitConstants);
    built.pso->CreateShaderResourceBinding(&built.srb, true);
    if (!built.srb) {
        blitFailed = true;
        return nullptr;
    }
    built.source = built.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Source");
    if (built.source == nullptr) {
        HP_LOG_ERROR(kLog, "the fullscreen blit shader has no g_Source variable");
        blitFailed = true;
        return nullptr;
    }
    return &blitPipelines.emplace(key, std::move(built)).first->second;
}

bool RenderLayer::Impl::blitTo(Diligent::ITexture* source, Diligent::ITextureView* destination) {
    if (source == nullptr || destination == nullptr || !context) {
        return false;
    }

    Diligent::ITextureView* sourceView =
        source->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    if (sourceView == nullptr) {
        if (!presentMismatchReported) {
            // A colour target created without BIND_SHADER_RESOURCE lands here.
            HP_LOG_WARN(kLog, "the blit source has no shader-resource view; it must be "
                              "created with BIND_SHADER_RESOURCE to be sampled");
            presentMismatchReported = true;
        }
        return false;
    }

    BlitPipeline* pipeline = ensureBlitPipeline(destination->GetDesc().Format);
    if (pipeline == nullptr) {
        if (!presentMismatchReported) {
            HP_LOG_WARN(kLog, "no blit pipeline; nothing can be drawn");
            presentMismatchReported = true;
        }
        return false;
    }

    {
        // Read from the device rather than assumed -- see the shader. This is
        // the same value `RenderLayer::clipSpace()` reports, taken from the same
        // place, so there is one answer to be wrong about rather than two.
        Diligent::MapHelper<float> params(context, blitConstants, Diligent::MAP_WRITE,
                                          Diligent::MAP_FLAG_DISCARD);
        params[0] = device->GetDeviceInfo().NDC.YtoVScale;
        params[1] = 0.0F;
        params[2] = 0.0F;
        params[3] = 0.0F;
    }

    // Bound without a depth-stencil view, matching the pipeline's declaration.
    context->SetRenderTargets(1, &destination, nullptr,
                              Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    pipeline->source->Set(sourceView);
    context->SetPipelineState(pipeline->pso);
    context->CommitShaderResources(pipeline->srb,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::DrawAttribs draw;
    draw.NumVertices = 3;
    draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
    context->Draw(draw);
    presentMismatchReported = false;
    return true;
}

bool RenderLayer::Impl::createVulkan(const Diligent::NativeWindow& window, int width, int height) {
#if DILIGENT_VK_EXPLICIT_LOAD
    auto* factoryFn = Diligent::LoadGraphicsEngineVk();
    if (factoryFn == nullptr) {
        HP_LOG_WARN(kLog, "Vulkan: the engine library could not be loaded");
        return false;
    }
    Diligent::IEngineFactoryVk* factory = factoryFn();
#else
    Diligent::IEngineFactoryVk* factory = Diligent::GetEngineFactoryVk();
#endif
    if (factory == nullptr) {
        return false;
    }
    // Per *factory*, not the global SetDebugMessageCallback.
    //
    // Each Diligent engine library carries its own copy of that global -- the
    // same statics-per-artifact property that has produced every other surprise
    // at this boundary (T0105.1, T0127). Setting it from libhp_engine sets
    // libhp_engine's copy, and GraphicsEngineVk.so keeps writing to stderr.
    // Measured: the global call compiled, ran, and changed nothing.
    factory->SetMessageCallback(&diligentMessage);

    Diligent::EngineVkCreateInfo info;
    // Validation layers on in debug (25.6). They are the only reason a wrong
    // resource lifetime or a bad barrier says anything at all; without them
    // both present as a driver hang somewhere unrelated.
    info.EnableValidation = config.validation;

    factory->CreateDeviceAndContextsVk(info, &device, &context);
    if (!device || !context) {
        HP_LOG_WARN(kLog, "Vulkan: device creation failed");
        device.Release();
        context.Release();
        return false;
    }

    Diligent::SwapChainDesc desc;
    describeSwapChain(desc);
    desc.Width = static_cast<Diligent::Uint32>(width);
    desc.Height = static_cast<Diligent::Uint32>(height);
    factory->CreateSwapChainVk(device, context, desc, window, &swapChain);
    if (!swapChain) {
        HP_LOG_WARN(kLog, "Vulkan: swap chain creation failed");
        context.Release();
        device.Release();
        return false;
    }

    active = RenderBackend::Vulkan;
    return true;
}

void RenderLayer::Impl::describeAdapter() {
    if (!device) {
        return;
    }
    const Diligent::GraphicsAdapterInfo& info = device->GetAdapterInfo();
    adapter = info.Description;
}

RenderLayer::RenderLayer(Window& window, RenderConfig config)
    : ILayer("render"), impl_(std::make_unique<Impl>()) {
    impl_->config = config;
    impl_->window = &window;
}

RenderLayer::~RenderLayer() = default;

void RenderLayer::onAttach() {
    HP_PROFILE_ZONE_NAMED("RenderLayer::onAttach");

    if (impl_->window == nullptr) {
        HP_LOG_INFO(kLog, "no window, so no device");
        return;
    }

    const Diligent::NativeWindow window = nativeWindowFrom(impl_->window->nativeHandles());
    const int width = impl_->window->width();
    const int height = impl_->window->height();

    // Vulkan, whatever was asked for -- `Default` and `Vulkan` are the same
    // device since D29 removed the OpenGL fallback. The switch stays so a new
    // enumerator cannot be added without deciding what it creates.
    bool ok = false;
    switch (impl_->config.backend) {
    case RenderBackend::Vulkan:
    case RenderBackend::Default:
        ok = impl_->createVulkan(window, width, height);
        break;
    }

    if (!ok) {
        // **The only failure mode there is, so it has to be a good one** (D29,
        // T0144.2). There is no fallback: a machine that cannot create a Vulkan
        // device does not render, full stop, and this message is what the
        // player's log ends with. Name the requirement, the floor and the
        // usual causes rather than leaving "no device" to be triaged as an
        // engine crash. The layer stays inert -- every other method checks for
        // that and does nothing -- so the failure is this message, never a
        // crash and never silence.
        HP_LOG_ERROR(kLog,
                     "no graphics device could be created ({} requested). Vulkan is the "
                     "engine's only graphics backend (D29) -- there is no OpenGL or other "
                     "fallback. This machine either has no Vulkan driver installed, no GPU "
                     "that supports Vulkan 1.0 (2012-class hardware or newer: NVIDIA Kepler, "
                     "AMD GCN, Intel Skylake), or is a VM/remote session with no GPU "
                     "acceleration. Updating the graphics driver from the GPU vendor is the "
                     "usual fix; the renderer will not start until a Vulkan device exists.",
                     backendName(impl_->config.backend));
        return;
    }

    impl_->describeAdapter();
    g_activeBackendName = backendName(impl_->active);
    g_activeAdapter = impl_->adapter.c_str();

    // Report what was *created*, not what was asked for. The surface overrides
    // both: a request for 2 buffers came back as 3 here ("minimal image count
    // supported for this surface"), and an sRGB RGBA format came back as BGRA.
    // Logging the request would have quietly told a lie that a latency
    // investigation would later act on.
    const auto& created = impl_->swapChain->GetDesc();
    HP_LOG_INFO(kLog, "{} on '{}', {}x{}, {} buffers, vsync {}", backendName(impl_->active),
                impl_->adapter, created.Width, created.Height, created.BufferCount,
                impl_->config.vsync ? "on" : "off");
    if (created.BufferCount != impl_->config.bufferCount) {
        HP_LOG_INFO(kLog, "swap chain buffer count {} requested, {} granted by the surface",
                    impl_->config.bufferCount, created.BufferCount);
    }
}

void RenderLayer::onDetach() {
    HP_PROFILE_ZONE_NAMED("RenderLayer::onDetach");
    if (!impl_->device) {
        return;
    }

    // Ordering is the whole of 25.4, and it is not arbitrary. Every command
    // still in flight must retire before anything it references is released;
    // then the swap chain, then the context, then the device. Releasing the
    // device first is not a crash here -- it is a validation-layer complaint
    // now and a driver hang later, on someone else's machine.
    if (impl_->context) {
        impl_->context->Flush();
        impl_->context->WaitForIdle();
    }
    // The blit pipeline goes here, and **leaving it out hangs the process on
    // exit** (T0137). These are device-owned objects held in `Impl`, so without
    // this they are released when `Impl` is destroyed -- which is *after* the
    // device below, and is precisely the "validation-layer complaint now and a
    // driver hang later" the comment above describes. It was not a later
    // machine and it was not subtle: closing the window force-quit.
    impl_->blitPipelines.clear();
    impl_->blitConstants.Release();

    impl_->swapChain.Release();
    impl_->context.Release();
    impl_->device.Release();
    HP_LOG_INFO(kLog, "device released");
}

void RenderLayer::onRender() {
    if (!impl_->swapChain) {
        return;
    }
    HP_PROFILE_ZONE_NAMED("RenderLayer::onRender");

    Diligent::ITextureView* rtv = impl_->swapChain->GetCurrentBackBufferRTV();
    Diligent::ITextureView* dsv = impl_->swapChain->GetDepthBufferDSV();
    impl_->context->SetRenderTargets(1, &rtv, dsv,
                                     Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    impl_->context->ClearRenderTarget(rtv, impl_->clearColour.data(),
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (dsv != nullptr) {
        impl_->context->ClearDepthStencil(dsv, Diligent::CLEAR_DEPTH_FLAG, 1.0F, 0,
                                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    // The present path (T0028, fixed by T0137).
    //
    // **A sampled fullscreen pass, not a copy.** The copy this replaced needed
    // an exact format match, which Vulkan never has -- its surface is BGRA
    // against the scene target's RGBA -- so the engine's default backend showed
    // a clear colour and looked broken. Sampling converts format and colour
    // space in hardware and handles a size difference besides.
    if (impl_->presentSource != nullptr) {
        HP_PROFILE_ZONE_NAMED("present blit");
        impl_->blitTo(impl_->presentSource, rtv);
    }

    // Phase 11. The sync interval is state rather than a literal, which is what
    // makes T0110's runtime toggle a one-liner instead of a rewrite.
    {
        HP_PROFILE_ZONE_NAMED("present");
        impl_->swapChain->Present(impl_->config.vsync ? 1U : 0U);
    }
}

bool RenderLayer::blitTexture(Diligent::ITexture* source, Diligent::ITextureView* destination) {
    return impl_ && impl_->blitTo(source, destination);
}

void RenderLayer::setPresentSource(Diligent::ITexture* texture) {
    // **Only reset the once-per-source warning when the source actually
    // changes.** Resetting unconditionally made "report once" mean "report every
    // frame", because the caller sets this every frame -- which is exactly the
    // log-burying failure the warning's own comment warns about, committed three
    // lines away from it.
    if (impl_->presentSource != texture) {
        impl_->presentSource = texture;
        impl_->presentMismatchReported = false;
    }
}

void RenderLayer::onEvent(Event& event) {
    if (event.type() != EventType::WindowResize) {
        return;
    }
    const auto& resized = static_cast<WindowResizeEvent&>(event);
    resize(resized.width(), resized.height());
    // Deliberately not consumed. A resize is news for everyone -- the UI needs
    // it, a camera needs the new aspect ratio -- and a render layer swallowing
    // it would break them in a way that looks like a layout bug.
}

bool RenderLayer::ready() const {
    return static_cast<bool>(impl_->swapChain);
}

RenderBackend RenderLayer::backend() const {
    return impl_->active;
}

std::string RenderLayer::adapterDescription() const {
    return impl_->adapter;
}

void RenderLayer::resize(int width, int height) {
    if (!impl_->swapChain || width <= 0 || height <= 0) {
        return;
    }
    const Diligent::SwapChainDesc& desc = impl_->swapChain->GetDesc();
    if (desc.Width == static_cast<Diligent::Uint32>(width) &&
        desc.Height == static_cast<Diligent::Uint32>(height)) {
        return;
    }
    impl_->swapChain->Resize(static_cast<Diligent::Uint32>(width),
                             static_cast<Diligent::Uint32>(height));
    HP_LOG_DEBUG(kLog, "swap chain resized to {}x{}", width, height);
}

int RenderLayer::swapChainWidth() const {
    return impl_->swapChain ? static_cast<int>(impl_->swapChain->GetDesc().Width) : 0;
}

int RenderLayer::swapChainHeight() const {
    return impl_->swapChain ? static_cast<int>(impl_->swapChain->GetDesc().Height) : 0;
}

bool RenderLayer::vsync() const {
    return impl_->config.vsync;
}

void RenderLayer::setVsync(bool enabled) {
    if (impl_->config.vsync == enabled) {
        return;
    }
    impl_->config.vsync = enabled;
    // No explicit recreate: Diligent notices the sync interval changed at the
    // next Present and rebuilds the swap chain itself. Doing it here as well
    // would rebuild twice.
    HP_LOG_INFO(kLog, "vsync {} -- the swap chain rebuilds on the next present",
                enabled ? "on" : "off");
}

void RenderLayer::setClearColour(float r, float g, float b, float a) {
    impl_->clearColour = {r, g, b, a};
}

Diligent::IRenderDevice* RenderLayer::device() const {
    return impl_ ? impl_->device.RawPtr() : nullptr;
}

Diligent::IDeviceContext* RenderLayer::context() const {
    return impl_ ? impl_->context.RawPtr() : nullptr;
}

Diligent::ISwapChain* RenderLayer::swapChain() const {
    return impl_ ? impl_->swapChain.RawPtr() : nullptr;
}

ClipSpace RenderLayer::clipSpace() const {
    ClipSpace clip;
    if (impl_ && impl_->device) {
        const Diligent::NDCAttribs& ndc = impl_->device->GetDeviceInfo().NDC;
        clip.minZ = ndc.MinZ;
        clip.yToV = ndc.YtoVScale;
    }
    return clip;
}

} // namespace hp
