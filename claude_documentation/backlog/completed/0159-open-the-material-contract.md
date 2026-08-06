# T0159 — Open the material contract: DiligentFX exposed, state across hooks, and the self-shadowing that proves it

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 465 |
| **Created** | 2026-08-06 |
| **Blocked by** | nothing |
| **Refs** | **D27 — amended by this ticket** ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)); [../../documentation/13-shader-capability-matrix.md](../../documentation/13-shader-capability-matrix.md) — the audit this came from, and the thing to update when a capability lands; [0160-material-declared-parameters.md](../inprogress/0160-material-declared-parameters.md) — the other half of "a game can do anything"; [../inprogress/0158-parallax-depth-cues.md](../inprogress/0158-parallax-depth-cues.md) — **158.2 unblocks here**, and its recorded "cannot keep state" finding is corrected by this ticket; [../completed/0157-rock-cube-sample.md](../completed/0157-rock-cube-sample.md) — the sample that found all of it; [0145-lighting-stage-own-the-light-loop.md](../open/0145-lighting-stage-own-the-light-loop.md) — **must land after this**, see below; [0146-vertex-stage-hook.md](../open/0146-vertex-stage-hook.md), [0147-engine-intermediates-for-shaders.md](../open/0147-engine-intermediates-for-shaders.md), [0153-surface-detiling.md](../open/0153-surface-detiling.md) — the next consumers; **D12**, **D24**, **D26**, **D28**, **D34** |

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

- [x] **`samples/rockcube` renders parallax self-shadowing**, written entirely in the game's `.slang`, with a measured frame difference against the same material with it off — 72 pixels darkened >10 luminance, max drop 51.7, asserted on both targets (`rockcube_sample_test.cpp`)
- [x] A material can **keep per-fragment state between hooks**, and a material that keeps none is byte-identical to today — stateful module renders exact (0,255,0) through state; `no-height vs zero-scale: 0` on all three parallax baselines, both targets
- [x] A game shader may **include DiligentFX**, and the documents say so where a shader author will read them — `HpMaterial.slang`'s header prose, regenerated into `docs/shaders/HpMaterial.md`
- [x] **`Time` reaches shaders** and is non-zero — exact (0,255,0) for a module bracketing `In.Time` at the passed 0.5, and exact red for the documented zero default; the editor passes `clock().elapsed()` (code path, not visually measured — no time-driven shader exists in the editor's content yet)
- [x] **`Tangent` is the mesh's tangent** when the mesh has one, not an unconditional zero — exact (255,0,0) from a +X-tangent quad, exact black without tangents, both targets
- [x] Every claim this ticket falsifies is **corrected where it is written**, not only here — including one found during the work: the scene's sun-direction comment (see findings)

## Subtasks

- [x] 159.1 **Amend D27** in the decision log, with the reasoning above and the date. The entry keeps its original argument as history — it was not wrong when written, and a reader must be able to see why it changed
- [x] 159.2 **`[mutating]` on `IHpMaterial`'s hooks**, and the material passed mutably through `evaluateSurface`. **Measured backward-compatible**: Slang lets a *non-mutating* override satisfy a `[mutating]` requirement, so no existing module changes. Verify that claim on the pinned `slangc 2026.14.1` before relying on it
- [x] 159.3 **Publish the undisplaced UV** (`HpSurfaceInput::UV0Base` or similar). `evaluateSurface` copies `VSOut.UV0` in and then overwrites it from the hook's return; one preserved field closes the derivative problem for stateless materials too
- [x] 159.4 **Stop hardcoding `Tangent` to zero.** `RenderPBR.vsh` writes a world-space tangent under `USE_VERTEX_TANGENTS` and `drawModel` sets that flag when the mesh carries them, but `HpSurface.slang` assigns `float4(0,0,0,1)` unconditionally. Note their wire format is `float3` — glTF's handedness `w` is dropped by their vertex path, so decide what `w` means here and document it
- [x] 159.5 **Write `Time`.** `PBRFrameAttribs` has the field, `hp::Time` has the clock, and `SceneRenderer` never connects them. One line, and it unblocks scrolling, flowmaps and pulsing emissive
- [x] 159.6 **Value-initialise the `Camera` block.** It is not, in the DISCARD-mapped buffer, so `uiFrameIndex`, `f2Jitter`, the scene bounds and the DoF/exposure fields hold **undefined memory**. Nothing compiled reads them today — latent, not active — and it should be fixed before something does
- [x] 159.7 **Self-shadowing in `rock_pom.slang`**, as the acceptance test rather than a feature: the march is already written and correct; it needed a frame and a light. A gpu case measuring the frame difference, and a **magenta guard** so a failed shader can never again pass as a working render
- [x] 159.8 **Correct every false claim this exposes** — see the list in Notes
- [x] 159.9 **Update the capability matrix** ([13-shader-capability-matrix.md](../../documentation/13-shader-capability-matrix.md)) for the cells this lands

## Not in scope

- **Game-declared parameters and textures** — T0160, and the larger unlock of the two.
- **The vertex stage** — T0146. Opening DiligentFX changes nothing there: we use their vertex shader as-is and there is no hook to open.
- **Screen resources** — T0147. A shader cannot sample a depth buffer that is not bound to the pipeline, whatever it may include.
- **The light loop** — T0145. This ticket lets a game *read* lights; replacing the loop is still that ticket's.

## Notes / findings

### The false claims this ticket must correct — ALL CORRECTED 2026-08-06

Each was written down somewhere a person would trust it. An overstated document is worse than an open question, so these were corrections, not additions:

| Where | Said | Actually | Fixed |
|---|---|---|---|
| `HpMaterial.slang:78` | `Time` "needs a frame-wide clock field, which `PBRFrameAttribs` has no room for yet" | The field exists — `float Time`, `PBR_Structures.fxh:141`. The engine never wrote it | Written now (159.5); the contract row replaced by the real `HpSurfaceInput::Time` |
| `HpMaterial.slang:146` | `Tangent` is "zero when the mesh has no tangents" | It was zero **always** | Real now (159.4); doc states the `w = +1` decision |
| `hp/Assets.hpp:344` | an edited shader is "picked up by the next pipeline build" | True only of builds that never happen | Corrected by the audit commit (hot reload traced as broken) |
| T0158's findings | a material "cannot keep state between hooks" | Our interface did not declare it; **Slang supports it** | 158.2 ticked with the landing numbers |
| `rockcube.hpscene`, found during this work | the sun is `Rot(Y, pi-0.7)*Rot(X,-0.7)` travelling `(-0.49,-0.64,0.59)` | the shipped quaternion produces `(0.15,-0.62,0.77)` — the comment's own N·L figures match the *actual* light | Comment corrected; the reviewed look is untouched |

### Shader hot reload does not work, and that is a separate ticket

Traced, not executed. Four links, three broken: no file watcher exists; `ensureMaterialBinding` rebuilds only when the `Material` **object identity** changes, so an edited shader behind an unchanged material is invisible; `SurfacePipeline`'s PSO cache is keyed on the module's **path string, not its content**, and the map is never cleared; and a failed compile is cached as null per key, so edit-fail-fix leaves the checkerboard until the process restarts.

That is T0058's or its own ticket's — recorded here because the `Assets.hpp` claim above is the thing that made it look solved.

### Findings from the implementation, 2026-08-06

**159.2, measured on the pinned `slangc 2026.14.1` before relying on it.** All
four claims verified by CLI probes: a non-mutating `override` satisfies a
`[mutating]` requirement (compiles clean); a default implementation on a
`[mutating]` requirement is legal; state written in one hook is visible in the
next (the emitted GLSL threads one `inout this` through the statically
specialised calls); and `= {}` zero-initialises both stateful and empty
conformances without slang's uninitialized-variable diagnostic. One extra fact
worth keeping: a **by-value** generic parameter also accepts `[mutating]` calls
(Slang parameters are mutable copies), so `inout` on `evaluateSurface` is the
honest signature rather than the enabling one — the copy would have carried
state across hooks within one evaluation anyway.

**The engine constructs the material `= {}`**, so a stateful module's members
start at zero rather than undefined, and the compile log stays clean of
uninitialized-variable warnings a stateful module would otherwise produce per
pipeline build. Verified on device: an unwritten member read in `surface()`
renders exactly zero (`custom_shader_material_test`).

**Light order among directional lights is unspecified, and a shader must not
index `g_Frame.Lights[]` by position.** `selectLightsFor` ranks by squared
distance; every directional light carries 0.0, and `std::sort` is unstable, so
which directional lands at index 0 is arbitrary — measured: the *fill* arrived
first on this machine, and the sample's first march aimed at a light lying in
the front face's plane, producing a 0.02-luminance shadow term. The sample now
picks its light by `IntensityRGB` luminance. **T0145 should decide the real
contract here** (a documented ordering, or a dominant-light convention) — noted
on that ticket.

**The scene's sun-direction comment was false, and the falsification method is
the finding.** `rockcube.hpscene` claimed the sun quaternion was
`Rot(Y, pi-0.7)*Rot(X,-0.7)` travelling `(-0.49, -0.64, 0.59)`; solving for the
light the shader actually received (from the rendered `lightTS.z` of two known
faces) gave `(0.15, -0.62, 0.77)`, and recomputing the quaternion confirmed it:
the composition in the comment produces a *different* quaternion from the one
shipped. The scene renders the reviewed, approved look — its own measured N·L
figures match the actual light — so the comment was corrected, not the data.
Consequence worth knowing: **the key is nearly frontal to the camera**, and
frontal light casts shadows away from the viewer, so parallax self-shadowing
reads small from this camera except where the spin turns a face until the key
grazes it.

**The self-shadow march needed three guards, all found by watching the spinning
cube live** (owner's report, confirmed by measurement):

- a **reach clamp** (`kMaxReachRatio`): `lightTS.xy / lightTS.z` diverges as
  1/z toward the horizon — the shadows grew hyperbolically and swallowed the
  face;
- a **horizon fade** (`smoothstep` on `lightTS.z`): below the old hard
  `1e-4` cutoff the whole term vanished in one frame — 158.5's terminator pop,
  observed as "suddenly disappear";
- a **light-share cap**: multiplying `BaseColor` darkens *every* light's
  contribution, but only one light was marched — at full strength that blacked
  out faces the fill still reaches. The bite is now capped by the marched
  light's share of incident light (N·L·intensity products), which is the
  sample-level approximation of the per-light decision T0145/158.3 owns.

**The black pixel speckle on the cube is NOT the shadow march**, although it
was reported against it. Measured with the march forced off through a
Prepend-mounted patched shader: the count of near-black pixels is
**bit-identical with the march on and off** — 431/431 at the test's yaw, up to
10000/10000 at yaw 0.9 where it peaks. It is the documented N·L clamp: a
normal-mapped texel tips past every light's horizon and clamps to pure black
with no ambient to catch it — the same mechanism the scene file already
documents (its `normalScale: 0.8` note), aggravated at grazing view because POM
relocates which texels are seen. **T0087 (environment lighting) is the owning
ticket**; recorded there and here so the next person does not re-debug the
march for it.

**A filter lies in both directions, again.** The first debugging pass rendered
diagnostic values into colour channels and then classified pixels with the
missing-material magenta test — which the diagnostic encoding of
"steeply lit, unshadowed" pixels also matches. Half the cube was misdiagnosed
as losing its per-fragment state for two full debug cycles until a pure state
probe (bool and float members read back with nothing else in the frame)
returned 47393/47393 intact. The magenta guard now in
`rockcube_sample_test.cpp` classifies *rendered* frames only, where nothing
legitimate is loud magenta.

**Measured numbers for the acceptance case** (`rockcube_sample_test.cpp`,
"parallax self-shadowing darkens the frame"): at yaw 0.45, mean luminance
81.7055 with the march against 81.7505 without; mean absolute difference
0.0450; **72 pixels darkened by more than 10 luminance, max drop 51.7**; black
pixels 431/431 (unchanged); magenta share 0 in both frames; coverage 46723 in
both. The assertions bound the darkened population (>30) and the max drop
(>20), not the frame mean — the honest shape of a small-footprint effect under
a frontal key.

### Why this must land before T0145

T0145 freezes the material/lighting interface. If the hooks are not `[mutating]` by then, its per-light method cannot read anything the surface stage cached — and self-shadowing inside the light loop is precisely the case that wants to. Cheap now, a second interface break later.

### Verification, 2026-08-06 — the full bar

- `zig build all` — exit 0, `grep -cE '^FAILED:|error:'` returns 0.
- `zig build test -Dtest=all` — 314 fast cases and both per-target suites
  (92 each, the Windows one under wine), all passed.
- `zig build test -Dtest=gpu` — **36 cases, 958 assertions, both targets**
  (Linux native and Windows under wine on the same GPU), all passed. The
  suite includes the new cases this ticket added:
  - *state*: a module writing a member in `surfaceCoordinates` and reading it
    in `surface()` renders exact **(0, 255, 0)**, with an unwritten member in
    the blue channel proving zero-initialisation;
  - *tangent*: exact **(255, 0, 0)** from a +X-tangent quad, exact black from
    the same quad without tangents;
  - *time*: exact **(0, 255, 0)** for `In.Time` bracketing the passed 0.5,
    exact red for the zero default;
  - *self-shadowing*: mean 81.7055 vs 81.7505, mean abs difference 0.0450,
    **72 pixels darkened >10 luminance, max drop 51.7406**, black pixels
    431/431 (bit-identical — the march adds none), magenta share 0 both
    frames, coverage 46723 both frames;
  - *magenta guard*: the committed-content case now rejects a frame whose
    covered pixels are >5% checkerboard magenta.
- **Byte-identical discipline held**: `no-height vs zero-scale: 0` on all
  three parallax baselines (`parallax_test`, `triplanar_parallax_test` x2),
  both targets — the standard material and every stateless path render
  exactly as before the `[mutating]`/`inout` change.
- `zig build docs` — clean; `docs/shaders/HpMaterial.md` and `IHpMaterial.md`
  regenerated with the amended D27 prose, `UV0Base`, `Time` and the
  `[mutating]` signatures.

### Not verified, said plainly

- The editor passes `clock().elapsed()` into `SceneView::render` and the gpu
  suite proves that parameter reaches `In.Time` — but no *visual* check of a
  time-driven shader in the live editor was made, because no editor content
  animates on time yet. The first scrolling material will be the live proof.
- `RenderStack`'s `timeSeconds` plumbing (`RenderPassContext::timeSeconds` →
  `SceneRenderLayer`) compiles and is exercised by no test: nothing drives a
  `RenderStack` with a clock today. The parameter defaults to zero, which the
  contract documents as the defined no-time value.
- The `Camera` block's value-initialisation (159.6) is verified by the suite
  rendering identically, not by a test that reads the previously-undefined
  fields — nothing compiled reads them, which is exactly why it was latent.
