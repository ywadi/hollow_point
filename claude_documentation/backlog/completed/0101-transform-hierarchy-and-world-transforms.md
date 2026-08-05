# T0101 — Transform hierarchy propagation and world transforms

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Order** | 210 |
| **Created** | 2026-08-03 |
| **Refs** | T0021, T0028, T0035, T0045, T0049, T0051, T0057, T0100, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md) |

## Why

T0021 makes transform parenting required and defines the component; T0028
consumes world transforms for drawing; T0045 needs world-space bounds; T0049's
sockets attach entities under animated joints; T0064's gizmos edit world-space
and store parent-relative. But **no ticket owns computing world transforms**:
dirty tracking, propagation order, caching, reparenting semantics, and where in
the frame propagation runs.

Without an owner, each consumer either walks parent chains per query (quietly
quadratic in hierarchy depth, per frame) or keeps its own cache — and two caches
that disagree present as "the object renders in one place and picks in another",
which is a miserable bug to localise. This is the largest piece of genuinely
missing engine code between T0021's component and T0028's renderer.

## Done when

- [x] One API yields any entity's world transform, correct in the same frame
      after any local edit, reparent, or hierarchy change
- [~] Propagation is a single pass at defined points in the frame (T0100), not
      per-query parent-chain walks — **the pass is built and is genuinely single;
      nothing calls it in the frame yet.** See "Not done"
- [x] Reparenting semantics are explicit: keep-world (editor drag in T0035) and
      keep-local both exist as deliberate operations
- [x] Dirty tracking means unchanged subtrees cost nothing — asserted on the
      recomputed count, not claimed
- [x] Previous-frame world transforms are retained, so T0057's interpolation
      alpha and (later) motion vectors have something to interpolate between
- [~] Sockets compose: an entity parented to an animated joint (T0049) gets a
      correct world transform, including its own children — **moved to T0049**,
      which owns joint sampling; there is no animation runtime to parent to
- [x] Tests: deep chains, reparent-then-read in one frame, delete a parent with
      children, serialization round-trip of hierarchy (T0022)

## Subtasks

- [x] 101.1 Storage: local TRS + cached world matrix + parent/child links in
      the transform component. Decide the runtime link representation
      (`entt::entity`) versus the serialized one (GUID, per T0022) and where
      the fix-up happens on load
- [x] 101.2 Dirty flagging and a topologically-ordered propagation pass
- [x] 101.3 Reparent API with keep-world and keep-local modes
- [~] 101.4 Placement in the frame (T0100): after behaviours and animation,
      before culling and submission; decide how physics writes interact
- [x] 101.5 Previous-frame transform storage for interpolation (T0057/T0051)
      and the TAA/motion-vector hook T0096 leaves open
- [x] 101.6 Deletion semantics: deleting a parent — children die too, or are
      reparented? Decide, and match the editor's behaviour (T0035/T0065)
- [x] 101.7 Tests

## Notes / findings

### Obligation from T0111 / D25 (2026-08-05) — 101.5's motion-vector hook is now committed

**D25 makes TAA the engine's antialiasing, so motion vectors stop being a
"hook TAA might want" and become a prerequisite.** They also feed every temporal
upscaler (DLSS, DirectSR, FSR2), which D25 adopts as an abstraction — so one
implementation serves both.

State today, measured: `PSO_FLAG_COMPUTE_MOTION_VECTORS` is masked off in
`SceneRenderer`'s `kFeatureMask`, but **`PBRFrameAttribs::PrevCamera` is already
written every frame**, so the camera half costs nothing.

**The two hard cases, which decide whether TAA looks good or smeared:**

- **Skinned meshes need previous *joint* matrices**, not just a previous world
  transform. `PBR_Renderer` has `MaxJointCount` and a joints buffer; the previous
  frame's contents must be kept. Get this wrong and animated characters ghost —
  the most visible object in most games.
- **Static geometry is the easy case** and is the whole of what a naive
  implementation covers.

See [../completed/0111-anti-aliasing-and-render-scale.md](../completed/0111-anti-aliasing-and-render-scale.md).


### Frame anatomy — phases 7 and 9 — transform propagation (T0100, D17)

Propagation has **two** points, phases 7 and 9. Phase 7 serves followers at
phase 8; phase 9 catches what phase 8 itself moved. This ticket may make the
second pass incremental — most frames it will have almost nothing to do — but
may not remove it.

The full order is in [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md); the decision and what it rejected is **D17** in the
decision log. If this ticket needs a phase that does not exist, that is a change
to T0100's document and to D17 — not a new call bolted into `Application::run`.

**Diligent already provides the math.** `DiligentCore/Common/interface/
AdvancedMath.hpp` has `ViewFrustum`/`ViewFrustumExt`,
`ExtractViewFrustumPlanesFromMatrix` (with an explicit OpenGL flag),
`GetBoxVisibility` and bound-box transforms — T0045 and T0081 should consume
these rather than re-derive them, and this ticket's world matrices are what
feeds them. `BasicMath.hpp` is already the chosen math library (T0056).

**Previous-frame transforms are cheap on day one and painful to retrofit.**
Physics interpolation (T0057's alpha) needs previous/current pairs, and TAA and
motion blur need per-object motion vectors eventually (T0096 leaves the TAA
hook open). One extra matrix per entity now versus touching every transform
consumer later.

**Keep the skeleton out of this.** T0021/T0049 already decide bones are not
entities — propagation here covers entity parenting (attachments, scene
structure), while ozz owns joint hierarchies in its own SoA buffers. The
composition point is the socket: an entity whose parent transform is sampled
from a joint each frame, feeding normal propagation below it.

Start serial. Parallel subtree propagation (T0026) only if a profile ever says
so — hierarchy sizes in this kind of game rarely justify it.

### Built 2026-08-04

In `engine/include/hp/Scene.hpp` and `engine/src/Scene.cpp` rather than a new
header: the components belong beside the ones T0021 added, and `setParent`
already lived on `Scene`, so splitting would have put the hierarchy invariant in
one file and the transform that depends on it in another.

**Verified:** `zig build test -Dtest=all` green — 73 fast and 56 integration
cases on **both** targets, Linux natively and Windows under wine. Ten new cases.

- **`WorldTransform { current, previous }`, written only by propagation.**
  `previous` is there from day one exactly as the notes argued; an entity that
  did not move reports `previous == current`, which is the right answer for an
  interpolator rather than a case it must special-case.
- **`DirtyTransform` is a tag component**, and the pass carries dirtiness *down*
  as it walks rather than marking a subtree up front — marking would be the same
  work done twice. `propagateTransforms` returns the recomputed count, which is
  what makes "unchanged subtrees cost nothing" a tested fact: a second call on an
  untouched scene returns 0. Without that assertion, a pass that recomputed
  everything every frame would satisfy every other case in the file.
- **101.1's storage question resolved differently from how it was posed.** It
  asked for "local TRS + cached world matrix + parent/child links in the
  transform component". T0021 had already split links into `Hierarchy` for the
  invariant reason, and the world matrix is separate too — it is derived state,
  and keeping it out of `Transform` is what lets `Transform` be serialized and
  diffed without asking whether a cached matrix is stale. The
  runtime-vs-serialized link question (`entt::entity` vs GUID) is untouched and
  belongs to T0022, which owns the load-time fix-up.
- **`Reparent::KeepWorld` inverts the parent's world matrix and re-decomposes.**
  Diligent has no matrix-to-quaternion conversion — `MakeQuaternion` takes four
  components — so that is written out using Shepperd's method. The branch is not
  an optimisation: the single-formula version divides by a near-zero number for
  rotations near 180° and loses most of its precision. Tested against a parent
  that is both scaled and rotated, which is where a naive "subtract the
  positions" reparent gets it wrong.
- **101.6 deletion semantics were already decided by T0021** and are unchanged:
  destroying a parent destroys its descendants. Orphans holding a parent link
  into a reused slot do not fail — they silently reparent to whatever is created
  next.
- **A clone always starts dirty.** `DirtyTransform` is an empty type and entt
  does not store those, so there is no `try_get` and the generic clone operation
  does not compile for it. Marking every cloned entity dirty instead is simpler
  *and* strictly safer: a clone of a stale scene cannot silently inherit
  staleness.

### Not done — and where the remainder went

- **101.4, frame placement: the pass exists, nothing calls it.** D17 puts
  propagation at phases 7 and 9, and that is documented on
  `propagateTransforms`, but `Application` owns no `Scene` — so there is nothing
  in the frame loop to propagate. This is not a design gap in this ticket; it is
  that scene *ownership* has no owner yet. **Recorded on T0077**, which is where
  a scene starts being loaded and held. The substance of the Done-when is met —
  it is one pass, parents before children, and world transforms are a lookup
  rather than a per-query parent-chain walk — but the wiring is not, and saying
  otherwise would be the overstatement this project's documents exist to avoid.
- **Sockets composing with animated joints: moved to T0049.** There is no
  animation runtime, so there is no joint to parent to. T0049 now carries it.
- **`Entity::get<Transform>()` still hands out a mutable reference**, and a write
  through it is invisible to propagation until something marks the entity dirty.
  `setLocalTransform` is the safe path and `markTransformDirty` is the escape
  hatch, both documented in the header — but this is a real sharp edge, not a
  solved problem. Closing it properly means routing component mutation through
  `entt::registry::patch` so `on_update` fires, which changes the shape of the
  whole component API and belongs to a ticket that can weigh that.
- **Propagation is serial and recursive.** The notes say start serial, and it
  does. Recursion depth equals hierarchy depth; a 64-deep chain is tested, but a
  pathological scene could overflow the stack. Not addressed, and worth an
  iterative rewrite if hierarchies ever get deep.
