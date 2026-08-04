#include <hp/Layer.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>

#include <algorithm>

namespace hp {
namespace {
const LogCategory kLog("layer");
} // namespace

LayerStack::~LayerStack() {
    clear();
}

ILayer* LayerStack::push(std::unique_ptr<ILayer> layer) {
    if (!layer) {
        return nullptr;
    }
    ILayer* raw = layer.get();
    layers_.insert(layers_.begin() + static_cast<std::ptrdiff_t>(insert_), std::move(layer));
    ++insert_;
    HP_LOG_DEBUG(kLog, "pushed '{}' ({} layers, {} overlays)", raw->name(), layers_.size(),
                 overlayCount_);
    raw->onAttach();
    return raw;
}

ILayer* LayerStack::pushOverlay(std::unique_ptr<ILayer> overlay) {
    if (!overlay) {
        return nullptr;
    }
    ILayer* raw = overlay.get();
    layers_.push_back(std::move(overlay));
    ++overlayCount_;
    HP_LOG_DEBUG(kLog, "pushed overlay '{}' ({} layers, {} overlays)", raw->name(), layers_.size(),
                 overlayCount_);
    raw->onAttach();
    return raw;
}

bool LayerStack::pop(ILayer* layer) {
    const auto it = std::find_if(layers_.begin(), layers_.end(),
                                 [layer](const auto& held) { return held.get() == layer; });
    if (it == layers_.end()) {
        return false;
    }
    const auto index = static_cast<std::size_t>(it - layers_.begin());
    const bool wasOverlay = index >= insert_;

    // Detach before destroying, and before the stack shrinks: a layer's
    // onDetach may legitimately look at the stack it is leaving.
    (*it)->onDetach();
    layers_.erase(it);
    if (wasOverlay) {
        --overlayCount_;
    } else {
        --insert_;
    }
    return true;
}

void LayerStack::clear() {
    // Top-down, mirroring dispatch: a layer above may depend on one below still
    // existing while it tears down, never the reverse.
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
        (*it)->onDetach();
    }
    layers_.clear();
    insert_ = 0;
    overlayCount_ = 0;
}

void LayerStack::fixedUpdate(double fixedStepSeconds) {
    HP_PROFILE_ZONE();
    for (const auto& layer : layers_) {
        HP_PROFILE_ZONE_NAMED("layer fixedUpdate");
        layer->onFixedUpdate(fixedStepSeconds);
    }
}

void LayerStack::update(double deltaSeconds) {
    HP_PROFILE_ZONE();
    for (const auto& layer : layers_) {
        HP_PROFILE_ZONE_NAMED("layer update");
        layer->onUpdate(deltaSeconds);
    }
}

void LayerStack::lateUpdate(double deltaSeconds) {
    HP_PROFILE_ZONE();
    for (const auto& layer : layers_) {
        HP_PROFILE_ZONE_NAMED("layer lateUpdate");
        layer->onLateUpdate(deltaSeconds);
    }
}

void LayerStack::render() {
    HP_PROFILE_ZONE();
    for (const auto& layer : layers_) {
        HP_PROFILE_ZONE_NAMED("layer render");
        layer->onRender();
    }
}

void LayerStack::dispatch(Event& event) {
    HP_PROFILE_ZONE();
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
        if (event.isConsumed()) {
            return;
        }
        (*it)->onEvent(event);
    }
}

ILayer* LayerStack::at(std::size_t index) const {
    return index < layers_.size() ? layers_[index].get() : nullptr;
}

} // namespace hp
