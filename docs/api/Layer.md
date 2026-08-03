# `<hp/Layer.hpp>`

*Generated from `engine/include/hp/Layer.hpp` — do not edit.*

```cpp
#include <hp/Layer.hpp>
```

24 public declaration(s), 12 documented.

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

## `ILayer::onUpdate`

```cpp
void onUpdate(double deltaSeconds)
```

 @param deltaSeconds scaled frame delta.

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

## `LayerStack::update`

```cpp
void update(double deltaSeconds)
```

 Bottom-up: the world simulates before the interface drawn over it.

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
