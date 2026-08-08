# `<hp/HandednessConvention.hpp>`

*Generated from `engine/include/hp/HandednessConvention.hpp` — do not edit.*

```cpp
#include <hp/HandednessConvention.hpp>
```

1 public declaration(s), 1 documented.

## `kRightHandedCameraSpace`

```cpp
inline constexpr bool kRightHandedCameraSpace = true
```

 Whether camera space is right-handed -- the camera looks down its own
 **negative** Z, with +X right and +Y up.

 **True, declared rather than defaulted, and this is the single place that
 decides it.** The engine matches glTF, which is also Blender's, Maya's,
 Houdini's and Godot's convention; Unity and Unreal are the outliers.
 Matching the format we import means a Blender artist's model arrives as
 authored, a position debugged in the engine has the same sign as in
 Blender, and a round trip is lossless.

 **The rejected alternative was the Unity answer: mirror at import.** It
 makes the engine internally correct and leaves the *boundary* permanently
 converted, so every future attribute type -- tangents, morph targets,
 skinning, physics collision normals, camera-relative audio -- owes a
 conversion, and a missed one renders subtly mirrored rather than failing.
 That is the silent-failure class **D35** exists to prevent, and this
 codebase was bitten by it three times in two days. See `D33`'s amendment
 for the full argument, and `kImportMirrorsContent` in
 `WindingConvention.hpp` for the constant that would have carried it.

 **It is not free, and the cost is that it is not local.** A handedness
 change is a *mirror*, and four things must agree:

 1. `hp::projectionMatrix` negates the third row of Diligent's left-handed
    form, which is exactly left-multiplying by `diag(1, 1, -1, 1)`. Nothing
    else in the matrix moves -- see the note below on reverse-Z.
 2. `kFrontFaceCounterClockwise` counts this as one mirror. The
    `static_assert` in `WindingConvention.hpp` is what enforces it.
 3. `PBRFrameAttribs::Camera.fHandness` is `+1` -- Diligent's own spelling
    of "right-handed" (`BasicStructures.fxh:100`). It orients the normal
    DiligentFX reconstructs from screen-space gradients.

    **It is in a stated disagreement, and T0166.8 re-states it rather than
    settling it.** The field's only consumer is `GetPerturbNormalInfo`'s
    zero-length-normal branch (`PBR_Shading.fxh:163-176`), which computes
    `normalize(cross(dPos_dx, dPos_dy)) * fHandness` under the comment *"the
    normal computed from gradients is always facing the viewer"*. That cross
    product faces **away** from the viewer here -- necessarily, because
    Diligent's Vulkan backend flips the viewport to D3D's screen convention
    so `ddy` runs *down*, and `cross(right, down)` points into the screen.
    T0166 re-derived that from the framebuffer rather than treating it as a
    measured curiosity, and `custom_shader_material_test` pins it. So `-1` is
    the value that makes the branch do what its comment says, and `+1` is the
    value their field *name* asks for. Upstream conflates "camera-space
    handedness" with "the screen basis's chirality about the normal", which
    are the same number only under the left-handed camera they assume
    throughout.

    **Nothing in this engine reaches that branch** -- it runs only for a
    fragment whose interpolated mesh normal is zero-length, and the importer
    refuses a mesh with no `NORMAL` at all. **The trigger is therefore an
    import one: the first mesh accepted without normals**, which is
    `T0168`'s (asset import coverage). Changing the constant before then
    would be guessing in the other direction with no way to tell.
 4. Any tangent frame rebuilt from `ddx`/`ddy` must not depend on the screen
    basis's chirality at all. `HpParallaxUv` solves `dp = T*du + B*dv`
    directly and divides by the **signed** UV determinant, which is
    DiligentFX's own construction (`ShaderUtilities.fxh:51-55`) and has no
    cross product in it. The Schueler frame that was here until T0166.2 did
    carry that chirality, and came back rotated 180 degrees on every asset.
    A game shader rebuilding one has the same obligation and no engine
    function to call yet (`13-shader-capability-matrix.md`, T0168).

 **Reverse-Z composes with this cleanly, and that was measured before
 anything was swept** (T0165.1). Diligent's `SetNearFarClipPlanes` is told
 the buffer is reversed by handing it the planes the other way round, and
 it writes only `_33`, `_43` and `_34`. Negating `_33` and `_34` afterwards
 leaves `_11`, `_22` and `_43` untouched, so:

 - the X and Y columns are identical -- a right-handed projection is **not**
   a screen-space mirror, and world +X still lands on screen right;
 - the Z endpoints are identical with the sign of view-space Z flipped:
   near -> 1, far -> 0, exactly as before;
 - the same one-line rule works for the orthographic form, whose `_34` is
   already zero;
 - the 4x4 determinant changes sign, which *is* the mirror, and is the only
   thing that changes.

 The display consequence is the point of the whole exercise: with an
 identity camera the screen frame (right, up, toward-viewer) is
 `(+X, +Y, +Z)` in world coordinates -- right-handed, so **content is
 displayed unmirrored**. Under the previous left-handed convention it was
 `(+X, +Y, -Z)`, and `tests/gpu/chirality_test.cpp` measured that mirror on
 hardware before it was removed.
