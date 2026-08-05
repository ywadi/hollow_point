# `<hp/FrameTargets.hpp>`

*Generated from `engine/include/hp/FrameTargets.hpp` — do not edit.*

```cpp
#include <hp/FrameTargets.hpp>
```

24 public declaration(s), 24 documented.

## `TargetFormat`

```cpp
enum class TargetFormat
```

| Enumerator | Value |
|---|---|
| `Colour` | 0 |
| `ColourHDR` | 1 |
| `Depth` | 2 |

 What a target is for, which is what decides its pixel format.

 A role rather than a format, so formats are chosen in **one** place
 (`FrameTargets.cpp`) rather than being spelled out at each declaration and
 drifting apart. That is 46.4's requirement stated as a type.

## `FrameTargetDesc`

```cpp
struct FrameTargetDesc
```

 One declared target.

## `FrameTargets`

```cpp
class FrameTargets
```

 Owns a named set of render targets, created once and resized with the
 viewport rather than rebuilt per frame.

## `FrameTargets::FrameTargets`

```cpp
FrameTargets()
```

 Constructs an empty set. Declares nothing and allocates no GPU memory.

## `FrameTargets::FrameTargets`

```cpp
FrameTargets(const FrameTargets &)
```

 Not copyable: a copy would own the same textures and release them twice.

## `FrameTargets::operator=`

```cpp
FrameTargets & operator=(const FrameTargets &)
```

 Not copyable; see the copy constructor.
 @returns nothing -- deleted.

## `FrameTargets::FrameTargets`

```cpp
FrameTargets(FrameTargets && other)
```

 Moves the set, leaving the source empty.
 @param other the set to move from.

## `FrameTargets::operator=`

```cpp
FrameTargets & operator=(FrameTargets && other)
```

 Moves the set, releasing whatever this one held.
 @param other the set to move from.
 @returns this set.

## `FrameTargets::declare`

```cpp
void declare(FrameTargetDesc desc)
```

 Declares a target. Call before `create`; declaring after it is a no-op
 and says so, because silently ignoring it would produce a null lookup
 far from the mistake.
 @param desc the target to declare.
 @returns nothing.

## `FrameTargets::create`

```cpp
bool create(Diligent::IRenderDevice * device, int width, int height)
```

 Creates every declared target at the given size.
 @param device the device to create on. Must outlive this object.
 @param width viewport width in pixels.
 @param height viewport height in pixels.
 @returns false if any target failed, having released the rest — a
          half-created set is the state that produces a null lookup three
          passes later with nothing pointing at the cause.

## `FrameTargets::resize`

```cpp
bool resize(int width, int height)
```

 Recreates every target at a new size.

 A no-op when the size is unchanged, which is what makes it safe to call
 every frame — 46.3 asks for debouncing, and this is where it lives rather
 than in every caller.
 @param width new viewport width in pixels.
 @param height new viewport height in pixels.
 @returns false if recreation failed; the set is then empty.

## `FrameTargets::release`

```cpp
void release()
```

 Releases every target. Safe to call more than once.
 @returns nothing.

## `FrameTargets::renderTarget`

```cpp
Diligent::ITextureView * renderTarget(std::string_view name) const
```

 Looks up a colour target's render-target view.
 @param name the declared name.
 @returns the view, or nullptr when the name is unknown, the set is not
          created, or the target is a depth target. **Valid for this frame
          only** — a resize invalidates it, so do not cache it across
          frames.

## `FrameTargets::depthStencil`

```cpp
Diligent::ITextureView * depthStencil(std::string_view name) const
```

 Looks up a depth target's depth-stencil view.
 @param name the declared name.
 @returns the view, or nullptr when the name is unknown or the target is
          not a depth target. Same single-frame lifetime as
          `renderTarget`.

## `FrameTargets::shaderResource`

```cpp
Diligent::ITextureView * shaderResource(std::string_view name) const
```

 Looks up any target's shader-resource view, for a pass that reads it.

 Depth targets have one too, which is what lets the transparent pass fade
 soft particles against scene depth (T0106.5) and what a later distortion
 pass will need from scene colour.
 @param name the declared name.
 @returns the view, or nullptr when the name is unknown. Same single-frame
          lifetime as `renderTarget`.

## `FrameTargets::declarePingPong`

```cpp
void declarePingPong(FrameTargetDesc desc)
```

 Declares a pair of identical targets for a multi-pass effect.

 Creates `"<name>.a"` and `"<name>.b"`, both with `desc`'s format and
 scale. They are ordinary targets: `renderTarget` and `shaderResource`
 work on the suffixed names, so a debug view can show either.
 @param desc the pair's shared description. Its `name` is the pair's name.
 @returns nothing.

## `FrameTargets::pingPongTarget`

```cpp
Diligent::ITextureView * pingPongTarget(std::string_view name, int pass) const
```

 The target a given pass writes into.
 @param name the pair's name, as declared.
 @param pass the zero-based pass index. Even passes write `.b`, odd write
        `.a`, so pass 0 reads the `.a` an earlier stage filled.
 @returns the render-target view, or nullptr when the pair is unknown or
          not created.

## `FrameTargets::pingPongSource`

```cpp
Diligent::ITextureView * pingPongSource(std::string_view name, int pass) const
```

 The target a given pass reads from — always the other one.
 @param name the pair's name, as declared.
 @param pass the zero-based pass index, the same value passed to
        `pingPongTarget`.
 @returns the shader-resource view, or nullptr when the pair is unknown or
          not created. **Never the same texture `pingPongTarget` returns
          for the same pass**, which is the whole point.

## `FrameTargets::hasPingPong`

```cpp
bool hasPingPong(std::string_view name) const
```

 @param name the pair's name.
 @returns whether a ping-pong pair was declared under that name.

## `FrameTargets::ready`

```cpp
bool ready() const
```

 @returns whether every declared target currently exists.

## `FrameTargets::width`

```cpp
int width() const
```

 @returns the current viewport width in pixels, or 0 before creation.

## `FrameTargets::height`

```cpp
int height() const
```

 @returns the current viewport height in pixels, or 0 before creation.

## `FrameTargets::memoryBytes`

```cpp
std::uint64_t memoryBytes() const
```

 @returns GPU memory currently held by these targets, in bytes (46.6).

 Computed from the declared formats and sizes rather than queried from the
 driver, so it is what we asked for rather than what was allocated —
 alignment and tiling mean the real figure is somewhat larger. Good enough
 to answer "what is eating VRAM", which is the question it exists for.

## `FrameTargets::declared`

```cpp
const std::vector<FrameTargetDesc> & declared() const
```

 @returns the declared targets, in declaration order, for a profiler or
          debug panel that wants to list them.
