# T0085 — Object layers and masks

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 430 |
| **Created** | 2026-08-03 |

## Inherited from T0081 (2026-08-05)

**The `Camera` component already carries a layer mask field and nothing filters
on it.** T0081 closed with 81.5 unbuilt and moved here, because filtering would
have meant inventing the vocabulary this ticket owns — what an object layer *is*
— inside a camera ticket.

So this ticket owes the camera path a definition and a filter: `parseScene`
(`hp/DrawSubmission.hpp`) is where the filter belongs, since its output is
already the explicit list a cull pass is meant to be inserted around.

## Why

A cross-cutting filter used in the hottest paths of three subsystems:

| Consumer | Uses it for |
|---|---|
| **Camera** | culling mask — which objects this camera renders at all |
| **Lighting** | light mask — which objects a light illuminates, and which cast its shadows |
| **Physics** | collision matrix — which layers collide with which |

Concretely: put an object on layer `Player` and a light on a mask excluding it,
and that light does not affect the player. Put the first-person weapon on
`Viewmodel` and only the weapon camera renders it.

Without this, the only tools are creating separate scenes or hacking per-object
flags into each subsystem — exactly the situation to avoid.

## Done when

- [x] Every renderable entity has a layer — `MeshRenderer::layers`, defaulting to layer 0
- [x] Cameras carry a culling mask; objects outside it are rejected during culling — in `parseScene`, **before any other check**, and counted as `culledByLayer`
- [x] Masks are applied during culling, not per-pixel — one AND in `parseScene`, and it runs first so an excluded object costs nothing further
- [x] Layers are **named in project settings** (T0078), not bare numbers — `hp::LayerNames`, stored under `layers` in a `SettingsStore`. Code says `names.indexOf("Player")`, not `7`

## Subtasks

- [x] 85.1 Layer definition in project settings — the set (`kMaxLayers`, `kDefaultLayer`) **and the names** (`hp::LayerNames`, round-tripping through `SettingsStore`)
- [x] 85.2 Layer field on the renderable component — `MeshRenderer::layers`
- [x] 85.3 Camera culling mask — applied in **`parseScene`**, which is what this ticket's own preamble specified; T0045 inserts frustum culling around it later

## Descoped 2026-08-05 — what left this ticket, and where it went

**These are no longer this ticket's checklist**, which is the point: a closed
ticket whose boxes are half unticked claims less than it did and reads as
abandoned. They were removed here and **written onto the receiving ticket**, so
the linkage reads both ways and nothing is lost.

Each left because it needs a subsystem that does not exist — not because it was
hard or skipped.

| Was | Went to | Because |
|---|---|---|
| Lights carry an illumination mask (85.4) | **T0079** | There are no lights. `hp::LayerMask` is ready for it |
| Separate shadow-casting mask (85.5) | **T0086** | There are no shadows. "Lit but does not cast" needs two masks, not one |
| Mask editor widget (85.6) | **T0032** | There is no inspector or panel framework |
| Physics collision matrix (85.7) | **T0051** | There is no physics. Jolt's own `ObjectLayer` must map onto `hp::LayerMask` in one place |
| Debug view of what a camera or light affects (85.8) | **T0061** | There is no debug draw. `DrawParseStats::culledByLayer` already counts it |

**What this ticket kept is what it could finish**: the type, the field, the
filter, and the names. Those are ticked because they are done and verified, not
because the rest was quietly dropped.

## Unblocked and closed 2026-08-05 — T0078 was worked rather than used as a parking space

The reopening below stands as the record. The fix was to **do T0078** instead of
citing it: `hp::LayerNames` now lives in a `SettingsStore`, round-trips as a
readable sequence, and resolves names to indices — so `layer 7` no longer appears
in code, which was the unmet requirement.

**One thing it wanted is still deliberately not done, and is recorded rather than
ticked quietly**: layers still *serialise* as an integer, not as names. Writing
names needs the `LayerNames` table threaded into `Serialize.cpp`, which has no
owner until T0024's `ProjectManager` — and inventing a global to reach it is
exactly the singleton T0078 declined. The `writeLeaf` branch carries a comment
marking the line. What this ticket asked for is met: **names exist in project
settings and code uses them.**

## Reopened 2026-08-05, same day — closing it was overstating

**Closed, then reopened within the hour, and the reason is worth keeping.** The
mechanism below is built and proven; five subtasks were moved to tickets that own
systems which do not exist yet, which is the T0095 → T0105 pattern and is fine.
The sixth move was not.

**85.1 — layer names — was moved to T0078 on the grounds of "blocked on something
that does not exist". That does not hold up.** T0078 is **order 360**, Phase 3,
complexity *Simple* — it sits **earlier** in the execution order than this ticket
at 430. It is not distant work.

And it is not peripheral. This ticket's own Done-when says *"Layers are **named
in project settings**, not bare numbers"*, and its notes say *"Name them in
project settings so `layer 7` never appears in code."* That is unmet: `layer 7`
is still `7`.

So the board was showing ✅ DONE for a ticket whose headline requirement was not
met. The remainder was recorded honestly in the "Not done" section — but **the
board is what people scan**, and a ticket that overstates what it achieved is
worse than one left open. Blocked on T0078, which is being worked next; this
closes as soon as there is a name table to read.

## Built and measured 2026-08-05 — the core, and what moved

**Closed on the half that was blocking, with the rest moved to the tickets that
own the things it needs.** The T0095 → T0105 pattern: five subtasks here were
waiting on project settings, an inspector, physics and a debug renderer, none of
which exist. Leaving the ticket open for them would have parked the *filter* —
which several tickets need now — behind an editor widget.

`engine/include/hp/Layers.hpp`, `MeshRenderer::layers`, `Camera::cullingMask`
retyped, `parseScene`'s filter, four serializer paths,
`tests/fast/layers_test.cpp`. **208 fast and 89 integration green on both
targets**, twelve new; the gpu bucket green on hardware.

### The shape, and why it is this shape

**An object carries layers; a viewer carries a mask.** A `MeshRenderer` says
which layers it is on, a `Camera` says which it renders, and the test is
`intersects` — one AND. The asymmetry reads the same way round as the question
("does this camera render this object?"), and both are the same `LayerMask` type
because a separate type per side would buy nothing but conversions.

**An object may be on several layers**, following Godot's `VisualInstance3D.layers`
rather than Unity's single-layer-per-GameObject. It costs nothing — the test is
the same AND either way — and it removes the "an object belongs to exactly one
category" constraint that Unity users routinely work around.

**The layer lives on `MeshRenderer`, not on a separate component.** A separate
`ObjectLayer` component would be more general, and it would cost a sparse-set
lookup per entity in the hottest loop in the renderer. `parseScene` already has
`MeshRenderer` in its view, so the field is free to reach. When physics arrives
(T0051) its colliders carry their own field — which is correct rather than a
compromise, since a collider is not a renderer, and 85.7's requirement is that
they share **the same definitions**, not the same storage.

**Default is layer 0 for objects and every layer for cameras.** That pairing is
what makes the no-configuration case work: an object nobody assigned is visible
to a camera nobody configured. An **empty** mask on either side is meaningful and
kept — a camera that draws nothing, or an object hidden without being destroyed.

**Out-of-range layers yield an empty mask rather than wrapping**, and this is the
one piece of defensive code here worth its cost. `1u << 32` is undefined
behaviour and on most hardware wraps to `1u << 0`, so layer 32 would silently
alias onto layer 0 — presenting as an unrelated object being lit by a light that
was explicitly told to exclude it.

### The filter runs first, and that ordering is asserted

In `parseScene` the mask test is **before** the mesh-GUID check, so a culled
object costs nothing further. That is observable through the statistics and a
test pins it: an entity with no mesh *and* the wrong layer is counted as
`culledByLayer`, never as `withoutMesh`.

`DrawParseStats::culledByLayer` is counted separately from every other rejection
deliberately. "The scene is empty", "the mesh is not assigned" and "this camera
does not render that layer" all look identical from the outside — a viewport
showing nothing — and the layer mask is by far the hardest of the three to guess.

### It closed a limitation T0027 had to work around

T0027's gpu composite test needed **two separate `Scene`s**, because a view slot
picks the camera and did not filter objects, so each layer drew the other's
geometry. That test now runs **one scene with two object layers** and produces
byte-identical pixels:

```
world only: left (0, 0, 0), right (0, 0, 255)
stacked:    left (0, 0, 0), right (0, 0, 0)
```

Kept that way on purpose: it is the **end-to-end proof of the filter on a
device**, not only in the fast bucket. If the mask were ignored, each camera
would draw both quads and the halves would stop distinguishing the layers.

### `Camera::cullingMask` changed type, and that touched serialisation

It was a bare `std::uint32_t`; it is now `LayerMask`, so there is **one
vocabulary** rather than two that drift. The cost was four paths in
`Serialize.cpp` — YAML write and read, binary write and read — following the
`Guid` precedent of a struct serialised as a scalar leaf.

**The on-disk shape is unchanged**: a plain integer, not a nested `{bits: N}`
map. Two existing tests already asserted the round trip
(`restored.cullingMask == original.cullingMask`) and YAML/binary agreement, so
they now cover the new type for free.

**When T0078 lands, that is the line to change** — layers named in project
settings should serialise as names, not numbers, and the comment in
`writeLeaf` says so where whoever does it will be standing.

### Not done, and honestly

- **Layers have no names.** `kMaxLayers` and `kDefaultLayer` exist; a
  `layer 7` still appears in code as `7`. That needs T0078, and it is the
  single biggest remaining gap — the ticket's own Done-when says *"named in
  project settings, not bare numbers"* and that is unmet.
- **Lights do not carry a mask**, because lights do not exist. T0079.
- **No inspector, no debug view, no physics matrix.** Moved, not forgotten.
- **`parseScene` is still linear over every drawable entity.** The mask makes
  excluded objects cheap, not free — frustum culling and any spatial structure
  remain T0045's.

## Notes / findings

**Layers are not gameplay tags (T0074), and conflating them is a real mistake.**

| | Layers | Tags |
|---|---|---|
| Count | small fixed set (32) | unlimited |
| Structure | flat bitmask | hierarchical |
| Purpose | engine filtering in hot loops | gameplay classification |
| Cost | one AND per test | a lookup |

Layers exist because culling and light selection run over every object every frame
and need a single-instruction test. Tags exist because gameplay wants to say
`enemy.flying.boss`. Use layers for what the *engine* filters, tags for what the
*game* means.

**Filter during culling, not in the shader.** A per-pixel mask test wastes all the
work of drawing the object. The mask belongs in T0045's culling pass and in
per-object light selection, so excluded work is never submitted.

**32 layers is the conventional limit** because a `uint32` mask is one register.
That is almost always enough, and going wider costs in the hottest loop in the
renderer. Name them in project settings so `layer 7` never appears in code.

Separate illumination and shadow masks matter more than expected — "lit by this
light but does not cast its shadow" is a common requirement for characters and
foliage.
