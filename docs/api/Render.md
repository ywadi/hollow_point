# `<hp/Render.hpp>`

*Generated from `engine/include/hp/Render.hpp` — do not edit.*

```cpp
#include <hp/Render.hpp>
```

16 public declaration(s), 16 documented.

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
