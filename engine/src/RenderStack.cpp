#include <hp/RenderStack.hpp>

#include <hp/FrameTargets.hpp>
#include <hp/Log.hpp>
#include <hp/Profiling.hpp>

#include <algorithm>
#include <utility>

#include <DeviceContext.h>

namespace hp {
namespace {

const LogCategory kLog("render.stack");

bool clearsColour(LayerClear clear) {
    return clear == LayerClear::Colour || clear == LayerClear::ColourAndDepth;
}

bool clearsDepth(LayerClear clear) {
    return clear == LayerClear::Depth || clear == LayerClear::ColourAndDepth;
}

} // namespace

IRenderLayer::~IRenderLayer() = default;

struct RenderStack::Impl {
    std::vector<IRenderLayer*> layers;

    void sort() {
        // Stable, so two layers at the same order keep insertion order rather
        // than swapping arbitrarily between runs -- which would make a
        // composite that depends on their order intermittently wrong.
        std::stable_sort(layers.begin(), layers.end(),
                         [](const IRenderLayer* a, const IRenderLayer* b) {
                             return a->order < b->order;
                         });
    }
};

RenderStack::RenderStack() : impl_(std::make_unique<Impl>()) {}

RenderStack::~RenderStack() = default;

RenderStack::RenderStack(RenderStack&& other) noexcept = default;

RenderStack& RenderStack::operator=(RenderStack&& other) noexcept = default;

void RenderStack::add(IRenderLayer* layer) {
    if (layer == nullptr) {
        return;
    }
    const auto existing = std::find(impl_->layers.begin(), impl_->layers.end(), layer);
    if (existing != impl_->layers.end()) {
        // Refused rather than allowed: a layer added twice draws twice, which
        // presents as a blending or alpha bug rather than as a double-add.
        HP_LOG_ERROR(kLog, "layer '{}' added twice; ignoring the second", layer->name());
        return;
    }
    impl_->layers.push_back(layer);
    impl_->sort();
}

bool RenderStack::remove(IRenderLayer* layer) {
    const auto found = std::find(impl_->layers.begin(), impl_->layers.end(), layer);
    if (found == impl_->layers.end()) {
        return false;
    }
    impl_->layers.erase(found);
    return true;
}

void RenderStack::clear() {
    impl_->layers.clear();
}

void RenderStack::reorder() {
    impl_->sort();
}

const std::vector<IRenderLayer*>& RenderStack::layers() const {
    return impl_->layers;
}

std::size_t RenderStack::size() const {
    return impl_->layers.size();
}

std::size_t RenderStack::render(Diligent::IRenderDevice* device,
                                Diligent::IDeviceContext* context,
                                Diligent::ITextureView* colour, Diligent::ITextureView* depth,
                                FrameTargets* targets, int width, int height, ClipSpace clip,
                                double timeSeconds) {
    HP_PROFILE_ZONE();

    if (context == nullptr || colour == nullptr) {
        return 0;
    }

    std::size_t rendered = 0;
    for (auto* layer : impl_->layers) {
        if (!layer->enabled) {
            continue;
        }

        HP_PROFILE_ZONE_NAMED(layer->name());

        // Per-layer depth, which is the whole reason this is not one global
        // bind: a HUD layer sets useDepth false and never depth-tests against
        // world geometry.
        Diligent::ITextureView* boundDepth = layer->useDepth ? depth : nullptr;
        context->SetRenderTargets(1, &colour, boundDepth,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        if (clearsColour(layer->clear)) {
            context->ClearRenderTarget(colour, layer->clearColour,
                                       Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        if (clearsDepth(layer->clear) && boundDepth != nullptr) {
            context->ClearDepthStencil(boundDepth, Diligent::CLEAR_DEPTH_FLAG, layer->clearDepth, 0,
                                       Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }

        RenderPassContext pass;
        pass.device = device;
        pass.context = context;
        pass.timeSeconds = timeSeconds;
        pass.targets = targets;
        pass.colour = colour;
        pass.depth = boundDepth;
        pass.width = width;
        pass.height = height;
        pass.clip = clip;

        layer->onRenderLayer(pass);
        ++rendered;
    }
    return rendered;
}

} // namespace hp
