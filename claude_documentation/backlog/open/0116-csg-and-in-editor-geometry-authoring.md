# T0116 — CSG / in-editor geometry authoring

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 6 — Editor |
| **Order** | 655 |
| **Created** | 2026-08-03 |
| **Refs** | T0021, T0023, T0033, T0038, T0043, T0051, T0061, T0064, T0065, T0101 |

## Why

The project owner asked for this directly: *"CSG support, Adding Mesh and resizing it and so on like Godot allows (this comes in handy down the road to also make the collision shapes for example in editor)."* That is Godot's `CSGBox3D`/`CSGCombiner3D` model — primitive solids placed and scaled in the editor, combined with union/subtraction/intersection, used for level blockout before art exists, and — the owner's own second use — a way to author collision shapes without a modelling tool.

**Verified absent.** `csg`, `constructive solid`, `blockout`, `whitebox`/`white-box`, `\bbrush\b` return **zero hits** across all 94 open tickets, 21 completed tickets and every documentation file. `boolean` hits only D11's doctest tag discussion — an unrelated sense.

`greybox`/`greyboxing` is **not** a clean zero, and the one hit matters enough to be precise about. T0023's second architecture-review note proposes "primitive meshes for greyboxing (cube/sphere/plane — scene authoring needs them before any real asset is imported)" as engine-provided builtin assets with reserved GUIDs. That is easy to mistake for this gap already being covered. It is not: T0023's greybox primitives are fixed unit meshes, placed and scaled through the ordinary entity transform. They give a level designer a cube to drop into a scene; they do not give a *resizable-by-dimension* box (Godot's `CSGBox3D.size` is a different authoring ergonomic than a transform's non-uniform scale, and for collision a different correctness — non-uniform scale on a convex-hull collider can produce a non-convex result), and they have no boolean combine at all. T0023's primitives are a related, narrower, already-scoped piece of infrastructure this ticket should build on, not a substitute for it.

Two distinct payoffs, worth separating because they pull toward different designs:

- **Blockout geometry.** Level design is otherwise gated on a modelling pipeline that does not exist yet — T0038 converts FBX to glTF, but nothing produces a first mesh to convert. Without in-editor primitive authoring, "design a level" cannot start until an artist has produced real art. This is the standard grey-box workflow every 3D editor (Godot, Unity, Unreal, Source's brush-based levels before it) provides for exactly this reason.
- **Collision-shape authoring.** Arguably the more durable payoff, because it does not go away once real art exists. `collision shape`/`collider` currently appear only in T0051 (Jolt physics: "Collision shape generation at import" — from imported meshes only) and T0061 (debug draw: rendering shapes that already exist). **Nothing lets anyone author a collision shape.** A designer who wants an invisible blocking volume, or a collision proxy simpler than the render mesh, has no in-editor path to make one. T0051's own subtask list only generates shapes *at import*; this ticket is the missing *authoring* side.

**The boolean-evaluation question is genuinely hard and must not be silently assumed.** Robust CSG — real union/subtract/intersect on arbitrary meshes — has well-known failure modes: coplanar-face ambiguity, numerical-tolerance slivers, and non-manifold results that break downstream consumers (collision cooking, LOD generation). Three shapes this could legitimately take, with different costs:

| Option | Buys | Costs |
|---|---|---|
| **Real CSG via a library** | The full Godot-like experience: union/subtract/intersect on arbitrary combinations, reusable for render and collision geometry alike | A new dependency with its own robustness record to vet under this project's cross-compile matrix (D1/D3); real CSG libraries are exactly the kind of thing that is "genuinely hard, with a real chance of needing a different approach" |
| **Primitive placement without true booleans** | Solves blockout entirely (a room is boxes, not one boolean-carved solid) and most collision authoring (a collision volume is usually a single primitive), with none of the robustness risk | No carved openings (a doorway cut from a wall), no organic blockout shapes |
| **Collision-primitive authoring only, no render geometry** | The owner's second use case, cheaply — a resizable box/sphere/capsule collider component, independent of what renders | Does not touch blockout at all |

This ticket does not pick. It records the trade-off so whoever decides is choosing with the costs in front of them, not discovering them mid-implementation.

**Interacts with the asset pipeline (T0023, T0043).** Is CSG output a *source* asset edited in place (like a scene), a *derived/cooked* asset baked at export the way T0043 already cooks scenes to binary, or evaluated fresh at load every time? Each answer changes T0023's import model and T0043's export walk differently, and neither ticket currently has an opinion because neither knew this existed.

**Interacts with the transform hierarchy (T0101).** CSG nodes nest — a combiner is a parent whose children are the operands — and T0101 is what makes world-space authoring of a hierarchy correct after a reparent. This ticket should not invent its own propagation.

## Done when

- [ ] At least one primitive solid (box at minimum; sphere and cylinder are the natural Godot-parity set) can be placed in the editor viewport and resized via authored dimensions — a `size`/`radius`-shaped property — not only through the entity's non-uniform transform scale
- [ ] The boolean-evaluation decision above is made and recorded, with the option taken and what it forecloses written down, not implied by what got built
- [ ] Whichever option is chosen, a placed primitive's edits (move, resize, and combine if in scope) update the resulting geometry live in the viewport
- [ ] The asset-pipeline question is answered and recorded — source asset, cooked/derived asset baked at export, or evaluated at load — and T0023 and/or T0043 are updated to match, or a follow-up ticket is filed against them rather than left silently inconsistent
- [ ] CSG nodes are entities that nest and combine correctly under T0101's transform propagation, including after a reparent
- [ ] If collision-shape authoring is in scope under the decision above: a CSG primitive can produce a shape T0051's rigid-body/collision components can reference, without requiring any render mesh to exist
- [ ] Placement, resize and (if in scope) combine operations are undoable through T0065's command system
- [ ] Builds and runs on both targets

## Subtasks

- [ ] 116.1 Decide the boolean-evaluation approach (see the Why table) and record the decision and its rejected alternatives, matching the decision-log's style of recording what was turned down and why
- [ ] 116.2 Primitive shape components — box, sphere, cylinder at minimum — with authored dimensions distinct from, but composing with, the entity transform (T0101)
- [ ] 116.3 Viewport manipulator for resizing by dimension (T0064), rather than only the general-purpose scale gizmo — a box's `size` handle is a different interaction than uniform scale
- [ ] 116.4 If real booleans are in scope: operation nodes (union/subtract/intersect), evaluation timing (recompute live in-editor versus on save/build), and a written answer to the coplanar-face/numerical-tolerance robustness question rather than an assumption it will be fine
- [ ] 116.5 Resolve the asset-pipeline placement against T0023/T0043 — source, cooked, or load-time-evaluated — and land the corresponding change (or follow-up ticket) in whichever of those two this decision touches
- [ ] 116.6 Collision-shape authoring path: a CSG primitive (or a collision-only variant, per 116.1's decision) produces a shape T0051 can reference, so collision authoring does not depend solely on T0051's own "generation at import" subtask
- [ ] 116.7 Undo/redo integration (T0065) for every authoring operation this ticket adds
- [ ] 116.8 Tests: resize updates geometry live; nested CSG entities compose correctly across a reparent (T0101); a collision-only shape (if in scope) requires no render mesh; serialization round-trip through T0022

## Notes / findings

**T0023's greybox primitives are a foundation, not a substitute.** Its reserved-GUID cube/sphere/plane give scene authors *something* to place before any real asset exists, but they are static mesh assets manipulated by the ordinary transform — no dimension-based resize, no boolean combine, no collision authoring. This ticket should reuse T0023's reserved-GUID mechanism for whatever built-in primitives it defines, rather than inventing a second one.

**Do not build this against T0038's absence.** Blockout geometry exists precisely *because* the modelling pipeline (T0038) is not ready; it is not a reason to wait for T0038, and it is not a replacement for it once real art pipelines exist. The two should coexist — a shipped level is free to mix converted glTF meshes and CSG-authored geometry, exactly as Godot allows.

**Nothing in the decision log addresses this directly.** D14 (no scripting language) and D12 (lockstep C++) are the nearest neighbours and neither constrains this — CSG evaluation, whichever option is chosen, is ordinary engine or host-tool code, not a scripting concern. Recording this so the absence of a decision-log citation here reads as a checked "nothing conflicts," not an oversight.
