# `<hp/SceneRenderer.hpp>`

*Generated from `engine/include/hp/SceneRenderer.hpp` — do not edit.*

```cpp
#include <hp/SceneRenderer.hpp>
```

14 public declaration(s), 14 documented.

## `SceneScreenInputs`

```cpp
struct SceneScreenInputs
```

 The targets a render draws into and the snapshots it copies them to, so a
 material shader can sample what is behind it (T0147).

 **All four or none.** The renderer needs the first two in order to put the
 targets back after the copy, and the second two are what it copies into and
 what the shader samples; leaving any of them null disables the intermediate
 it belongs to. A default-constructed value is the pre-T0147 behaviour
 exactly: no copy is issued, `g_SceneColour` and `g_SceneDepth` bind the
 engine's white and black stand-ins, and a material that reads them is
 warned about once.

 **Every pointer is valid for the call and not beyond**, the same rule
 `FrameTargets` states: a resize recreates the textures behind these views.

 This is the *engine-fed* half of the game resource model — engine names in
 the engine's base signature — and D35's other half, a game's own
 declarations, needs nothing here.

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

## `SceneRenderer::setGameTexture`

```cpp
void setGameTexture(std::string_view name, Diligent::ITextureView * view)
```

 Feeds a texture a game layer produced to material shaders, by name
 (T0147.4, T0094.5).

 **The second source for a declared texture.** A module declares
 `Texture2DArray visibility;` exactly as it declares any other texture
 (T0161, D35); a `.hpmat` binds one to an *asset*, and this binds one to
 whatever a game layer just rendered — a fog-of-war field (T0093), a flow
 simulation, a minimap. One declaration mechanism, two feeds, and the
 `.hpmat` wins when both name the same slot.

 **Not refcounted, deliberately**, matching `FrameTargets`' rule: this
 object does not keep the texture alive, and a resize invalidates the
 view. Feed it again after a resize; feeding null removes the entry.

 @param name the shader-side declaration name. Empty is ignored.
 @param view the view to bind, or null to remove the entry.
 @returns nothing.

## `SceneRenderer::clearGameTextures`

```cpp
void clearGameTextures()
```

 Removes every game-fed texture. What a layer calls on detach.
 @returns nothing.

## `SceneRenderer::render`

```cpp
std::size_t render(Diligent::IDeviceContext * context, const int & list, const ResolvedView & view, const AssetPool & pool, const int & lights, DrawSubmitStats * stats, double timeSeconds, const SceneScreenInputs & screen)
```

 Draws a list through a view.

 Targets must already be bound by the caller -- this issues draws and does
 not set render targets, because the layer that owns the target is the one
 that knows what else shares it (T0027). The **one** exception is the
 screen snapshot (T0147): when @p screen names them, the renderer copies
 the frame between its opaque and blend passes and re-binds exactly the
 two views it was handed. It still never chooses a target.

 **Two passes since T0147, and that changed draw order.** Every `Opaque`
 and `Mask` primitive is submitted before every `Blend` one, rather than
 in draw-list order — which is a correctness fix in its own right (a
 transparent surface drawn before the opaque geometry behind it blended
 against the clear colour) and is what gives the snapshot a moment to
 happen in. Ordering *within* the blend pass is still submission order;
 the back-to-front sort is T0045's.
 @param context the immediate context to record into. Must not be null.
 @param list what to draw, from `parseScene`.
 @param view the resolved camera, from `buildView`.
 @param pool where mesh GUIDs are resolved. Items whose mesh is absent are
        skipped and counted.
 @param stats optional; filled with what the pass did. Null to ignore.
 @returns how many items were drawn.
 @param lights what illuminates the scene, from `gatherLights`. **An
        empty list renders black**, which is not an error and is exactly
        what every frame did before T0079.
 @param timeSeconds the frame's time, in seconds — `Clock::elapsed()`
        from whichever clock owns this view's timeline (T0159.5). Reaches
        shaders as `g_Frame.Renderer.Time` and the contract's
        `HpSurfaceInput::Time`. Defaulted to zero so a caller that has no
        clock (a thumbnail, a one-shot test render) gets a defined value,
        not a stale or undefined one.
 @param screen the targets being drawn into and the snapshots a material
        shader samples (T0147). Defaulted to nothing, which is what a
        caller with no need for scene colour or depth passes — no copy is
        issued and the intermediates read the engine's stand-ins.
