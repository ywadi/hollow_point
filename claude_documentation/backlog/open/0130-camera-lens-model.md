# T0130 — Camera lens model: decide what a camera describes, before content is authored against it

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 4 — Render layer |
| **Order** | 415 |
| **Created** | 2026-08-05 |
| **Refs** | [../completed/0021-scene-and-ecs.md](../completed/0021-scene-and-ecs.md), T0081, T0096, T0028, [../inprogress/0046-frame-render-targets.md](../inprogress/0046-frame-render-targets.md), T0111 |

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

- [ ] The parameterisation is decided and recorded: **artistic** (vertical FoV,
      as today) or **physical** (focal length + sensor size, with FoV derived).
      Either is defensible; the absence of a decision is not, because it is
      being answered by accumulation every time a camera is authored
- [ ] If physical: the conversion is one documented function, and the component
      stores exactly one of the two so they cannot disagree
- [ ] Aspect ratio is settled — derived from the viewport (T0081) rather than
      stored, with the reason recorded, since a stored aspect goes stale on every
      resize and produces subtly stretched output nobody notices for weeks
- [ ] Projection convention is stated and tested: reverse-Z or not, depth range,
      handedness, and which of those Diligent's `SetNearFarClipPlanes` /
      `GetProjAttribs` already decides for us on GL versus Vulkan
- [ ] Exposure ownership is decided: a camera property, a post-process
      property (T0096), or both with a stated precedence
- [ ] Depth of field: scoped in or explicitly deferred, with the *storage*
      decision made either way — an `aperture`/`focusDistance` pair costs two
      floats now and a component migration later
- [ ] Whatever is decided is reflected in the `Camera` component and its
      registration in `engine/src/Scene.cpp`, so the editor shows the real
      vocabulary rather than the placeholder

## Subtasks

- [ ] 130.1 Decide artistic versus physical parameterisation and record the
      rejected option with its reason
- [ ] 130.2 Aspect ratio: derive from viewport, and say so in the header
- [ ] 130.3 Projection convention — reverse-Z, depth range, handedness — stated,
      and measured against both backends rather than assumed. GL and Vulkan
      disagree about clip-space Z and Diligent has an explicit flag for it;
      this is exactly the kind of thing that is right on one backend and
      mirrored on the other
- [ ] 130.4 Exposure ownership versus T0096
- [ ] 130.5 Depth-of-field storage: in, or deliberately out
- [ ] 130.6 Amend the `Camera` component and its reflected properties to match

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
