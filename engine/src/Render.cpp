// Device, context and swap chain (T0025). See hp/Render.hpp for the rules.
//
// This is the only translation unit in the engine that includes Diligent, and
// keeping it that way is what lets the link stay PRIVATE.
#include <hp/Render.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>
#include <hp/Window.hpp>

#include <array>
#include <cstdlib>
#include <cstring>

// Must precede the factory headers: their LoadGraphicsEngine* helpers are
// inline and call LoadEngineDll, which lives here and is Windows-only.
#if defined(_WIN32)
#include <LoadEngineDll.h>
#endif

#include <EngineFactoryOpenGL.h>
#include <EngineFactoryVk.h>

#include <RefCntAutoPtr.hpp>

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
    case RenderBackend::OpenGL:
        return "OpenGL";
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

    /// Fills `desc` from the config. Separate so both backends get the same
    /// answer -- a buffer count that differs by backend is a latency difference
    /// nobody chose.
    void describeSwapChain(Diligent::SwapChainDesc& desc) const {
        desc.BufferCount = config.bufferCount;
    }

    bool createVulkan(const Diligent::NativeWindow& window, int width, int height);
    bool createOpenGL(const Diligent::NativeWindow& window, int width, int height);
    void describeAdapter();
};

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

bool RenderLayer::Impl::createOpenGL(const Diligent::NativeWindow& window, int width, int height) {
#if DILIGENT_OPENGL_EXPLICIT_LOAD
    auto* factoryFn = Diligent::LoadGraphicsEngineOpenGL();
    if (factoryFn == nullptr) {
        HP_LOG_WARN(kLog, "OpenGL: the engine library could not be loaded");
        return false;
    }
    Diligent::IEngineFactoryOpenGL* factory = factoryFn();
#else
    Diligent::IEngineFactoryOpenGL* factory = Diligent::GetEngineFactoryOpenGL();
#endif
    if (factory == nullptr) {
        return false;
    }
    factory->SetMessageCallback(&diligentMessage);

    Diligent::EngineGLCreateInfo info;
    info.Window = window;

    Diligent::SwapChainDesc desc;
    describeSwapChain(desc);
    desc.Width = static_cast<Diligent::Uint32>(width);
    desc.Height = static_cast<Diligent::Uint32>(height);

    // GL creates device, context and swap chain in one call -- the context is
    // the GL context, which is inseparable from the window it was made against.
    factory->CreateDeviceAndSwapChainGL(info, &device, &context, desc, &swapChain);
    if (!device || !context || !swapChain) {
        HP_LOG_WARN(kLog, "OpenGL: device or swap chain creation failed");
        swapChain.Release();
        context.Release();
        device.Release();
        return false;
    }

    // D15's floor. Particles are GPU-compute-only, so a GL device without
    // compute shaders cannot run them -- and the failure a floor prevents is
    // not a crash, it is an emitter that dispatches nothing and a scene that is
    // quietly missing its effects. Fail here, where the message can say why.
    const Diligent::DeviceFeatures& features = device->GetDeviceInfo().Features;
    if (features.ComputeShaders != Diligent::DEVICE_FEATURE_STATE_ENABLED) {
        HP_LOG_ERROR(kLog, "OpenGL device has no compute shader support. D15 sets an OpenGL 4.3 "
                           "floor because particles are GPU-compute-only; a device below it would "
                           "run with effects silently absent. Refusing the device rather than "
                           "starting without it.");
        swapChain.Release();
        context.Release();
        device.Release();
        return false;
    }

    active = RenderBackend::OpenGL;
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

    // Vulkan first, OpenGL second, and the order is the whole of "Default".
    // A requested backend is *not* silently substituted: asking for Vulkan and
    // getting GL would make a bug report unreadable.
    bool ok = false;
    switch (impl_->config.backend) {
    case RenderBackend::Vulkan:
        ok = impl_->createVulkan(window, width, height);
        break;
    case RenderBackend::OpenGL:
        ok = impl_->createOpenGL(window, width, height);
        break;
    case RenderBackend::Default:
        ok = impl_->createVulkan(window, width, height);
        if (!ok) {
            HP_LOG_INFO(kLog, "Vulkan unavailable, falling back to OpenGL");
            ok = impl_->createOpenGL(window, width, height);
        }
        break;
    }

    if (!ok) {
        HP_LOG_ERROR(kLog, "no graphics device could be created ({} requested)",
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

    // Phase 11. The sync interval is state rather than a literal, which is what
    // makes T0110's runtime toggle a one-liner instead of a rewrite.
    {
        HP_PROFILE_ZONE_NAMED("present");
        impl_->swapChain->Present(impl_->config.vsync ? 1U : 0U);
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

} // namespace hp
