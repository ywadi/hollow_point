# `<hp/RenderStack.hpp>`

*Generated from `engine/include/hp/RenderStack.hpp` — do not edit.*

```cpp
#include <hp/RenderStack.hpp>
```

21 public declaration(s), 21 documented.

## `RenderPassContext`

```cpp
struct RenderPassContext
```

 Everything a layer needs to draw, handed to it for the duration of one call.

 **Every pointer here is valid for that call and not beyond.** Storing one and
 using it next frame is a use-after-free the moment the window resizes, since
 a resize recreates the targets. The device outlives the frame, but even that
 is destroyed when `RenderLayer` detaches, so a layer holding GPU resources
 must release them before then.

## `LayerClear`

```cpp
enum class LayerClear
```

| Enumerator | Value |
|---|---|
| `None` | 0 |
| `Colour` | 1 |
| `Depth` | 2 |
| `ColourAndDepth` | 3 |

 What a layer clears before it draws.

## `IRenderLayer`

```cpp
class IRenderLayer
```

 A visual layer. Implement this in the engine, in the editor, or in a gameplay
 module (T0094) — the stack does not care which.

 The configuration is public data rather than virtual accessors because it is
 data: a caller flips `enabled` at run time, and routing that through a virtual
 would buy nothing and cost every implementer four overrides.

## `IRenderLayer::IRenderLayer`

```cpp
IRenderLayer()
```

 Constructs a layer with default configuration.

## `IRenderLayer::IRenderLayer`

```cpp
IRenderLayer(const IRenderLayer &)
```

 Not copyable: a layer is an identity in a stack, not a value.

## `IRenderLayer::operator=`

```cpp
IRenderLayer & operator=(const IRenderLayer &)
```

 Not copyable; see the copy constructor.
 @returns nothing -- deleted.

## `IRenderLayer::onRenderLayer`

```cpp
void onRenderLayer(const RenderPassContext & pass)
```

 Draws this layer. Targets and clears are already applied.
 @param pass the device, context and bound targets for this call. Nothing
        in it may be stored beyond the call.
 @returns nothing.

## `IRenderLayer::name`

```cpp
const char * name() const
```

 @returns a stable name, used for the profiling zone (27.6) and for logs.
          Must remain valid for the layer's lifetime; a string literal is
          the expected answer.

## `RenderStack`

```cpp
class RenderStack
```

 An ordered set of layers, composited into one target per frame.

 **Does not own its layers.** A gameplay module's layer is owned by that
 module, and a module can be unloaded — so ownership here would mean the
 engine holding a pointer into a library that no longer exists. The rule is
 the other way round and must be stated where implementers will read it:
 **whoever adds a layer removes it before destroying it**, and a gameplay
 module removes its layers on unload.

## `RenderStack::RenderStack`

```cpp
RenderStack()
```

 Constructs an empty stack.

## `RenderStack::RenderStack`

```cpp
RenderStack(const RenderStack &)
```

 Not copyable: two stacks holding the same layers would render them twice.

## `RenderStack::operator=`

```cpp
RenderStack & operator=(const RenderStack &)
```

 Not copyable; see the copy constructor.
 @returns nothing -- deleted.

## `RenderStack::RenderStack`

```cpp
RenderStack(RenderStack && other)
```

 Moves the stack.
 @param other the stack to move from.

## `RenderStack::operator=`

```cpp
RenderStack & operator=(RenderStack && other)
```

 Moves the stack.
 @param other the stack to move from.
 @returns this stack.

## `RenderStack::add`

```cpp
void add(IRenderLayer * layer)
```

 Inserts a layer, keeping the stack sorted by `order`.

 Adding the same layer twice is refused and logged rather than producing a
 layer that draws twice — which looks like a blending bug, not a
 double-add.
 @param layer the layer to insert. Not owned; must outlive its removal.
 @returns nothing.

## `RenderStack::remove`

```cpp
bool remove(IRenderLayer * layer)
```

 Removes a layer. Safe to call for a layer that is not present.
 @param layer the layer to remove.
 @returns whether it was there.

## `RenderStack::clear`

```cpp
void clear()
```

 Removes every layer without destroying any.
 @returns nothing.

## `RenderStack::reorder`

```cpp
void reorder()
```

 Re-sorts after a layer's `order` changed.

 Explicit rather than sorting every frame: `order` is public data, so the
 stack cannot observe a change, and sorting unconditionally would cost a
 pass over the list every frame to catch something that happens rarely.
 @returns nothing.

## `RenderStack::layers`

```cpp
const std::vector<IRenderLayer *> & layers() const
```

 @returns the layers, in render order.

## `RenderStack::size`

```cpp
std::size_t size() const
```

 @returns how many layers are in the stack, enabled or not.

## `RenderStack::render`

```cpp
std::size_t render(Diligent::IRenderDevice * device, Diligent::IDeviceContext * context, Diligent::ITextureView * colour, Diligent::ITextureView * depth, FrameTargets * targets, int width, int height, ClipSpace clip)
```

 Renders every enabled layer, in order, into `colour`.

 Binds targets and applies each layer's clear policy before calling it, so
 a layer that draws nothing still gets its clear — which is what makes a
 disabled world layer show background rather than last frame's image.
 @param device the device to hand layers.
 @param context the immediate context to hand layers.
 @param colour the composite colour target. Nothing renders if null.
 @param depth the depth target, or nullptr if none exists. Layers with
        `useDepth` set see it; others do not.
 @param targets the shared frame targets, or nullptr.
 @param width target width in pixels.
 @param height target height in pixels.
 @param clip the device's texture-space convention, from
        `RenderLayer::clipSpace()`. Required rather than defaulted: a
        default-constructed `ClipSpace` looks plausible and flips
        nothing, which silently inverts a layer's render-to-texture
        sampling.
 @returns how many layers actually rendered, which is what a test asserts
          on to prove `enabled` is honoured.
