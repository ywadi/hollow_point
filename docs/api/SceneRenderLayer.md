# `<hp/SceneRenderLayer.hpp>`

*Generated from `engine/include/hp/SceneRenderLayer.hpp` — do not edit.*

```cpp
#include <hp/SceneRenderLayer.hpp>
```

11 public declaration(s), 11 documented.

## `SceneRenderLayer`

```cpp
class SceneRenderLayer
```

 Renders a scene into the stack's shared target, through the camera on its
 view slot.

 **Binds no targets and clears nothing itself.** `RenderStack` has already
 bound and cleared per this layer's `clear` and `useDepth` policy by the time
 `onRenderLayer` runs, and a layer that re-bound them would silently undo the
 stack's compositing — which presents as later layers overwriting earlier ones
 rather than as a binding mistake.

## `SceneRenderLayer::SceneRenderLayer`

```cpp
SceneRenderLayer(const char * name)
```

 Constructs a layer that renders nothing until it is given a scene.
 @param name a stable name for the profiling zone and logs. Must outlive
        the layer; a string literal is the expected answer.

## `SceneRenderLayer::create`

```cpp
bool create(Diligent::IRenderDevice * device, Diligent::IDeviceContext * context, TargetFormat colour, TargetFormat depth)
```

 Creates the GPU-side renderer.

 Separate from the constructor because a layer is normally built before
 the device is, and because the target formats must match the stack's --
 they are baked into the pipeline state.

 **`useDepth` must already be set, and this is the ordering that matters.**
 The pipeline state records whether the pass has a depth attachment, so a
 HUD layer must be configured before it is created — `configureAsHud`
 then `create`, not the other way round. Getting it wrong is caught at the
 first frame with a named error rather than a validation failure; see
 `onRenderLayer`.
 @param device the device to create against. Must not be null.
 @param context the immediate context, for one-off setup uploads. Must not
        be null.
 @param colour the format of the colour target the stack composites into.
 @param depth the format of the stack's depth target. Ignored when
        `useDepth` is false, which builds a pipeline state with no depth
        attachment at all.
 @returns whether creation succeeded. False leaves the layer inert rather
          than half-created, and it logs why.

## `SceneRenderLayer::release`

```cpp
void release()
```

 Releases every device resource. Safe to call more than once.
 @returns nothing.

## `SceneRenderLayer::valid`

```cpp
bool valid() const
```

 @returns whether `create` succeeded and this layer can draw.

## `SceneRenderLayer::setScene`

```cpp
void setScene(Scene * scene, const AssetPool * pool)
```

 Points the layer at what it draws.

 Both are borrowed and neither is owned, so **whoever owns them must
 outlive the layer or clear this first**. Passing nulls is how a layer is
 parked without removing it from the stack.
 @param scene the scene to draw, or nullptr.
 @param pool where mesh GUIDs resolve, or nullptr.
 @returns nothing.

## `SceneRenderLayer::scene`

```cpp
Scene * scene() const
```

 @returns the scene this layer draws, or nullptr.

## `SceneRenderLayer::setGameTexture`

```cpp
void setGameTexture(std::string_view name, Diligent::ITextureView * view)
```

 Feeds a texture this layer produced to material shaders, by name
 (T0147.4, T0094.5).

 **This is where the game-fed half is meant to be called from.** A
 gameplay-authored layer (T0094) renders its fog-of-war field, its flow
 map or its minimap into a target it owns, then hands the view here; a
 material module that declared a texture of that name samples it. The
 engine never learns what the bytes mean, which is the point.

 **Feed it again after a resize** — the view is not kept alive by this
 call, matching `FrameTargets`' rule.
 @param name the shader-side declaration name.
 @param view the view to bind, or null to remove the entry.
 @returns nothing.

## `SceneRenderLayer::onRenderLayer`

```cpp
void onRenderLayer(const RenderPassContext & pass)
```

 Draws the scene. See the class comment for what it deliberately does not
 do.
 @param pass the bound targets and context, from `RenderStack`.
 @returns nothing.

## `SceneRenderLayer::name`

```cpp
const char * name() const
```

 @returns the name given at construction.

## `configureAsHud`

```cpp
void configureAsHud(SceneRenderLayer & layer, std::uint8_t slot, int order)
```

 Configures a layer as a HUD drawn over the world (27.4).

 Sets the three things that make 2D-over-3D work, all of which are wrong by
 default for this purpose:

   * **`useDepth = false`** — the trap the per-layer depth policy exists for.
     A HUD that depth-tests against the world vanishes behind whatever
     geometry is near the camera, and that reads as flickering UI rather than
     as a depth bug.
   * **`clear = LayerClear::None`** — a HUD composites over what is beneath
     it. Clearing colour would erase the world it is drawn on.
   * **`order` above the world layer**, and `viewSlot` off 0, so it resolves
     its own camera.

 The camera itself is content, not engine: put an orthographic `Camera` on an
 entity with this slot. Nothing here creates one.
 @param layer the layer to configure.
 @param slot the view slot its camera lives on. Must not be the world's.
 @param order the sort key. Above the world layer's, and below whatever
        tonemapping will eventually sit at (T0096).
 @returns nothing.
