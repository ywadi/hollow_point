#include <hp/SceneRenderLayer.hpp>

#include <hp/Assets.hpp>
#include <hp/CameraSystem.hpp>
#include <hp/Log.hpp>
#include <hp/Profiling.hpp>
#include <hp/Scene.hpp>

#include <DeviceContext.h>

#include <optional>

namespace hp {
namespace {

const LogCategory kLog("render.scenelayer");

} // namespace

SceneRenderLayer::SceneRenderLayer(const char* name)
    : name_(name == nullptr ? "scene" : name) {}

SceneRenderLayer::~SceneRenderLayer() = default;

bool SceneRenderLayer::create(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                              TargetFormat colour, TargetFormat depth) {
    // Derived from `useDepth` rather than asked for separately, so there is one
    // place that decides and not two that can disagree. The disagreement is
    // still possible -- `useDepth` is public data and can be flipped after this
    // -- which is what `onRenderLayer`'s guard is for.
    createdWithDepth_ = useDepth;
    warnedDepthMismatch_ = false;
    return renderer_.create(device, context, colour,
                            useDepth ? std::optional<TargetFormat>(depth) : std::nullopt);
}

void SceneRenderLayer::release() {
    renderer_.release();
}

bool SceneRenderLayer::valid() const {
    return renderer_.valid();
}

void SceneRenderLayer::setScene(Scene* scene, const AssetPool* pool) {
    scene_ = scene;
    pool_ = pool;
}

Scene* SceneRenderLayer::scene() const {
    return scene_;
}

const char* SceneRenderLayer::name() const {
    return name_;
}

void SceneRenderLayer::onRenderLayer(const RenderPassContext& pass) {
    HP_PROFILE_ZONE();

    lastFrame = DrawSubmitStats{};
    lastFrameHadCamera = false;

    if (!renderer_.valid() || scene_ == nullptr || pool_ == nullptr || pass.context == nullptr) {
        return;
    }
    if (pass.width <= 0 || pass.height <= 0) {
        return;
    }

    // **The pipeline state and the bound targets must agree about depth.** A
    // state built with a DSV format, drawn with nothing bound (or the reverse),
    // is a render-pass incompatibility — a validation error on Vulkan, not a
    // slightly wrong image. It happens two ways that both look like nothing:
    // `useDepth` flipped after `create`, or a stack that has no depth target at
    // all. Named here rather than left to the driver.
    if ((pass.depth != nullptr) != createdWithDepth_) {
        if (!warnedDepthMismatch_) {
            warnedDepthMismatch_ = true;
            HP_LOG_ERROR(kLog,
                         "layer '{}': created for {} depth but the stack bound {}; skipping. "
                         "Set `useDepth` before calling create().",
                         name_, createdWithDepth_ ? "a" : "no",
                         pass.depth != nullptr ? "one" : "none");
        }
        return;
    }

    const std::optional<Entity> camera = resolveCamera(*scene_, viewSlot);
    if (!camera) {
        // Returning here leaves the stack's clear in place, which is the whole
        // reason the stack clears before calling rather than leaving it to the
        // layer: a slot with no camera shows the background, not last frame.
        // Debug rather than warning -- a scene under construction legitimately
        // has no camera, and a per-frame warning trains people to ignore the log.
        HP_LOG_DEBUG(kLog, "layer '{}': no enabled camera on view slot {}", name_,
                     static_cast<int>(viewSlot));
        return;
    }

    const std::optional<ResolvedView> view =
        buildView(*camera, pass.width, pass.height, pass.clip);
    if (!view) {
        HP_LOG_WARN(kLog,
                    "layer '{}': a camera was found on view slot {} but no view could be built "
                    "from it; check its near/far planes",
                    name_, static_cast<int>(viewSlot));
        return;
    }
    lastFrameHadCamera = true;

    // **The viewport comes from the resolved view, not from the pass.** They
    // differ under a letterboxing aspect policy, and using the pass size
    // produces an image stretched by exactly the letterbox ratio (T0081).
    Diligent::Viewport viewport;
    viewport.TopLeftX = static_cast<float>(view->viewportX);
    viewport.TopLeftY = static_cast<float>(view->viewportY);
    viewport.Width = static_cast<float>(view->viewportWidth);
    viewport.Height = static_cast<float>(view->viewportHeight);
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;
    pass.context->SetViewports(1, &viewport, pass.width, pass.height);

    drawList_ = parseScene(*scene_);
    renderer_.render(pass.context, drawList_, *view, *pool_, &lastFrame);
}

void configureAsHud(SceneRenderLayer& layer, std::uint8_t slot, int order) {
    layer.viewSlot = slot;
    layer.order = order;
    layer.useDepth = false;
    layer.clear = LayerClear::None;
}

} // namespace hp
