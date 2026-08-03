# T0049 — Animation runtime library

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Very Complex |
| **Phase** | 7 — Content pipeline |
| **Order** | 750 |
| **Created** | 2026-08-03 |

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
