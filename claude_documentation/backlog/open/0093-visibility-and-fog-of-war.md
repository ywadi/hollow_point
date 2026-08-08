# T0093 — Visibility as an engine capability: prove a vision mechanic needs no engine changes

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 540 |
| **Created** | 2026-08-03 |
| **Reframed** | 2026-08-04 — see "What this ticket is, and what it stopped being" |
| **Refs** | T0044 (dropped), T0126, T0045, T0060, T0079, T0085, T0086, T0094, T0120, T0061, T0083, T0109; **T0147** ([../completed/0147-engine-intermediates-for-shaders.md](../completed/0147-engine-intermediates-for-shaders.md)) / **D37** — **the mechanism is built and this ticket must use it rather than invent one**: a layer renders the visibility field into a target it owns, calls `setGameTexture("visibility", view)`, and a material samples it as an ordinary declared `Texture2DArray` under that name. **No `HpSurfaceInput::Visibility` field was added, deliberately** — a contract field returning 1.0 because nothing computes it is the exact failure the arrival table exists to prevent, so it arrives with *this* ticket's system and is one line then |

## What this ticket is, and what it stopped being

**This is a validation ticket, not a feature ticket.** It exists to prove the
engine is general enough that a game can build a vision-based visibility
mechanic — forward cone, peripheral radius, occluded by walls, dithered edges,
remembered explored areas — **entirely in its own gameplay module, with zero
engine changes.**

It used to be a ticket to *build* that mechanic. That was wrong, for the same
reason T0044 was wrong: HollowPoint is an engine for a studio's several games
(T0109), and a vision cone with alert states and team vision is a **game
feature**, not an engine subsystem. Shipping `VisionSource` in the engine would
bake one game's mechanic into every game the studio makes, and the second game
would either inherit machinery it does not want or fork it.

The ticket already argued this against itself and only applied it once:

> **Explored-area memory is NOT an engine feature.** The engine exposes the
> primitives; the game builds the policy... baking one in would be wrong.

That reasoning is right and it generalises. It applies to the vision cone too,
and to the hide/dim response, and to team vision. This reframe applies it
consistently.

**What survives is the valuable part**: this scenario is a demanding, concrete
test of engine generality, and the architectural constraints it imposes are
real. It is the example that keeps the render layer honest.

## Why the scenario is worth keeping as a test

A vision mechanic is unusually good at catching a sealed render pipeline,
because it needs the engine to do several things it is tempting not to allow:

- compute occlusion **for something other than illumination** — shadow machinery
  used to answer "can this be seen", where light is only the mechanism
- hand a raw per-pixel intermediate to a **material shader**, rather than
  emitting a finished colour
- let gameplay own a **persistent render target** and accumulate into it across
  frames
- let gameplay **sample that target** in a material and **serialise** it
- feed a per-object result back into **culling**, so hidden things are not
  rendered at all rather than merely drawn dark

An engine that can do all five can express a great many mechanics nobody has
thought of yet. An engine that cannot will meet this as a post-process hack
bolted on later — which is the outcome this ticket exists to prevent.

## The engine capabilities this validates — each owned elsewhere

This ticket **builds none of these.** It asserts they exist and are composable,
and it fails if they are not. Each is a general capability with its own owner:

| Capability | Owner | Why it is general, not vision-specific |
|---|---|---|
| Material shaders receive engine intermediates, not finished colour | **T0147** | any custom shading effect needs this |
| Occlusion map renderable from an arbitrary frustum | **T0086** | shadows, projectors, portals, light cookies |
| A projector that does **not** shade | **T0079** | decals (T0108), cookies, any masked projection |
| Layer masks controlling what occludes | **T0085** | already general |
| Per-object visibility result usable by culling | **T0045** | already general — see 45.6's shape constraint |
| Gameplay-ownable persistent render targets, custom passes, sampling | **T0094** | the whole point of gameplay-extensible rendering |
| Render-to-texture with independent per-camera state | **T0120** | already general |
| Debug view of an arbitrary engine buffer | **T0061** | already general |
| Serialising a gameplay-owned target | **T0083** | already general |

**If a capability turns out to be missing, that is a gap in the owning ticket,
and it is filed there.** Not built here. This ticket's output when something is
missing is a bug report against T0060/T0079/T0086/T0094, not an engine patch.

## Done when

- [ ] A vision mechanic — cone plus radial, wall-occluded, dithered edge,
      hide-or-dim response, accumulated explored-area memory — is demonstrated
      **in `samples/sandbox/`**, i.e. in a gameplay module, using only public
      engine API
- [ ] **Zero engine changes were needed to build it.** If any were, they are
      recorded here as gaps against the owning ticket above, and the ticket does
      not close until they land there rather than here
- [ ] No `VisionSource`, no vision cone, no fog-of-war policy and no dither
      pattern ships **in the engine** — all of it lives in the sample module
- [ ] The public API surface it needed is listed here, so T0109's installed-engine
      SDK is known to carry it
- [ ] Cost is bounded and profiled (T0030), so "possible" is not confused with
      "usable"

## Subtasks

- [ ] 93.1 Write the capability list above as concrete API expectations, and
      check each against its owning ticket's current plan — **do this early**,
      because it is where the constraints get honoured or lost
- [ ] 93.2 Build the mechanic in `samples/sandbox/`, gameplay-side only
- [ ] 93.3 Record every engine change it turned out to need; file each against
      its owner
- [ ] 93.4 Profile it (T0030)
- [ ] 93.5 List the public API surface used, for T0109

## The architectural constraint this imposes — unchanged and still binding

**Materials must receive the raw visibility factor, not a finished shaded
colour.** If shading is a sealed pipeline that consumes lights and emits pixels,
none of this is expressible and it ends up bolted on as a post-process hack. So:

- the shading path exposes per-pixel engine intermediates to material shaders
- custom material shaders (T0060) can consume them — dim, desaturate, dither, hide
- they are first-class inputs alongside albedo and roughness, not special cases

This is recorded here because it constrains **T0060, T0079 and T0086, all of
which are earlier in the plan** — which is the whole reason this ticket is worth
keeping rather than deleting. The constraint has to exist before the tickets it
constrains are built.

**Dithering needs the raw factor plus screen-space coordinates.** A dithered
edge is a threshold against a Bayer or blue-noise pattern in screen space, so
material shaders need the value *and* the screen position as documented inputs
rather than something a shader reconstructs. The pattern itself is the game's.

**Visibility is independent of illumination.** A visible area renders normally
regardless of what lights reach it — a dark room inside the cone is *visible and
dark*, not hidden. Practically: whatever term a game applies, it applies after
shading rather than folded into light accumulation, which is only possible if
the engine exposes the intermediate. Same constraint, stated from the other
side.

**Per-pixel and per-object are both needed.** Per-pixel gives correct silhouettes
and soft edges; per-object lets a fully hidden entity be skipped entirely —
often *required*, so hidden entities are genuinely not rendered rather than
merely dark. The engine must support both; which a game uses is the game's call.

## Notes / findings

**Vision sources are not lights, and in the engine they are neither.** The
original note said they should be their own component type. The reframed answer:
the engine provides a **projector that does not shade** (T0079) and an occlusion
map from an arbitrary frustum (T0086), both of which decals and light cookies
want anyway. A "vision source" is a gameplay concept composed from those. Alert
states, team vision and shared sight are game logic and never appear in engine
headers.

**The `Escape from Duckov` reference is retained deliberately, as a target to
test against rather than a specification to implement.** Naming a concrete,
demanding mechanic is what makes this a real test; the risk is only in confusing
the example with the requirement, which is what happened the first time.

**This is a generality test, and generality tests belong late.** Order 540 keeps
it after T0060 (450), T0079 (470), T0086 (480), T0094 (510) and T0120 (515) —
everything it validates. Running it earlier would produce gaps against tickets
nobody has started.

**T0098's dependency was rechecked and corrected (2026-08-04, T0126).** It
refd this ticket as evidence the engine needs navigation ("vision cones and
alert states exist") — T0044-shaped reasoning, inferring engine scope from a
hypothesised game. T0098's first scope item now decides navigation on its own
engineering grounds (cost of recastnavigation against what else assumes a
navmesh), and T0044 is off its Refs. Nothing here is cited as evidence for
another ticket's existence any more.

### 2026-08-08 — considered for merge into T0094, and **declined**

The proposal was to fold this into [T0094](../inprogress/0094-gameplay-extensible-rendering.md)
as its acceptance test, on the grounds that this ticket's title is literally
*"prove a vision mechanic needs no engine changes"* and that it builds nothing.
The second half of that is true and the conclusion does not follow.

**Two reasons, and the first is the decisive one.**

1. **It would block T0094 behind six unstarted tickets.** T0094 is now **#2 in
   the current sequence** — the frame seam that four other tickets ride. This
   ticket validates **nine** owners (T0147, T0086, T0079, T0085, T0045, T0094,
   T0120, T0061, T0083), of which T0094 is *one row of the table*. Merged, T0094
   could not close until shadows, lights, culling, render-to-texture, debug draw
   and the save system had all landed. Its own note says it is ordered at 540
   **deliberately**: *"this is a generality test, and generality tests belong
   late… running it earlier would produce gaps against tickets nobody has
   started."*
2. **The overlap is one subtask, not the ticket.** T0094.9's worked example
   (accumulate visibility into a persistent texture, from a module) proves
   *T0094*; 93.2 extends the same artefact into the full mechanic — cone,
   peripheral radius, wall occlusion, dithered edges, remembered explored areas —
   to prove *the render layer*. **They are the same artefact at two stages**, and
   that is now said on both tickets rather than resolved by merging.

**The precedent this follows is T0167**, which was exactly this shape: point the
engine at something it did not help make, and file what breaks against the owner
rather than patching it in place. That ticket was one of the most productive on
the board.
