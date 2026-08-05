# T0130 — Camera lens model: decide what a camera describes, before content is authored against it

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 4 — Render layer |
| **Order** | 415 |
| **Created** | 2026-08-05 |
| **Refs** | [../completed/0021-scene-and-ecs.md](../completed/0021-scene-and-ecs.md), T0081, T0096, T0028, [../completed/0046-frame-render-targets.md](../completed/0046-frame-render-targets.md), T0111 |

## Why

T0021 shipped a `Camera` component to have something concrete for entities to
carry, and it made the minimum viable choice without arguing for it:

```cpp
struct Camera {
    float verticalFov{1.0472F};   // 60 degrees, radians
    float nearPlane{0.1F};
    float farPlane{1000.0F};
    bool  orthographic{false};
    float orthographicSize{5.0F};
};
```

That is enough to build a projection matrix and nothing more. **No ticket owns
the question of whether it is the right vocabulary**, and the surrounding tickets
each own a neighbouring piece while carefully not owning this one: T0081 resolves
*which* camera is active and its viewport and culling mask; T0096 owns the HDR
pipeline and tonemapping; T0111 owns anti-aliasing and render scale. What a
camera *is* falls between them.

**The timing argument is the same one T0112 made about strings, and it is why
this is High rather than Medium.** A `verticalFov` authored into scenes,
prefabs and gameplay code is cheap to write and expensive to reinterpret. If the
engine later moves to a physical model — focal length in millimetres against a
sensor size — then every authored camera, every `setFov` call and every saved
game carries a number whose meaning changed. Deciding now costs one discussion;
deciding after Phase 6 costs a migration nobody can verify, because a wrong FoV
looks plausible.

Position and orientation are explicitly **not** in scope: they come from the
entity's `Transform`, and T0101's propagation is what makes a camera on a boom
arm or a head socket work. That split is settled and this ticket does not
reopen it.

## Done when

- [x] The parameterisation is decided and recorded: **artistic** (vertical FoV,
      as today) or **physical** (focal length + sensor size, with FoV derived).
      Either is defensible; the absence of a decision is not, because it is
      being answered by accumulation every time a camera is authored
- [x] If physical: the conversion is one documented function, and the component
      stores exactly one of the two so they cannot disagree
- [x] Aspect ratio is settled — derived from the viewport (T0081) rather than
      stored, with the reason recorded, since a stored aspect goes stale on every
      resize and produces subtly stretched output nobody notices for weeks
- [x] Projection convention is stated and tested: reverse-Z or not, depth range,
      handedness, and which of those Diligent's `SetNearFarClipPlanes` /
      `GetProjAttribs` already decides for us on GL versus Vulkan
- [x] Exposure ownership is decided: a camera property, a post-process
      property (T0096), or both with a stated precedence
- [x] Depth of field: scoped in or explicitly deferred, with the *storage*
      decision made either way — an `aperture`/`focusDistance` pair costs two
      floats now and a component migration later
- [x] Whatever is decided is reflected in the `Camera` component and its
      registration in `engine/src/Scene.cpp`, so the editor shows the real
      vocabulary rather than the placeholder

## Subtasks

- [x] 130.1 Decide artistic versus physical parameterisation and record the
      rejected option with its reason
- [x] 130.2 Aspect ratio: derive from viewport, and say so in the header
- [x] 130.3 Projection convention — reverse-Z, depth range, handedness — stated,
      and measured against both backends rather than assumed. GL and Vulkan
      disagree about clip-space Z and Diligent has an explicit flag for it;
      this is exactly the kind of thing that is right on one backend and
      mirrored on the other
- [x] 130.4 Exposure ownership versus T0096
- [x] 130.5 Depth-of-field storage: in, or deliberately out
- [x] 130.6 Amend the `Camera` component and its reflected properties to match

## Notes / findings

**A physical model is not automatically the better answer**, and the survey
should not assume it. It buys a vocabulary artists and photographers already
share, and it makes depth of field physically meaningful rather than a pair of
tuning knobs. It costs a conversion at every call site that thinks in degrees, a
sensor-size constant that means nothing to a gameplay programmer setting up a
top-down camera, and a class of bug where a "50mm" camera looks wrong because the
sensor size differs from what the artist assumed. Engines split on this for real
reasons — several offer both and let the project choose, which is a third option
and has its own cost in two code paths that must agree.

**The reverse-Z question is the one most likely to be discovered late and hurt.**
It is a depth-precision decision that touches the projection matrix, the depth
buffer format in T0046, the comparison function in every pipeline state, and any
shader that reconstructs position from depth. Choosing it after those exist is a
sweep through all of them; choosing it now is a line in a matrix helper.

**Diligent already carries some of this.** `BasicMath.hpp` has
`SetNearFarClipPlanes` and projection helpers with an explicit
`bIsGL` flag, precisely because OpenGL's clip-space Z is `[-1, 1]` and Vulkan's
is `[0, 1]`. Both backends ship here (D16, and no D3D under MinGW), so this is
not theoretical — it is a thing that will be correct on Vulkan and wrong on GL,
or the reverse, unless 130.3 is measured on both.

**Do not let this grow into a post-processing ticket.** Bloom, motion blur and
colour grading are not camera parameters even when a real camera exhibits them,
and T0096 owns the HDR chain they live in. This ticket decides *what a camera
describes*; the passes that consume it are elsewhere. The single exception is
depth of field, which is here only because its **storage** cannot be retrofitted
cheaply — the pass that implements it is not this ticket's work.

---

## Resolved — 2026-08-05

### 130.1 Artistic, not physical

`verticalFov` stays the stored truth. Focal length is a conversion at the edge:
`verticalFovFromFocalLength` / `focalLengthFromVerticalFov`, exact inverses,
tested at 14/24/35/50/85/200mm.

**The rejected option and why.** A physical model is genuinely defensible, and
the deciding argument was not "FoV is simpler" — it was *where the conversions
land*. Every consumer of a camera needs the field of view: the projection
matrix, the culling frustum, the editor gizmo, any code framing a target. None
of them needs millimetres. Storing focal length means converting at each of
those sites, and a conversion repeated at N sites is N chances to pick a
different sensor size. Storing both invites the two to disagree, which is a bug
invisible in an inspector.

`sensorHeightMm` is stored per camera (default 24mm, full-frame 35mm) because
"50mm" is meaningless without a sensor to measure it against — and a hidden
global constant is precisely how a 50mm camera ends up looking wrong. It does
not participate in the projection at all; it is the reference frame for the
conversions and for depth of field.

Verified: a 50mm lens on a 24mm sensor gives 26.99° vertical. If the maths had
used sensor *width* it would give ~40°, which is the mistake the test pins.

### 130.2 Aspect is derived, never stored

`projectionMatrix(camera, aspect, clip)` takes it as a parameter. A test asserts
that widening the viewport scales `_11` and leaves `_22` alone — a vertical FoV
is stable under a shape change, so an ultrawide monitor shows more at the sides
rather than cropping the top.

### 130.3 Left-handed, [0, 1] clip space, reverse-Z — and the GL trap was real

**Handedness is Diligent's and not ours to reopen.** Left-handed, +Z into the
screen. `BasicMath.hpp` explicitly does *not* invert Z the way `gluPerspective`
does, keeping the DX camera-space convention even on GL. Picking the other
handedness means fighting every helper, sample and shader in the dependency.

**The clip-space trap the ticket predicted was present in this tree**, and had
not bitten only because nothing had built a projection matrix yet:

- `EngineGLCreateInfo::ZeroToOneNDZ` defaults to **false**, and the engine never
  set it. Measured on an RTX 2080: GL reported `NDC.MinZ = -1`, Vulkan reported
  `0`. Two backends, two different clip spaces.
- Setting it, and refusing a device that cannot honour it, makes both `[0, 1]`.
  Measured after the change: Vulkan `minZ=0 yToV=-0.5`, OpenGL `minZ=0
  yToV=+0.5`.
- The `yToV` sign genuinely differs and always will — that is the texture-space
  disagreement, not the depth one, and it is why `ClipSpace` carries both.

The before/after was measured by flipping the flag off and re-running, not
inferred: with `ZeroToOneNDZ = false` the GL device is refused and the GPU case
skips.

**Reverse-Z is on**, implemented by swapping near and far into
`SetNearFarClipPlanes`. That is exact rather than a trick: the helper solves for
a mapping sending its `zNear` argument to clip 0, so handing it the planes
reversed sends the true near plane to 1. It holds for the orthographic form too,
which uses a different pair of expressions — so the tests assert *endpoints
after the perspective divide*, not matrix coefficients, and cover ortho
separately. That is the case that would have caught reverse-Z applied to
perspective and forgotten on ortho.

**The measurement that justifies it**, over 900m–901m with a 0.1m near plane:

| Mapping | Distinct float values between 900m and 901m |
|---|---|
| Reverse-Z | **135,604** |
| Conventional | **2** |

That table is the second version of the measurement. The first compared the
*absolute* depth difference and reported 1.233e-07 against 1.192e-07 — nearly
identical, and it disproved a "100x better" assertion I had already written. The
reason is that in exact arithmetic the two mappings are `1 - x` of each other,
so the absolute gap is necessarily almost the same; what differs is where on the
float number line those values sit. Measured in ULPs the ratio is ~68,000x. The
first measurement was not wrong, it was measuring the wrong quantity, and it
only surfaced because the assertion failed.

### 130.4 Exposure is a camera property

`Camera::exposureEv100`, in EV100. The argument is that exposure belongs to a
*view*: a frame can hold a main view, a security monitor and a portal looking at
the same world and needing different exposures, which one post-process value
cannot express.

Precedence, now recorded on T0096 as well: **T0096 owns the tonemap curve and
any auto-exposure, and auto-exposure writes this field rather than shadowing
it.** A second exposure on the post-process stack would multiply with this one,
and every individual number would look reasonable.

### 130.5 Depth of field: storage in, pass out

`depthOfField`, `aperture` (f-number), `focusDistance` (metres). The pass is not
this ticket's work and does not exist. The storage is here because it is what
cannot be retrofitted cheaply, and because `sensorHeightMm` — earned by 130.1 —
is what makes a physically meaningful circle of confusion computable later.

### 130.6 Component and reflection amended

`Camera` moved from `hp/Scene.hpp` to a new `hp/Camera.hpp`, so the vocabulary
and the maths that consume it live together; `Scene.hpp` includes it, so nothing
downstream changed. All five new fields are registered in
`registerCoreComponents()`, so the editor inspector shows the real vocabulary.

## Evidence

Full suite, both targets (Linux native, Windows under wine):

```
fast:        101 cases | 213,493 assertions | 0 failed
integration:  56 cases |     305 assertions | 0 failed
gpu:           2 cases |      92 assertions | 0 failed
```

Device measurements on an NVIDIA RTX 2080, both backends:

```
clip space on default: minZ=0 yToV=-0.5
clip space on OpenGL:  minZ=0 yToV=0.5
900m..901m resolves to 135604 float steps under reverse-Z, and 2 under the
conventional mapping
```

`zig build docs` regenerates and passes.

## Not done, and not verified

- **No pixel has been drawn through this projection.** Every depth assertion is
  arithmetic on the matrix after a perspective divide, plus a device-measured
  clip space. What is *not* proven is that a triangle at 900m actually occludes
  one at 901m on real hardware, because that needs a pipeline state and a
  shader — T0028's work. The convention T0028 must set is written into its
  ticket rather than left to be rediscovered.
- **Reverse-Z is not exercised end to end on OpenGL.** The clip space is
  measured on both backends and the matrix is backend-independent given that,
  but no GL draw has tested the depth comparison.
- **The GL clip-control refusal path is measured, but no device that lacks the
  extension was available** — it was produced by disabling the request, which
  exercises the same branch for the same reason. A driver genuinely without
  `GL_ARB_clip_control` has not been seen here.
- **`aperture` and `focusDistance` are storage only.** Nothing reads them. They
  are not validated beyond being floats, and no circle-of-confusion maths
  exists.
- **`exposureEv100` is not applied anywhere**, and cannot be until T0096.
- **An infinite far plane was not adopted.** It pairs conventionally with
  reverse-Z and improves precision further, but it complicates culling and
  shadow cascades, and nothing here needed it. Left as a possible refinement
  under T0045.

## Follow-on found while doing this

**The API doc generator did not emit namespace-scope constants at all.**
`hp/DepthConvention.hpp` rendered as "0 public declaration(s)" — a page whose
entire content is two `inline constexpr` values carrying a binding engine-wide
convention. `docs/api/index.md` claims to list every public symbol and that was
false for constants. Fixed here rather than filed, since the whole point of
deciding reverse-Z now is that T0028 can find it: `VAR_DECL` added, restricted
to namespace scope, with the signature rebuilt from tokens so the value shows.
`kModuleBuildIdSymbol`, `kModuleApiSymbol` and `kDefaultSensorHeightMm` appeared
as a side effect.

**GPU tests had no log sink**, so a device the engine *deliberately refused* was
indistinguishable from a machine with no GPU — both printed "skipping". That is
how the D15 compute floor and this ticket's clip-space floor would both fail
silently on CI. A console sink is now attached once per bucket.
