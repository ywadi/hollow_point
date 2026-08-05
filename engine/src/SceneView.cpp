#include <hp/SceneView.hpp>

#include <hp/Assets.hpp>
#include <hp/CameraSystem.hpp>
#include <hp/Light.hpp>
#include <hp/DepthConvention.hpp>
#include <hp/Log.hpp>
#include <hp/Profiling.hpp>
#include <hp/Scene.hpp>

#include <DeviceContext.h>
#include <RenderDevice.h>
#include <Texture.h>
#include <TextureView.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

namespace hp {
namespace {

const LogCategory kLog("render.view");

/// The two targets this view owns. Names are internal; nothing outside looks
/// them up, which is why they are not part of the public surface.
constexpr const char* kColourTarget = "scene.colour";
constexpr const char* kDepthTarget = "scene.depth";

} // namespace

struct SceneView::Impl {
    Diligent::IRenderDevice* device = nullptr;
    FrameTargets targets;
    SceneRenderer renderer;
    std::array<float, 4> clearColour{0.16F, 0.22F, 0.34F, 1.0F};

    /// Reused between frames so a frame does not allocate a draw list.
    DrawList drawList;
};

SceneView::SceneView() = default;
SceneView::~SceneView() = default;
SceneView::SceneView(SceneView&&) noexcept = default;
SceneView& SceneView::operator=(SceneView&&) noexcept = default;

bool SceneView::create(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                       int width, int height, TargetFormat colour) {
    HP_PROFILE_ZONE();

    if (device == nullptr || context == nullptr) {
        HP_LOG_ERROR(kLog, "create() needs a device and a context");
        return false;
    }

    auto impl = std::make_unique<Impl>();
    impl->device = device;

    impl->targets.declare(FrameTargetDesc{kColourTarget, colour, 1.0F});
    impl->targets.declare(FrameTargetDesc{kDepthTarget, TargetFormat::Depth, 1.0F});

    if (!impl->targets.create(device, std::max(width, 1), std::max(height, 1))) {
        HP_LOG_ERROR(kLog, "could not create the scene view's targets");
        return false;
    }

    if (!impl->renderer.create(device, context, colour, TargetFormat::Depth)) {
        // The renderer already said why.
        return false;
    }

    impl_ = std::move(impl);
    HP_LOG_INFO(kLog, "scene view ready, {}x{}", width, height);
    return true;
}

bool SceneView::resize(int width, int height) {
    if (!valid()) {
        return false;
    }
    // `FrameTargets::resize` is already a no-op when the size is unchanged, so
    // this stays cheap to call every frame rather than needing a caller-side
    // guard that each caller would get slightly wrong.
    return impl_->targets.resize(std::max(width, 1), std::max(height, 1));
}

void SceneView::release() {
    impl_.reset();
}

bool SceneView::valid() const {
    return impl_ != nullptr && impl_->targets.ready() && impl_->renderer.valid();
}

void SceneView::setClearColour(float r, float g, float b, float a) {
    if (impl_ != nullptr) {
        impl_->clearColour = {r, g, b, a};
    }
}

Diligent::ITextureView* SceneView::colour() const {
    return impl_ == nullptr ? nullptr : impl_->targets.shaderResource(kColourTarget);
}

Diligent::ITexture* SceneView::colourTexture() const {
    Diligent::ITextureView* view = colour();
    return view == nullptr ? nullptr : view->GetTexture();
}

bool SceneView::readback(Diligent::IDeviceContext* context,
                         std::vector<std::uint8_t>& outRgba) const {
    // Delegated rather than reimplemented. This used to be a copy of the staging
    // texture, the flush-and-wait and the row-by-row stride walk; a second
    // consumer (T0027's composite test) needed the same on a target this class
    // does not own, and two copies of a readback is how one of them keeps the
    // stride bug the other fixed.
    outRgba.clear();
    if (!valid()) {
        return false;
    }
    return impl_->targets.readback(context, kColourTarget, outRgba);
}

int SceneView::width() const {
    return impl_ == nullptr ? 0 : impl_->targets.width();
}

int SceneView::height() const {
    return impl_ == nullptr ? 0 : impl_->targets.height();
}

Diligent::ITextureView* SceneView::render(Diligent::IDeviceContext* context, Scene& scene,
                                          const AssetPool& pool, ClipSpace clip,
                                          std::uint8_t viewSlot, SceneViewStats* stats) {
    HP_PROFILE_ZONE();

    SceneViewStats counted;
    const auto report = [&]() {
        if (stats != nullptr) {
            *stats = counted;
        }
    };

    if (!valid() || context == nullptr) {
        report();
        return nullptr;
    }
    Impl& impl = *impl_;

    Diligent::ITextureView* colourTarget = impl.targets.renderTarget(kColourTarget);
    Diligent::ITextureView* depthTarget = impl.targets.depthStencil(kDepthTarget);
    if (colourTarget == nullptr || depthTarget == nullptr) {
        report();
        return nullptr;
    }

    // Bind and clear first, and unconditionally. A frame with no camera still
    // clears — otherwise the viewport shows whatever the previous frame left,
    // which reads as "the renderer froze" rather than "there is no camera".
    context->SetRenderTargets(1, &colourTarget, depthTarget,
                              Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->ClearRenderTarget(colourTarget, impl.clearColour.data(),
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    // `kDepthClearValue`, not 1.0 — reverse-Z puts the far plane at 0 (T0130).
    // Spelled as the constant so the day that convention is revisited, this line
    // moves with it instead of being the one place that quietly did not.
    context->ClearDepthStencil(depthTarget, Diligent::CLEAR_DEPTH_FLAG, kDepthClearValue, 0,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const std::optional<Entity> camera = resolveCamera(scene, viewSlot);
    if (!camera) {
        // **"Renders nothing and says so, rather than crashing."** Logged at
        // debug rather than warning: a scene under construction legitimately has
        // no camera, and a warning every frame would train people to ignore the
        // log. The cleared frame is still published so the viewport shows the
        // clear colour rather than a stale image.
        HP_LOG_DEBUG(kLog, "no enabled camera on view slot {}; nothing to render",
                     static_cast<int>(viewSlot));
        report();
        return impl.targets.shaderResource(kColourTarget);
    }

    const std::optional<ResolvedView> view =
        buildView(*camera, impl.targets.width(), impl.targets.height(), clip);
    if (!view) {
        HP_LOG_WARN(kLog, "a camera was found on view slot {} but no view could be built from "
                          "it; check its near/far planes",
                    static_cast<int>(viewSlot));
        report();
        return impl.targets.shaderResource(kColourTarget);
    }
    counted.hadCamera = true;

    // **The viewport comes from the resolved view, not from the target.** Under
    // a letterboxing aspect policy they differ, and using the target's size
    // produces an image stretched by exactly the letterbox ratio (T0081).
    Diligent::Viewport viewport;
    viewport.TopLeftX = static_cast<float>(view->viewportX);
    viewport.TopLeftY = static_cast<float>(view->viewportY);
    viewport.Width = static_cast<float>(view->viewportWidth);
    viewport.Height = static_cast<float>(view->viewportHeight);
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;
    context->SetViewports(1, &viewport, impl.targets.width(), impl.targets.height());

    DrawParseStats parsed;
    // The resolved camera's mask, so an object on a layer this camera does not
    // render is never submitted (T0085).
    impl.drawList = parseScene(scene, view->camera.cullingMask, &parsed);
    counted.considered = parsed.considered;
    counted.culledByLayer = parsed.culledByLayer;

    // Frame-wide for now: every light lights everything. Per-object selection
    // and the illumination layer mask are T0079.3/79.5.
    const LightList lights = gatherLights(scene);

    DrawSubmitStats submitted;
    impl.renderer.render(context, impl.drawList, *view, pool, lights, &submitted);
    counted.submitted = submitted.submitted;
    counted.missingMesh = submitted.missingMesh;

    report();
    return impl.targets.shaderResource(kColourTarget);
}

} // namespace hp
