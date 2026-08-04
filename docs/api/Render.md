# `<hp/Render.hpp>`

*Generated from `engine/include/hp/Render.hpp` — do not edit.*

```cpp
#include <hp/Render.hpp>
```

25 public declaration(s), 25 documented.

## `RenderBackend`

```cpp
enum class RenderBackend
```

| Enumerator | Value |
|---|---|
| `Default` | 0 |
| `Vulkan` | 1 |
| `OpenGL` | 2 |

 Which graphics backend to run on.

 No Direct3D, and that is deliberate rather than missing: zig targets Windows
 through the MinGW ABI, MinGW has no `atlbase.h`, and DiligentCore gates
 D3D11/D3D12 on ATL. Both targets get Vulkan and OpenGL.

## `ClipSpace`

```cpp
struct ClipSpace
```

 The device's clip-space convention (T0130.3).

 **This is a property of the device, not of the engine, and the two backends
 disagree by default.** Vulkan clips Z to [0, 1]; OpenGL clips it to [-1, 1]
 unless `glClipControl` says otherwise, and Diligent gates that on an opt-in
 flag that defaults to off. The engine turns it on and refuses a device that
 cannot honour it, so in practice `minZ` is always 0 here — but the value is
 still read from the device rather than assumed, because "assumed" is how a
 projection matrix ends up right on one backend and mirrored on the other.

## `ClipSpace::negativeOneToOneZ`

```cpp
bool negativeOneToOneZ() const
```

 @returns whether clip-space Z runs [-1, 1] rather than [0, 1]. This is
          the argument every Diligent projection helper takes, so it is
          spelled the way they spell it.

## `RenderConfig`

```cpp
struct RenderConfig
```

 How the render layer comes up.

## `RenderLayer`

```cpp
class RenderLayer
```

 Owns the graphics device, the immediate context and the swap chain.

 A layer, so it sits in the stack like everything else and gets `onDetach`
 before teardown — which is where every other layer must have released its
 GPU resources, because after this one detaches the device is gone (25.4).

## `RenderLayer::RenderLayer`

```cpp
RenderLayer(Window & window, RenderConfig config)
```

 Constructs the layer. Creates nothing until `onAttach`, so a layer can
 be built before there is a device to build.

 @param window the window to present into. Must outlive the layer, which
        `Application` guarantees: it owns both, and tears the stack down
        before the window. Handed in rather than fetched because `ILayer`
        has no back-pointer to the application — layers are decoupled on
        purpose (T0017), and a render layer is not the reason to change
        that.
 @param config backend, vsync, buffer count and validation.

## `RenderLayer::RenderLayer`

```cpp
RenderLayer(const RenderLayer &)
```

 Not copyable: a copy would hold the same device and release it twice.

## `RenderLayer::operator=`

```cpp
RenderLayer & operator=(const RenderLayer &)
```

 Not copyable; see the copy constructor.
 @returns nothing -- deleted.

## `RenderLayer::onAttach`

```cpp
void onAttach()
```

 Creates the device, context and swap chain against the application's
 window. Logs and leaves the layer inert on failure rather than throwing —
 an exception here would cross into the layer stack, and the conventions
 say engine code does not throw.

## `RenderLayer::onDetach`

```cpp
void onDetach()
```

 Releases the swap chain, context and device, in that order.

## `RenderLayer::onRender`

```cpp
void onRender()
```

 Clears the back buffer and presents (frame phases 10 and 11).

## `RenderLayer::onEvent`

```cpp
void onEvent(Event & event)
```

 Resizes the swap chain when the window resizes (25.3).

 Taken from the event rather than polled, so the swap chain follows the
 window without `Application` having to know a render layer exists.
 @param event the event to inspect; only window resizes are acted on, and
        none is consumed — other layers need to see a resize too.

## `RenderLayer::ready`

```cpp
bool ready() const
```

 @returns whether a device came up. Everything else is safe to call when
          this is false; it simply does nothing.

## `RenderLayer::backend`

```cpp
RenderBackend backend() const
```

 @returns the backend actually in use, which may differ from the one
          requested when `Default` was asked for or a fallback happened.

## `RenderLayer::adapterDescription`

```cpp
std::string adapterDescription() const
```

 @returns a human-readable adapter description, for logs and an about box.
          Empty when no device came up.

## `RenderLayer::resize`

```cpp
void resize(int width, int height)
```

 Resizes the swap chain. Call on a window resize; a no-op when the size
 is unchanged or the layer is inert.
 @param width new width in pixels.
 @param height new height in pixels.

## `RenderLayer::swapChainWidth`

```cpp
int swapChainWidth() const
```

 @returns the swap chain's current width in pixels, or 0 when inert.

 The *swap chain's* size, not the window's. They are supposed to track
 each other and the interesting failures are exactly when they do not —
 a window that resized while the chain did not renders undefined
 contents, which looks like a stale image rather than an error.

## `RenderLayer::swapChainHeight`

```cpp
int swapChainHeight() const
```

 @returns the swap chain's current height in pixels, or 0 when inert.

## `RenderLayer::vsync`

```cpp
bool vsync() const
```

 @returns whether presents currently wait for vertical blank.

## `RenderLayer::setVsync`

```cpp
void setVsync(bool enabled)
```

 Turns vsync on or off at run time (T0110.1).

 Costs a swap-chain recreate on the next present, because Diligent
 derives the present mode from this and rebuilds when it changes. The
 device survives.
 @param enabled whether to wait for vertical blank.

## `RenderLayer::setClearColour`

```cpp
void setClearColour(float r, float g, float b, float a)
```

 Colour the back buffer is cleared to, as linear RGBA in [0, 1].

 A temporary affordance: until there is anything to draw, the clear
 colour is the only evidence the device is alive, and a test needs to be
 able to change it. T0027's render stack owns clearing properly.
 @param r red.
 @param g green.
 @param b blue.
 @param a alpha.

## `RenderLayer::device`

```cpp
Diligent::IRenderDevice * device() const
```

 @returns the graphics device, or nullptr when the layer is inert.

 Handed out so passes and gameplay-authored layers can create resources
 and issue draws (D22). The engine owns it and destroys it in `onDetach`;
 **anything holding this pointer must release its GPU resources by then**,
 which is the whole reason `RenderLayer` is a layer and gets `onDetach`
 before teardown (25.4).

## `RenderLayer::context`

```cpp
Diligent::IDeviceContext * context() const
```

 @returns the immediate context, or nullptr when the layer is inert.

 **Valid for the frame, not beyond**, and not thread-safe: it is the
 immediate context, so it belongs to whichever thread runs the frame.

## `RenderLayer::swapChain`

```cpp
Diligent::ISwapChain * swapChain() const
```

 @returns the swap chain, or nullptr when the layer is inert. Its back
          buffer is where a composited frame ultimately lands.

## `RenderLayer::clipSpace`

```cpp
ClipSpace clipSpace() const
```

 @returns the device's clip-space convention, or the defaults when the
          layer is inert.

 **Read this rather than assuming it.** It is the difference between a
 projection matrix that is right on Vulkan and silently wrong on OpenGL;
 see `ClipSpace` and T0130.3.
