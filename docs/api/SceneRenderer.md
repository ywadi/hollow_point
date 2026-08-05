# `<hp/SceneRenderer.hpp>`

*Generated from `engine/include/hp/SceneRenderer.hpp` — do not edit.*

```cpp
#include <hp/SceneRenderer.hpp>
```

11 public declaration(s), 11 documented.

## `DrawSubmitStats`

```cpp
struct DrawSubmitStats
```

 What one submit pass did, for logging and for tests.

## `SceneRenderer`

```cpp
class SceneRenderer
```

 Draws a `DrawList` through a resolved camera.

 Owns the GPU-side renderer, its pipeline states and its per-model resource
 bindings, so it is created once against a device and reused. Not tied to a
 scene, a camera or a target: everything that varies per frame is a parameter,
 which is what keeps this callable for portals, reflection probes and
 thumbnails rather than only for "the" viewport (T0120.2).

## `SceneRenderer::SceneRenderer`

```cpp
SceneRenderer()
```

 Constructs an empty renderer that holds no device resources.

## `SceneRenderer::SceneRenderer`

```cpp
SceneRenderer(const SceneRenderer &)
```

 Not copyable: it owns GPU objects, and two owners would double-release.

## `SceneRenderer::operator=`

```cpp
SceneRenderer & operator=(const SceneRenderer &)
```

 Not copyable; see the copy constructor.
 @returns nothing -- deleted.

## `SceneRenderer::SceneRenderer`

```cpp
SceneRenderer(SceneRenderer && other)
```

 Moves the renderer.
 @param other the renderer to move from.

## `SceneRenderer::operator=`

```cpp
SceneRenderer & operator=(SceneRenderer && other)
```

 Moves the renderer, releasing whatever this one held.
 @param other the renderer to move from.
 @returns this renderer.

## `SceneRenderer::create`

```cpp
bool create(Diligent::IRenderDevice * device, Diligent::IDeviceContext * context, TargetFormat colour, std::optional<TargetFormat> depth)
```

 Creates the pipeline states and shaders.

 The target formats are baked into the pipeline state, so a renderer
 created for one pair cannot draw into another -- recreate it if the
 targets change format. Size may change freely; only format is baked.
 @param device the device to create against. Must not be null.
 @param context the immediate context, needed for one-off setup uploads.
        Must not be null.
 @param colour the colour target's format.
 @param depth the depth target's format, or **nothing for a pass that
        renders without depth at all** — a HUD or an overlay (T0027.4).
        This is not a convenience: a pipeline state declares whether it has
        a depth target, and drawing with no depth bound through a state
        that declares one is a render-pass incompatibility, not a
        harmless mismatch. Absent, depth test and depth write are both off
        and draw order within the pass is submission order.
 @returns whether creation succeeded. False leaves this renderer empty and
          logs why; it is never a partial state.

## `SceneRenderer::release`

```cpp
void release()
```

 Releases every device resource. Safe to call more than once.
 @returns nothing.

## `SceneRenderer::valid`

```cpp
bool valid() const
```

 @returns whether `create` succeeded and this renderer can draw.

## `SceneRenderer::render`

```cpp
std::size_t render(Diligent::IDeviceContext * context, const int & list, const ResolvedView & view, const AssetPool & pool, DrawSubmitStats * stats)
```

 Draws a list through a view.

 Targets must already be bound by the caller -- this issues draws and does
 not set render targets, because the layer that owns the target is the one
 that knows what else shares it (T0027).
 @param context the immediate context to record into. Must not be null.
 @param list what to draw, from `parseScene`.
 @param view the resolved camera, from `buildView`.
 @param pool where mesh GUIDs are resolved. Items whose mesh is absent are
        skipped and counted.
 @param stats optional; filled with what the pass did. Null to ignore.
 @returns how many items were drawn.
