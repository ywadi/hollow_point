# T0159 — Open the material contract: DiligentFX exposed, state across hooks, and the self-shadowing that proves it

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 465 |
| **Created** | 2026-08-06 |
| **Blocked by** | nothing |
| **Refs** | **D27 — amended by this ticket** ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)); [../../documentation/13-shader-capability-matrix.md](../../documentation/13-shader-capability-matrix.md) — the audit this came from, and the thing to update when a capability lands; [0160-material-declared-parameters.md](0160-material-declared-parameters.md) — the other half of "a game can do anything"; [../inprogress/0158-parallax-depth-cues.md](../inprogress/0158-parallax-depth-cues.md) — **158.2 unblocks here**, and its recorded "cannot keep state" finding is corrected by this ticket; [../completed/0157-rock-cube-sample.md](../completed/0157-rock-cube-sample.md) — the sample that found all of it; [0145-lighting-stage-own-the-light-loop.md](0145-lighting-stage-own-the-light-loop.md) — **must land after this**, see below; [0146-vertex-stage-hook.md](0146-vertex-stage-hook.md), [0147-engine-intermediates-for-shaders.md](0147-engine-intermediates-for-shaders.md), [0153-surface-detiling.md](0153-surface-detiling.md) — the next consumers; **D12**, **D24**, **D26**, **D28**, **D34** |

## Why

**A game shader could not implement parallax self-shadowing** — a technique from 2006 — and the failure was silent: it compiled, rendered, and produced a zero shadow term while reading as correct in the source. T0157's sample found it by being looked at. T0158 measured it. This closes it.

Two things blocked it, and neither is a limitation of Slang, Diligent or the technique:

1. **A material cannot keep state between hooks.** `IHpMaterial`'s requirements are not declared `[mutating]` and `evaluateSurface` takes the material by value, so a tangent frame computed in `surfaceCoordinates` cannot be read in `surface()`. By then `In.UV0` is the *displaced* coordinate whose screen-space derivatives are discontinuous, so the frame cannot be rebuilt either — measured, the rebuilt frame gives the shadow ray **0.006 UV of lateral reach against the 0.14 the geometry needs**.
2. **A material cannot reach a light**, because the only source is `g_Frame.Lights[]` and **D27** forbade depending on DiligentFX.

## The decision, made by the owner 2026-08-06: D27 is amended

**A game's shader may include DiligentFX and reach engine internals. No warning, no version stamp, no refusal.**

The reasoning, recorded because it overturns a binding entry:

- **This engine is permanently on Diligent.** D24 as amended by T0143 commits to feature parity with DiligentFX's PBR; D29 removed the OpenGL backend. Insulating against a renderer swap is insurance against a fire that cannot start.
- **A shipped game never meets a newer engine.** D12's lockstep ships them together. So a DiligentFX rename is *development-time* friction for this studio on its own timetable — not breakage in a shipped title. D27's language reads as though it protects third-party games; T0109 says there are none.
- **Breakage is already loud.** A shader that fails to compile renders magenta and logs one compiler error (T0141.4). Measured this session, including by being mistaken for a working render, which is a fair test of how visible it is.
- **The rule was never enforced.** `samples/rockcube/content/shaders/rock_pom.slang` already reaches `g_HeightMap`, `g_Material.Basic.CustomData` and the permutation macros, and compiles, because the module is textually inside `HpSurface.slang`. D27 has been costing reasoning time while blocking nothing.
- **The residual risk is one we already carry.** A compile error catches a rename; it does not catch *semantic* drift — an upstream function keeping its signature and changing meaning. But the engine itself calls `GetBaseColor`, `PerturbNormal` and `ApplyPunctualLight`, so that risk exists today. Widening it to game shaders enlarges the surface without adding a class. The mitigation is what it already is: pinned submodule SHAs, byte-identical baselines, and the gpu suite.

**What is not amended:** D27's *positive* half stands. The engine still owns `main`, still includes DiligentFX so a game does not have to, and `HpSurfaceInput` is still the vocabulary that should be good enough that nobody needs the escape. Reaching for an internal is a signal that a contract widening is owed — see the capability matrix.

## Done when

- [ ] **`samples/rockcube` renders parallax self-shadowing**, written entirely in the game's `.slang`, with a measured frame difference against the same material with it off
- [ ] A material can **keep per-fragment state between hooks**, and a material that keeps none is byte-identical to today
- [ ] A game shader may **include DiligentFX**, and the documents say so where a shader author will read them
- [ ] **`Time` reaches shaders** and is non-zero
- [ ] **`Tangent` is the mesh's tangent** when the mesh has one, not an unconditional zero
- [ ] Every claim this ticket falsifies is **corrected where it is written**, not only here

## Subtasks

- [ ] 159.1 **Amend D27** in the decision log, with the reasoning above and the date. The entry keeps its original argument as history — it was not wrong when written, and a reader must be able to see why it changed
- [ ] 159.2 **`[mutating]` on `IHpMaterial`'s hooks**, and the material passed mutably through `evaluateSurface`. **Measured backward-compatible**: Slang lets a *non-mutating* override satisfy a `[mutating]` requirement, so no existing module changes. Verify that claim on the pinned `slangc 2026.14.1` before relying on it
- [ ] 159.3 **Publish the undisplaced UV** (`HpSurfaceInput::UV0Base` or similar). `evaluateSurface` copies `VSOut.UV0` in and then overwrites it from the hook's return; one preserved field closes the derivative problem for stateless materials too
- [ ] 159.4 **Stop hardcoding `Tangent` to zero.** `RenderPBR.vsh` writes a world-space tangent under `USE_VERTEX_TANGENTS` and `drawModel` sets that flag when the mesh carries them, but `HpSurface.slang` assigns `float4(0,0,0,1)` unconditionally. Note their wire format is `float3` — glTF's handedness `w` is dropped by their vertex path, so decide what `w` means here and document it
- [ ] 159.5 **Write `Time`.** `PBRFrameAttribs` has the field, `hp::Time` has the clock, and `SceneRenderer` never connects them. One line, and it unblocks scrolling, flowmaps and pulsing emissive
- [ ] 159.6 **Value-initialise the `Camera` block.** It is not, in the DISCARD-mapped buffer, so `uiFrameIndex`, `f2Jitter`, the scene bounds and the DoF/exposure fields hold **undefined memory**. Nothing compiled reads them today — latent, not active — and it should be fixed before something does
- [ ] 159.7 **Self-shadowing in `rock_pom.slang`**, as the acceptance test rather than a feature: the march is already written and correct; it needed a frame and a light. A gpu case measuring the frame difference, and a **magenta guard** so a failed shader can never again pass as a working render
- [ ] 159.8 **Correct every false claim this exposes** — see the list in Notes
- [ ] 159.9 **Update the capability matrix** ([13-shader-capability-matrix.md](../../documentation/13-shader-capability-matrix.md)) for the cells this lands

## Not in scope

- **Game-declared parameters and textures** — T0160, and the larger unlock of the two.
- **The vertex stage** — T0146. Opening DiligentFX changes nothing there: we use their vertex shader as-is and there is no hook to open.
- **Screen resources** — T0147. A shader cannot sample a depth buffer that is not bound to the pipeline, whatever it may include.
- **The light loop** — T0145. This ticket lets a game *read* lights; replacing the loop is still that ticket's.

## Notes / findings

### The false claims this ticket must correct

Each is written down somewhere a person would trust it. An overstated document is worse than an open question, so these are corrections, not additions:

| Where | Says | Actually |
|---|---|---|
| `HpMaterial.slang:78` | `Time` "needs a frame-wide clock field, which `PBRFrameAttribs` has no room for yet" | The field exists — `float Time`, `PBR_Structures.fxh:141`. The engine never writes it |
| `HpMaterial.slang:146` | `Tangent` is "zero when the mesh has no tangents" | It is zero **always** — `HpSurface.slang:742` assigns it unconditionally |
| `hp/Assets.hpp:344` | an edited shader is "picked up by the next pipeline build" | True only of builds that never happen — see below |
| T0158's findings | a material "cannot keep state between hooks" | Our interface does not declare it; **Slang supports it**, and the change is backward-compatible |

### Shader hot reload does not work, and that is a separate ticket

Traced, not executed. Four links, three broken: no file watcher exists; `ensureMaterialBinding` rebuilds only when the `Material` **object identity** changes, so an edited shader behind an unchanged material is invisible; `SurfacePipeline`'s PSO cache is keyed on the module's **path string, not its content**, and the map is never cleared; and a failed compile is cached as null per key, so edit-fail-fix leaves the checkerboard until the process restarts.

That is T0058's or its own ticket's — recorded here because the `Assets.hpp` claim above is the thing that made it look solved.

### Why this must land before T0145

T0145 freezes the material/lighting interface. If the hooks are not `[mutating]` by then, its per-light method cannot read anything the surface stage cached — and self-shadowing inside the light loop is precisely the case that wants to. Cheap now, a second interface break later.
