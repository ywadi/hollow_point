// Layers and the layer stack (T0017).
//
// A layer is a slice of the frame that can update, render and see input. The
// editor UI is a layer, the game world is a layer, a pause menu is a layer.
// `Application` owns the stack and nothing else -- if a feature is being added
// to `Application`, it is almost certainly a layer instead (T0014).
//
// **Update goes bottom-up, events go top-down**, and that asymmetry is the
// whole point. The world should simulate before the UI drawn over it, so update
// runs from the bottom. But the topmost thing on screen should get first refusal
// on a click, so events run from the top. A stack that walked both the same way
// would be wrong in one direction.
//
// **Overlays sit above normal layers** and stay there. Without that, pushing a
// gameplay layer after the editor UI would silently put the world on top of the
// interface -- the ordering bug is invisible until someone clicks.
#pragma once

#include <hp/Api.hpp>
#include <hp/Event.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hp {

class HP_API ILayer {
public:
    /// @param name identifies the layer in logs and in the profiler. Not unique
    ///        and not an identifier -- it is for a human reading a capture.
    explicit ILayer(std::string_view name = "Layer") : name_(name) {}

    virtual ~ILayer() = default;

    ILayer(const ILayer&) = delete;
    ILayer& operator=(const ILayer&) = delete;

    /// Called when the layer joins the stack, and when it leaves.
    ///
    /// Detach ordering matters and is easy to get wrong later: layers must
    /// release GPU resources here, while the device still exists (T0025).
    virtual void onAttach() {}

    virtual void onDetach() {}

    /// @param deltaSeconds scaled frame delta.
    virtual void onUpdate(double deltaSeconds) { (void)deltaSeconds; }

    virtual void onRender() {}

    /// @param event the event, which may be consumed to stop it descending.
    virtual void onEvent(Event& event) { (void)event; }

    std::string_view name() const { return name_; }

private:
    std::string name_;
};

/// Ordered layers, with overlays pinned above.
///
/// Owns what it holds. A layer is `unique_ptr` rather than a raw pointer because
/// the alternative -- the caller keeping ownership -- makes lifetime a question
/// every caller has to answer correctly, and getting it wrong means a stack
/// walking into freed memory during shutdown.
class HP_API LayerStack {
public:
    LayerStack() = default;
    ~LayerStack();

    LayerStack(const LayerStack&) = delete;
    LayerStack& operator=(const LayerStack&) = delete;

    /// Adds below any overlays. @param layer the layer; ownership is taken.
    /// @returns a borrowed pointer, valid until the layer is popped.
    ILayer* push(std::unique_ptr<ILayer> layer);

    /// Adds above everything, and above later non-overlay pushes.
    /// @param overlay the layer; ownership is taken.
    /// @returns a borrowed pointer, valid until it is popped.
    ILayer* pushOverlay(std::unique_ptr<ILayer> overlay);

    /// @param layer the layer to remove; `onDetach` runs before it is destroyed.
    /// @returns true if it was in the stack.
    bool pop(ILayer* layer);

    /// Detaches everything, top-down. Called by the destructor; safe twice.
    void clear();

    /// Bottom-up: the world simulates before the interface drawn over it.
    void update(double deltaSeconds);
    void render();

    /// Top-down, stopping at the first layer that consumes.
    /// @param event the event to deliver.
    void dispatch(Event& event);

    std::size_t size() const { return layers_.size(); }

    std::size_t overlayCount() const { return overlayCount_; }

    /// @param index position from the bottom. @returns the layer, or null.
    ILayer* at(std::size_t index) const;

private:
    // One vector, with normal layers below `insert_` and overlays above it.
    // Two vectors would make "dispatch top-down across both" a special case at
    // every call site rather than in one place.
    std::vector<std::unique_ptr<ILayer>> layers_;
    std::size_t insert_ = 0;
    std::size_t overlayCount_ = 0;
};

} // namespace hp
