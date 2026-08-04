# `<hp/Layer.hpp>`

*Generated from `engine/include/hp/Layer.hpp` — do not edit.*

```cpp
#include <hp/Layer.hpp>
```

28 public declaration(s), 16 documented.

## `ILayer`

```cpp
class ILayer
```

*No documentation comment.*

## `ILayer::ILayer`

```cpp
ILayer(std::string_view name)
```

 @param name identifies the layer in logs and in the profiler. Not unique
        and not an identifier -- it is for a human reading a capture.

## `ILayer::ILayer`

```cpp
ILayer(const ILayer &)
```

*No documentation comment.*

## `ILayer::operator=`

```cpp
ILayer & operator=(const ILayer &)
```

*No documentation comment.*

## `ILayer::onAttach`

```cpp
void onAttach()
```

 Called when the layer joins the stack, and when it leaves.

 Detach ordering matters and is easy to get wrong later: layers must
 release GPU resources here, while the device still exists (T0025).

## `ILayer::onDetach`

```cpp
void onDetach()
```

*No documentation comment.*

## `ILayer::onFixedUpdate`

```cpp
void onFixedUpdate(double fixedStepSeconds)
```

 Runs 0..n times per frame with a *constant* step, before `onUpdate`.

 Anything that must be reproducible belongs here rather than in
 `onUpdate`: a simulation fed a variable delta produces a different result
 on a faster machine, which is what makes physics bugs unreproducible.
 May not run at all in a given frame, and may run several times -- code
 here must not assume once-per-frame (T0100).

 @param fixedStepSeconds the constant step, `Clock::fixedStep()`.

## `ILayer::onUpdate`

```cpp
void onUpdate(double deltaSeconds)
```

 @param deltaSeconds scaled frame delta.

## `ILayer::onLateUpdate`

```cpp
void onLateUpdate(double deltaSeconds)
```

 Runs after every `onUpdate` and after transforms have propagated.

 This is where anything that *follows* something else belongs -- cameras,
 audio listeners, attachment points. Doing that work in `onUpdate` reads
 either this frame's or last frame's position depending on which layer
 happens to be registered first, which shows up as intermittent jitter
 that profiles as nothing (T0100).

 @param deltaSeconds scaled frame delta, the same value `onUpdate` saw.

## `ILayer::onRender`

```cpp
void onRender()
```

*No documentation comment.*

## `ILayer::onEvent`

```cpp
void onEvent(Event & event)
```

 @param event the event, which may be consumed to stop it descending.

## `ILayer::name`

```cpp
std::string_view name() const
```

*No documentation comment.*

## `LayerStack`

```cpp
class LayerStack
```

 Ordered layers, with overlays pinned above.

 Owns what it holds. A layer is `unique_ptr` rather than a raw pointer because
 the alternative -- the caller keeping ownership -- makes lifetime a question
 every caller has to answer correctly, and getting it wrong means a stack
 walking into freed memory during shutdown.

## `LayerStack::LayerStack`

```cpp
LayerStack()
```

*No documentation comment.*

## `LayerStack::LayerStack`

```cpp
LayerStack(const LayerStack &)
```

*No documentation comment.*

## `LayerStack::operator=`

```cpp
LayerStack & operator=(const LayerStack &)
```

*No documentation comment.*

## `LayerStack::push`

```cpp
ILayer * push(std::unique_ptr<ILayer> layer)
```

 Adds below any overlays.

 @param layer the layer; ownership is taken.
 @returns a borrowed pointer, valid until the layer is popped.

## `LayerStack::pushOverlay`

```cpp
ILayer * pushOverlay(std::unique_ptr<ILayer> overlay)
```

 Adds above everything, and above later non-overlay pushes.
 @param overlay the layer; ownership is taken.
 @returns a borrowed pointer, valid until it is popped.

## `LayerStack::pop`

```cpp
bool pop(ILayer * layer)
```

 @param layer the layer to remove; `onDetach` runs before it is destroyed.
 @returns true if it was in the stack.

## `LayerStack::clear`

```cpp
void clear()
```

 Detaches everything, top-down. Called by the destructor; safe twice.

## `LayerStack::fixedUpdate`

```cpp
void fixedUpdate(double fixedStepSeconds)
```

 Bottom-up, like `update`. Called once per fixed step, which may be zero
 or several times in one frame -- see `ILayer::onFixedUpdate`.

 @param fixedStepSeconds the constant step passed to every layer.

## `LayerStack::update`

```cpp
void update(double deltaSeconds)
```

 Bottom-up: the world simulates before the interface drawn over it.

 @param deltaSeconds scaled frame delta, passed to every layer.

## `LayerStack::lateUpdate`

```cpp
void lateUpdate(double deltaSeconds)
```

 Bottom-up, after every layer's `update` -- see `ILayer::onLateUpdate`.

 @param deltaSeconds scaled frame delta, passed to every layer.

## `LayerStack::render`

```cpp
void render()
```

*No documentation comment.*

## `LayerStack::dispatch`

```cpp
void dispatch(Event & event)
```

 Top-down, stopping at the first layer that consumes.
 @param event the event to deliver.

## `LayerStack::size`

```cpp
std::size_t size() const
```

*No documentation comment.*

## `LayerStack::overlayCount`

```cpp
std::size_t overlayCount() const
```

*No documentation comment.*

## `LayerStack::at`

```cpp
ILayer * at(std::size_t index) const
```

 @param index position from the bottom, overlays last.
 @returns the layer at `index`, or null if out of range.
