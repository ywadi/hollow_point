# `<hp/DrawSubmission.hpp>`

*Generated from `engine/include/hp/DrawSubmission.hpp` — do not edit.*

```cpp
#include <hp/DrawSubmission.hpp>
```

3 public declaration(s), 3 documented.

## `DrawItem`

```cpp
struct DrawItem
```

 One thing to draw: where it is, and which assets it needs.

 A value, valid for the frame that produced it. It holds **GUIDs, not asset
 pointers**, because parsing must not require a loaded pool -- an entity can
 legitimately reference an asset that has not been loaded yet, and deciding
 what to do about that is the resolve step's job, not the parse step's.

## `DrawParseStats`

```cpp
struct DrawParseStats
```

 What a parse pass saw, for logging and for tests.

 Separate from the list because "how many were skipped and why" is the part
 worth reporting when a scene renders nothing, and stuffing it into the list
 would make every consumer step over it.

## `parseScene`

```cpp
int parseScene(const Scene & scene, LayerMask cullingMask, DrawParseStats * stats)
```

 Collects everything drawable in a scene.

 Filters to entities carrying **both** `WorldTransform` and `MeshRenderer`,
 dropping those whose mesh GUID is unset. `Transform` alone is not enough:
 the world matrix is what a draw needs, and it exists only after propagation
 (T0101), so parsing off `Transform` would silently draw everything at its
 local position the first frame a hierarchy is built.

 **Object layers are filtered here** (T0085), before anything else is checked,
 because this is the cheapest place they can be: one AND against a mask the
 caller already has, and an excluded object costs nothing further. Filtering
 in a shader instead would pay the entire cost of drawing an object in order
 to discard it.

 @param scene the scene to walk. Not modified.
 @param cullingMask which object layers to keep, normally a camera's
        `cullingMask`. An entity is kept when this intersects its
        `MeshRenderer::layers`. Defaults to every layer, so a caller with no
        opinion sees everything.
 @param stats optional; filled with what the pass saw. Null to ignore.
 @returns the draw list, empty when nothing is drawable.
