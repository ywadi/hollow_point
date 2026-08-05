# `<hp/SceneView.hpp>`

*Generated from `engine/include/hp/SceneView.hpp` — do not edit.*

```cpp
#include <hp/SceneView.hpp>
```

16 public declaration(s), 16 documented.

## `SceneViewStats`

```cpp
struct SceneViewStats
```

 What one frame of scene rendering did.

## `SceneView`

```cpp
class SceneView
```

 Renders a scene into its own colour and depth targets.

 One of these per view. A second view — a portal, a security monitor, a
 thumbnail — is a second instance with its own targets and its own view slot,
 which is what T0120.2 asks for and why nothing here is a singleton.

## `SceneView::SceneView`

```cpp
SceneView()
```

 Constructs an empty view holding no GPU resources.

## `SceneView::SceneView`

```cpp
SceneView(const SceneView &)
```

 Not copyable: it owns GPU objects.

## `SceneView::operator=`

```cpp
SceneView & operator=(const SceneView &)
```

 Not copyable; see the copy constructor.
 @returns nothing -- deleted.

## `SceneView::SceneView`

```cpp
SceneView(SceneView && other)
```

 Moves the view.
 @param other the view to move from.

## `SceneView::operator=`

```cpp
SceneView & operator=(SceneView && other)
```

 Moves the view, releasing whatever this one held.
 @param other the view to move from.
 @returns this view.

## `SceneView::create`

```cpp
bool create(Diligent::IRenderDevice * device, Diligent::IDeviceContext * context, int width, int height, TargetFormat colour)
```

 Creates the targets and the renderer.
 @param device the device to create on. Must not be null.
 @param context the immediate context, for one-off setup. Must not be null.
 @param width initial width in pixels. Clamped to at least 1.
 @param height initial height in pixels. Clamped to at least 1.
 @param colour the colour target's format. `ColourHDR` is what T0096 will
        want; `Colour` is what composites directly to a swap chain today.
 @returns whether everything came up. False leaves this empty, never
          half-created.

## `SceneView::resize`

```cpp
bool resize(int width, int height)
```

 Resizes the targets. A no-op when the size is unchanged, so it is safe to
 call every frame.
 @param width new width in pixels.
 @param height new height in pixels.
 @returns whether the targets are usable afterwards.

## `SceneView::release`

```cpp
void release()
```

 Releases everything. Safe to call more than once.
 @returns nothing.

## `SceneView::valid`

```cpp
bool valid() const
```

 @returns whether `create` succeeded and this view can render.

## `SceneView::render`

```cpp
Diligent::ITextureView * render(Diligent::IDeviceContext * context, Scene & scene, const AssetPool & pool, ClipSpace clip, std::uint8_t viewSlot, SceneViewStats * stats)
```

 Renders one frame of the scene.

 Binds the targets, clears them — colour to `clearColour`, depth to
 `kDepthClearValue`, which is 0 under reverse-Z — resolves the camera for
 `viewSlot`, and submits every drawable entity.
 @param context the immediate context. Must not be null.
 @param scene the scene to draw.
 @param pool where mesh GUIDs resolve.
 @param clip the device's clip-space convention, from
        `RenderLayer::clipSpace()`.
 @param viewSlot which camera slot to resolve. Slot 0 is the world.
 @param stats optional; filled with what the frame did.
 @returns the colour target to display, or **nullptr when nothing was
          published** — no camera, or the view could not be built. A null
          return is the signal not to emit a `FrameRenderedEvent`.

## `SceneView::colour`

```cpp
Diligent::ITextureView * colour() const
```

 @returns the colour target's shader-resource view, or nullptr. **Valid
          for the current frame only** — a resize recreates it.

## `SceneView::width`

```cpp
int width() const
```

 @returns the current target width in pixels, or 0.

## `SceneView::height`

```cpp
int height() const
```

 @returns the current target height in pixels, or 0.

## `SceneView::setClearColour`

```cpp
void setClearColour(float r, float g, float b, float a)
```

 Colour the target is cleared to, as linear RGBA.

 Not black by default: a black frame and a broken frame look identical,
 and "did anything run?" is the first question asked of a viewport that
 shows nothing.
 @param r red.
 @param g green.
 @param b blue.
 @param a alpha.
 @returns nothing.
