# T0049 — Animation runtime library

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Very Complex |
| **Phase** | 7 — Content pipeline |
| **Order** | 750 |
| **Created** | 2026-08-03 |
| **Refs** | [../completed/0168-asset-import-coverage.md](../completed/0168-asset-import-coverage.md) — the import matrix (`14-asset-import-matrix.md`) assigns this ticket the `JOINTS_0`/`WEIGHTS_0` row: skins and animations are **fully loaded and upstream-evaluatable already** — `GLTF::Model::ComputeTransforms(scene, transforms, root, AnimationIndex, Time)` updates joint palettes today, the engine just always passes `-1` — so this runtime starts from a running mechanism, not from raw data. A skinned mesh currently renders its bind pose, which is the intended degradation until then. See T0041's Refs for the draw-path flags the first render needs |

## Why

**Skeletal animation is core to the games this engine is for**, and animation
logic will be written in C++ attached to entities. That only stays pleasant if
the utilities underneath are good — otherwise every character re-implements
blending, transitions and attachment by hand.

So: a proper animation library, built on ozz's job types, exposing the pieces
character code actually needs. No node-graph editor, no scripting layer — a
well-designed C++ API.

## Done when

- [ ] An animation component drives a skinned character from C++
- [ ] Clip playback with speed, looping and time control
- [ ] Cross-fade between clips over a duration
- [ ] **Layered animation** — upper/lower body via per-joint masks
- [ ] **IK** — foot placement and aim
- [ ] **Root motion** extracted and applied to the entity transform
- [ ] **Sockets** — attach entities to named joints (weapons, props)
- [ ] **Animation events** fire at authored times (footsteps, hit frames)
- [ ] Sampling parallelised across characters (T0026)
- [ ] A state-machine helper exists so transitions are not hand-rolled per character

## Subtasks

- [ ] 49.1 Animation component: skeleton + clip references by GUID, playback state
- [ ] 49.2 Sampling via `ozz::animation::SamplingJob`, with per-instance contexts
- [ ] 49.3 Cross-fade via `BlendingJob`
- [ ] 49.4 Layer masks via `BlendingJob`'s per-joint weights
- [ ] 49.5 `LocalToModelJob` → skinning matrices for the renderer
- [ ] 49.6 Sockets using `skeleton_utils.h` joint lookup
- [ ] 49.7 Root motion via `motion_extractor` (offline) + `motion_blending_job`
- [ ] 49.8 Events via `track_triggering_job`
- [ ] 49.9 IK via `ik_two_bone_job` (feet) and `ik_aim_job` (head/weapon)
- [ ] 49.10 A lightweight C++ state machine helper — states, conditions, transitions

## Notes / findings

**ozz provides all the primitives**, which is why this is a library-assembly task
rather than an animation-system-from-scratch task:

| Need | ozz |
|---|---|
| Sample a clip | `sampling_job.h` |
| Blend / cross-fade / layer masks | `blending_job.h` |
| Skinning matrices | `local_to_model_job.h` |
| Foot IK / aim IK | `ik_two_bone_job.h`, `ik_aim_job.h` |
| Animation events | `track_triggering_job.h` |
| Root motion | `motion_extractor.h`, `motion_blending_job.h` |
| Joint lookup for sockets | `skeleton_utils.h` |

**Bones are not entities** — see T0021. A skeleton's joints live in ozz's compact
SoA pose buffers; only *attachment points* are entities. Making every bone an
entity would swamp the registry and is a costly mistake to unwind.

`SamplingJob` needs a per-instance context that must persist between frames —
allocate it with the component, not per frame, or sampling silently loses its
optimisation and gets much slower.

The state machine (49.10) is deliberately a **C++ helper**, not an authored graph.
If hand-writing transitions becomes painful at scale, that is the evidence for
revisiting a data-driven authoring tool — not a reason to build one up front.

### Cross-ticket obligation — T0101 (2026-08-04)

**Sockets are yours: an entity parented to an animated joint.** T0101 built
entity-transform propagation and closed on everything except this, because there
is no animation runtime to parent to.

The composition point is precise, and T0021/T0049 already decided the half that
constrains it: **bones are not entities.** A skeleton with 80 joints per
character would swamp the registry, and ozz keeps its own compact SoA pose
buffers that the renderer consumes directly. So a socket is an entity whose
*local* transform is written each frame from a sampled joint, after which normal
`Scene::propagateTransforms` carries it down to that entity's own children —
weapons in hands, props on sockets, cameras on rigs.

Two things that follow: the joint sample must land **before** phase 7 so
propagation sees it in the same frame, and the write should go through
`Scene::setLocalTransform` (or be followed by `markTransformDirty`), because a
direct write through `get<Transform>()` is invisible to the dirty tracking.
