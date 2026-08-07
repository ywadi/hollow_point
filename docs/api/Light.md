# `<hp/Light.hpp>`

*Generated from `engine/include/hp/Light.hpp` — do not edit.*

```cpp
#include <hp/Light.hpp>
```

8 public declaration(s), 8 documented.

## `LightType`

```cpp
enum class LightType
```

| Enumerator | Value |
|---|---|
| `Directional` | 0 |
| `Point` | 1 |
| `Spot` | 2 |

 What shape a light throws.

## `kMaxLights`

```cpp
inline constexpr std :: size_t kMaxLights = 16
```

 How many lights a frame can carry.

 Sixteen, which is `PBR_Renderer`'s own default. **It is not free**: the frame
 attributes buffer is sized `CameraAttribs * 2 + renderer params +
 PBRLightAttribs * count`, so raising it grows a buffer written every frame.
 Lowering it is the cheaper mistake.

## `Light`

```cpp
struct Light
```

 A light source. Position and orientation come from the entity (T0101).

## `ResolvedPlacement`

```cpp
struct ResolvedPlacement
```

 Where something in the hierarchy is, and which way it faces.

 **Split out of `ResolvedLight` so T0093 does not fork it.** Vision cones and
 radial reveals are cone- and point-shaped projectors that reuse light-shaped
 machinery but are emphatically **not** lights — a vision source must not
 illuminate — and T0079's cross-ticket obligation says in as many words that
 gathering must stay reusable for a component that does not shade, "or T0093
 ends up forking it".

 This is the part that would have been forked: deriving a world position and a
 facing direction from a transform, including the glTF negative-Z convention
 and the degenerate-matrix guard. It is three lines that are wrong in two
 interesting ways, which is exactly the kind of thing that must exist once.

## `resolvePlacement`

```cpp
ResolvedPlacement resolvePlacement(const float4x4 & world)
```

 Derives a placement from a world matrix (T0101).

 @param world the entity's world transform.
 @returns the position and facing. A degenerate (zero-scaled) matrix yields a
          default facing rather than a NaN — a NaN here would propagate into
          the shading of *every* object rather than of the one entity that is
          broken.

## `ResolvedLight`

```cpp
struct ResolvedLight
```

 A light resolved for one frame: the lamp, and where it is.

 A value, like `ResolvedView`, and meaningless after the frame that produced
 it. Recompute rather than storing.

## `gatherLights`

```cpp
int gatherLights(const Scene & scene, std::size_t maxLights)
```

 Collects the lights in a scene.

 Filters to entities carrying both a `WorldTransform` and an **enabled**
 `Light`, and takes position and direction from the world matrix.

 **Frustum culling and per-object selection are not here** (79.2/79.3). This
 returns what exists, capped, and the cap is applied in registry order —
 which is *not* a stable guarantee, so a scene with more than `maxLights`
 lights gets an arbitrary subset and says so in the log. Choosing *which*
 lights matter per object is the design decision the ticket calls out, and it
 is deliberately not made by accident here.

 @param scene the scene to walk. Not modified.
 @param maxLights the cap, normally `kMaxLights`.
 @returns the lights, empty when the scene has none — which is not an error
          and renders an unlit (black) frame.

## `selectLightsFor`

```cpp
void selectLightsFor(const int & lights, const float3 & objectPosition, LayerMask objectLayers, std::size_t maxLights, int & out)
```

 Picks the lights that matter to one object (T0079.3, T0085.4).

 **This is the design decision the ticket names**, and the answer here is
 deliberately the simple one: filter by layer, then take the nearest N by
 distance from the light to the object, with directional lights always kept
 because they have no position to be far from.

 Nearest-N is chosen over tiled or clustered forward because it is the option
 that can be *measured* against a real scene before a harder one is justified
 — clustered forward is substantially more work and changes the shape of the
 frame. **Its known weakness is popping**: when an object moves, the set can
 change abruptly and the lighting jumps. Sorting by distance alone is exactly
 what causes that, so this is the line to revisit first when it shows up,
 rather than the conclusion that nearest-N was wrong.

 ## The order is a contract, and `out[0]` is the dominant light (T0145)

 **Nearest, then brightest, then fixed** — a total order:

 1. ascending distance from the object, with every directional light at 0, so
    the suns precede every lamp that is not exactly on top of the object;
 2. descending Rec. 709 luminance of `colour * intensity`, so the brightest
    of several equally-near lights wins;
 3. ascending entity, so two identical lamps still sort the same way on every
    run and a frame is reproducible.

 **Step 2 exists because its absence was a bug, found the expensive way.**
 Until T0145 the comparison was distance alone and `std::sort` is *unstable*,
 so with two directional lights — both at distance 0 — which one landed at
 index 0 was unspecified. On the machine it was measured on it was the dim
 fill, and the rock cube sample's parallax shadow march spent its one
 expensive ray on the wrong light while reading as correct in its own source
 (T0158/T0159). The sample ranked by intensity itself to work around it; that
 workaround is now the engine's rule.

 A shader sees this order as `HpLight::Index`, and `index == 0` is documented
 there as the dominant light. A technique that can afford one expensive light
 — a shadow march, a shaped specular, a ramp with a second tint — spends it
 on that one.

 @param lights every light in the frame, from `gatherLights`.
 @param objectPosition the object's world position.
 @param objectLayers the object's `MeshRenderer::layers`.
 @param maxLights how many to keep. Clamped to `kMaxLights`.
 @param out filled with the chosen lights, dominant first per the order
        above. Cleared first, and reused across draws so selection does not
        allocate per object.
 @returns nothing.
