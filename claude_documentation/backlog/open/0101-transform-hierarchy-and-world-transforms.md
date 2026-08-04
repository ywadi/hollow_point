# T0101 — Transform hierarchy propagation and world transforms

| | |
|---|---|
| **Status** | 🔜 TODO |
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

- [ ] One API yields any entity's world transform, correct in the same frame
      after any local edit, reparent, or hierarchy change
- [ ] Propagation is a single pass at defined points in the frame (T0100), not
      per-query parent-chain walks
- [ ] Reparenting semantics are explicit: keep-world (editor drag in T0035) and
      keep-local both exist as deliberate operations
- [ ] Dirty tracking means unchanged subtrees cost nothing
- [ ] Previous-frame world transforms are retained, so T0057's interpolation
      alpha and (later) motion vectors have something to interpolate between
- [ ] Sockets compose: an entity parented to an animated joint (T0049) gets a
      correct world transform, including its own children
- [ ] Tests: deep chains, reparent-then-read in one frame, delete a parent with
      children, serialization round-trip of hierarchy (T0022)

## Subtasks

- [ ] 101.1 Storage: local TRS + cached world matrix + parent/child links in
      the transform component. Decide the runtime link representation
      (`entt::entity`) versus the serialized one (GUID, per T0022) and where
      the fix-up happens on load
- [ ] 101.2 Dirty flagging and a topologically-ordered propagation pass
- [ ] 101.3 Reparent API with keep-world and keep-local modes
- [ ] 101.4 Placement in the frame (T0100): after behaviours and animation,
      before culling and submission; decide how physics writes interact
- [ ] 101.5 Previous-frame transform storage for interpolation (T0057/T0051)
      and the TAA/motion-vector hook T0096 leaves open
- [ ] 101.6 Deletion semantics: deleting a parent — children die too, or are
      reparented? Decide, and match the editor's behaviour (T0035/T0065)
- [ ] 101.7 Tests

## Notes / findings


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
