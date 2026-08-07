# Frame anatomy

The single ordered list of what runs when, and the defined points where
structural change applies. Owned by **T0100**.

`Application::run` implements exactly this order. If the two ever disagree, the
loop is wrong, not this document — but fix both in the same commit.

## Why this document exists at all

Many tickets each define a *piece* of the frame: T0014 the loop, T0057 the
fixed-step accumulator, T0062 behaviour dispatch, T0072/T0075 deferred queues,
T0048 a reload that must happen "between frames", T0077 scene transitions
applied from gameplay code. None of them own the frame's anatomy.

Left unowned, each system picks its own spot and the result is a specific,
well-known family of bugs: one-frame-lag chains, a camera that reads a transform
before the thing it follows has moved, a scene transition applied mid-iteration,
a module reload while a queue still holds module-typed payloads, entities
destroyed while a view is iterating them.

Every one of those is a decision that costs a paragraph here and a debugging week
later. That is the whole trade this document represents.

## The order

| # | Phase | Status | Owner |
|---|---|---|---|
| 1 | Frame begin — `clock_.tick()` | implemented | T0057 |
| 2 | Poll and dispatch input | implemented | T0018 |
| 3 | **Fixed-step block**, 0..n times | implemented | T0057 |
| 3a | — input snapshot | named, empty | T0068 |
| 3b | — `onFixedUpdate` | implemented | T0100 |
| 3c | — physics step | named, empty | T0051 |
| 3d | — post-physics resolution | named, empty | T0051 |
| 4 | Variable update — `onUpdate`, then **gameplay modules** | implemented | T0062 |
| 5 | Structural apply — entity create/destroy | named, empty | T0021 |
| 6 | Deferred drain — signals, message bus | named, empty | T0072, T0075 |
| 7 | Transform propagation | implemented | T0101 |
| 8 | **Late update** — `onLateUpdate` | implemented | T0081, T0052 |
| 9 | Transform propagation, again | implemented | T0101 |
| 10 | **Render** — see the breakdown below | implemented | T0025 |
| 11 | Present | named, empty | T0025, T0110 |
| 12 | **End-of-frame safe point** | named, empty | T0048, T0058, T0077 |
| 13 | Frame end — profiler frame mark, exit checks | implemented | T0014 |

**Phases 4, 7 and 9 changed together on 2026-08-06 (T0157), and the reason is
worth reading once.** Phase 4 now calls `ModuleHost::update`, so a gameplay
module gets a per-frame hook — and phases 7 and 9 propagate the scene the
application named through `Application::setServices`. Those had to land in the
same change: a module that moves a `Transform` while propagation does nothing
changes a value no renderer ever reads, so **the entity sits still, the
inspector shows it moving, and nothing reports a problem.** An empty phase is
only harmless while nothing upstream of it does work.

Propagation is a no-op on a clean scene — it walks the hierarchy and writes
nothing — so phase 9 is not a second cost in the ordinary frame where late
update moved nothing.

**"Named, empty"** means the phase exists in `Application::run` as a profiler
zone with a comment naming its owning ticket, and does nothing yet. That is
deliberate. A later system slots into a place that already exists and is already
in the right position, instead of choosing one — which is the retrofit this
document exists to avoid.

## Inside phase 10 — what "Render" actually does (2026-08-05)

The table above has said "Render / implemented / T0025" since before anything was
drawn. It now has an interior worth naming, because five tickets landed in it and
the next few slot into gaps between these steps rather than after them.

| Step | What | Owner |
|---|---|---|
| 10.1 | Resolve the camera for a view slot | T0081 |
| 10.2 | Build view + projection (reverse-Z) | T0081, T0130 |
| 10.3 | **Set the viewport from the resolved view**, not the target size | T0081 |
| 10.4 | `parseScene` — filter by **object layer mask**, then by mesh | T0085 |
| 10.5 | *(gap)* frustum cull, sort into queues | **T0045** |
| 10.6 | `gatherLights` — resolve placement, cap, warn | T0079 |
| 10.7 | *(gap)* per-object light selection | **T0079.3** |
| 10.8 | Write frame attribs — camera, renderer params, light array | T0079, T0134 |
| 10.9a | Submit **opaque and masked** draws through `PBR_Renderer` | T0028, D24, T0147 |
| 10.9b | **Scene snapshot** — copy colour and depth for material shaders | **T0147**, D37 |
| 10.9c | Submit **blended** draws; these may sample 10.9b | T0147, D37 |
| 10.10 | *(gap)* tonemap / upscale / composite pass | **T0096, T0111** |

Repeated per `RenderStack` layer for 10.1–10.9c, into **one shared colour
target** (T0027.5).

**Four things about this order are load-bearing:**

- **The layer mask test is first in 10.4, before the mesh check.** An object the
  camera does not render must cost nothing further — no GUID lookup, no list
  entry, no draw. It is also cheaper than the frustum test that will join it at
  10.5, so it stays first when that lands.
- **10.3 takes the viewport from the resolved camera, not from the pass.** They
  differ under a letterboxing aspect policy, and using the pass size stretches
  the image by exactly the letterbox ratio.
- **10.10 is one seam, wanted by three tickets.** Tonemapping (T0096), the
  render-scale upscale and UI-at-native resolution (T0111/D25) all belong
  between the world layers and the UI layers. D25 records that they must share
  it rather than each inventing one.
- **10.9 became three steps on T0147, and the split is not only for the
  snapshot.** Until then submission was one walk in draw-list order, so a
  blended surface could be drawn *before* the opaque geometry behind it — it
  blended against the clear colour and, worse, wrote depth that then rejected
  the geometry it should have been in front of. 10.9a/10.9c fix that, and 10.9b
  exists only because it is the one instant at which "the opaque image" is a
  thing that exists.

  **10.9b is a copy, and copies nothing unless something reads it.** The
  scan happens inside 10.9a: the opaque walk records, from the primitives it
  skips, whether the blend pass has work and whether any of its modules samples
  `g_SceneColour` or `g_SceneDepth` — a module never compiled counting as "might".
  So a scene of opaque geometry pays nothing, and a refraction is correct on the
  frame it first appears. D37 carries the full argument, including why the read
  is a copy rather than an alias of the live attachments, and why only a
  material with `alphaMode: Blend` may perform it.

  **Ordering *within* 10.9c is still submission order.** The back-to-front sort
  a correct transparent pass needs is T0045's, at 10.5, and T0147 deliberately
  did not build half of it.

**Lights are frame-wide today.** 10.6 gathers every enabled light and 10.8 writes
them all; nothing selects per object, so every light lights everything and
`Light` deliberately carries no layer mask until 10.7 exists to apply one.

## The decisions worth knowing

### Time is read once, at phase 1

Everything in the frame sees one delta. A system that calls the clock itself
mid-frame gets a different answer from the one before it and the two drift
apart. This is T0057's rule; the frame just enforces it by reading once and
handing the value down.

### The fixed-step block may run zero times, or several

`Clock::consumeFixedStep()` drains an accumulator, bounded by
`maxFixedStepsPerFrame`. Code in phase 3 **must not assume once per frame**.
Anything that must be reproducible belongs here rather than phase 4, because a
variable delta produces a different result on a faster machine — which is what
makes physics bugs unreproducible.

The cap matters: without it, a machine that cannot simulate as fast as real time
accumulates more debt each frame and runs more steps to pay it, making the next
frame slower still. The bound converts that spiral into the game running slow,
which is survivable and visible.

### Structural apply (5) is not at the safe point (12)

Entity creation and destruction queued during update takes effect at phase 5,
before transforms and before rendering.

Deferring it to phase 12 instead would be simpler and wrong: an entity destroyed
during update would still have its transform propagated at 7, be read by a
follower at 8, and be drawn at 10 — one last frame of a thing that no longer
exists. Applying at 5 means destruction is visible to every phase that could
observe it.

### Transform propagation happens twice (7 and 9)

Phase 7 gives world transforms to everything that moved during update, so
followers at phase 8 read final positions. Phase 9 catches whatever phase 8
itself moved.

One point is not enough in either position. Propagate only before late update
and a camera that moves itself renders a frame stale; only after, and followers
read stale targets at 8. T0101 owns the propagation itself and may make the
second pass incremental — most frames it will have almost nothing to do — but
both points stay.

### Late update (8) exists so followers are not order-dependent

Gameplay moves the player at phase 4. If the camera also ran at phase 4 with no
defined relative order, it would read either this frame's or last frame's
position depending on which layer happened to be registered first — intermittent
jitter that profiles as nothing.

Cameras, audio listeners and attachment points go at phase 8. This is a rule, not
a convention: putting follow logic in `onUpdate` is a bug even when it appears to
work.

### The safe point (12) is one point, after present

Three tickets independently need the same guarantee — a moment when nothing is
iterating the world and nothing is mid-draw:

- **T0048** hot-reloads the gameplay module "between frames"
- **T0058** swaps a reloaded asset
- **T0077** applies a scene transition requested from gameplay code, which must
  not destroy the scene currently being iterated

One point, one set of assertions, three bugs prevented. It sits *after* present
because that is what "between frames" means: the frame's output is already on
its way to the screen.

When those systems land, this phase must **assert before acting** that the
phase-6 queues are drained and no jobs are in flight (T0026). A reload while a
queue still holds a module-typed payload is the exact hazard T0075's review note
describes — the assertion is what makes it a loud failure rather than a
use-after-free in a type that no longer exists.

## Presentation is deliberately half-owned

Phases 11 and 13 are named here; the *policy* behind them — present mode, vsync,
frame-rate cap, focus-loss behaviour — is owned by **T0110**. This document says
where presentation happens in the frame. T0110 says how it behaves. Neither
should duplicate the other's half.

## What is not decided here

- **Threading.** Every phase above is main-thread. Which phases may fan out to
  the job system, and what that does to the ordering guarantees, is T0026 and
  T0050. Until then, assume serial.
- **Multiple worlds.** One scene, one frame. Additive scenes (T0077) will need
  to say whether phases 4–9 run per-scene or once across all of them.
- **Editor vs game timelines.** T0057 already supports two clocks; which phases
  read which is T0034/T0037's problem when play mode lands.
