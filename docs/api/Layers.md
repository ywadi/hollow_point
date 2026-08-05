# `<hp/Layers.hpp>`

*Generated from `engine/include/hp/Layers.hpp` — do not edit.*

```cpp
#include <hp/Layers.hpp>
```

15 public declaration(s), 15 documented.

## `kMaxLayers`

```cpp
inline constexpr int kMaxLayers = 32
```

 How many distinct layers exist.

 **32, because a `uint32` mask is one register** and the test is one AND.
 Going wider costs in the hottest loop in the renderer to buy something that
 is almost never needed — every engine that has picked this number has picked
 32, and none of them regret it.

## `kDefaultLayer`

```cpp
inline constexpr int kDefaultLayer = 0
```

 The layer everything starts on.

 Zero rather than "unassigned": an object with no opinion must still be
 visible to a camera with no opinion, and a nullable layer would make the
 common case the one that needs handling.

## `LayerMask`

```cpp
struct LayerMask
```

 A set of layers, as a bitmask.

 Used both as "the layers this object is on" and as "the layers this viewer
 affects" — the two are the same type because the test between them is
 symmetric, and giving them separate types would buy nothing but conversions.

 A value type with no invariants to break: any bit pattern is meaningful, and
 the empty mask legitimately means "nothing", which is how a light is switched
 off without destroying it.

## `LayerMask::LayerMask`

```cpp
LayerMask()
```

 Constructs an empty mask, matching nothing.

## `LayerMask::LayerMask`

```cpp
LayerMask(std::uint32_t value)
```

 Constructs from raw bits.
 @param value the bit pattern.

## `LayerMask::all`

```cpp
static LayerMask all()
```

 @returns a mask matching every layer. The default for a camera or light
          that has not been told otherwise.

## `LayerMask::none`

```cpp
static LayerMask none()
```

 @returns a mask matching nothing.

## `LayerMask::layer`

```cpp
static LayerMask layer(int index)
```

 A mask containing exactly one layer.
 @param index the layer, 0 to `kMaxLayers - 1`. **Out of range yields an
        empty mask** rather than wrapping — a wrapped shift is undefined
        behaviour, and silently aliasing layer 32 onto layer 0 is the kind
        of bug that presents as an unrelated object being lit.
 @returns the mask.

## `LayerMask::intersects`

```cpp
bool intersects(LayerMask other) const
```

 Whether this mask and another share any layer.

 **The one test the hot loops run.** Reads as "does this viewer's mask
 intersect this object's layers", which is the question being asked.
 @param other the other mask.
 @returns true when at least one layer is in both.

## `LayerMask::has`

```cpp
bool has(int index) const
```

 @param index the layer to test.
 @returns whether that layer is in this mask.

## `LayerMask::add`

```cpp
void add(int index)
```

 Adds a layer.
 @param index the layer to add. Out of range is ignored.
 @returns nothing.

## `LayerMask::remove`

```cpp
void remove(int index)
```

 Removes a layer.
 @param index the layer to remove. Out of range is ignored.
 @returns nothing.

## `LayerMask::empty`

```cpp
bool empty() const
```

 @returns whether this mask matches nothing.

## `LayerMask::operator==`

```cpp
bool operator==(const LayerMask & other) const
```

 @param other the mask to compare with.
 @returns whether both masks hold the same layers.

## `defaultObjectLayers`

```cpp
LayerMask defaultObjectLayers()
```

 The layer set an object with no explicit opinion belongs to.
 @returns a mask containing only `kDefaultLayer`.
