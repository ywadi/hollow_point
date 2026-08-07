# T0166 — Tangent frames, the conventions underneath them, and the first real asset this engine has ever rendered

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 462 |
| **Created** | 2026-08-08 |
| **Blocked by** | nothing |
| **Refs** | [../completed/0165-right-handed-engine.md](../completed/0165-right-handed-engine.md) — left `HpTangentFrame` as the capability matrix's unowned row and `fHandness` in a stated disagreement; **this ticket owns both**; [../completed/0152-winding-convention.md](../completed/0152-winding-convention.md) — the convention this is measured against; [../completed/0157-rock-cube-sample.md](../completed/0157-rock-cube-sample.md) — lost a session to a tangent basis nobody could see was wrong; [../completed/0159-open-the-material-contract.md](../completed/0159-open-the-material-contract.md) 159.4 — gave `Tangent` real data and recorded that `w` does not survive the loader; [../inprogress/0158-parallax-depth-cues.md](../inprogress/0158-parallax-depth-cues.md) — the self-shadow is the only directional consumer, and the thing a wrong fix breaks first; [0167-sketchfab-asset-validation.md](0167-sketchfab-asset-validation.md) — **runs after this**, and will find what a controlled case cannot; [0162-shader-authoring-docs.md](0162-shader-authoring-docs.md) — must document whatever `Tangent.w` ends up meaning; [../../documentation/13-shader-capability-matrix.md](../../documentation/13-shader-capability-matrix.md); **D26**, **D33** (as amended), **D35** |

## Why

**Every mesh this engine has ever rendered was written by the same people who wrote the code that consumes it.** `make_cube_gltf.py` generates the sample cube; the gpu suite hand-winds its quads. That is not a test of the import path — it is the import path agreeing with itself. Every shared assumption between authoring and rendering is invisible by construction, and the class of bug it hides is the one that has cost this project the most: T0157 lost a session to a tangent basis that looked fine, and T0152 spent a ticket on assets that contradicted their own normals.

One instance of that class is already found, by reading, and it is concrete enough to fix without any asset at all. The rest of this ticket is about the instances nobody has found yet.

## The defect that is already diagnosed

`HpSurface.slang`'s `HpParallaxUv` builds Schüler's cotangent frame and normalises it with

```hlsl
float invMax = rsqrt(max(max(dot(T, T), dot(B, B)), 1e-12));
```

Work the construction through. With `det = duv1.x·duv2.y − duv1.y·duv2.x`, substituting `dp1 = T·duv1.x + B·duv1.y` and `dp2 = T·duv2.x + B·duv2.y` into the perp trick gives

```
T_computed = dp2perp·duv1.x + dp1perp·duv2.x = det · T
```

so the true tangent is `T_computed / det`. Dividing by `invMax` — **always positive** — yields `sign(det) · T`. **When `det` is negative the function returns −T and −B**: a 180° rotation of the frame about the normal.

**`det` is negative exactly when the UV parameterisation is mirrored**, which is not an edge case but standard game art — symmetric characters and props with the left and right shells overlapped to double texel density, which is what Blender's UV Mirror produces. On those shells `viewTS.xy` negates, the march runs the opposite way, and **relief reads inside-out on half a symmetric model while the other half looks correct**. That is the worst available failure signature: it presents as an asset bug, and someone will re-bake normals for a day.

**And it disagrees with the engine's own normal mapping.** DiligentFX's `TransformTangentSpaceNormalGrad` (`ShaderUtilities.fxh:51–55`) divides by the **signed** determinant and guards `d != 0.0`, so a mirrored shell is oriented correctly there. On the same pixel, lighting and parallax then disagree about which way the bump faces.

`samples/rockcube/content/shaders/rock_pom.slang:320` carries the same `invMax`, so the sample a game author copies inherits it.

**Nothing in CI can catch this**, because every test asset has consistent UV winding.

## The conventions underneath, none of which has ever been checked

- **`Tangent.w` is a constant pretending to be data.** glTF ships the bitangent handedness sign in `TANGENT.w` — the format's own answer to the mirrored-shell problem — and `HpSurface.slang:1676` records that Diligent's vertex path is `float3` (`VSTangentAttrib` is `VT_FLOAT32, 3`), so `w` never survives the loader and is hardcoded `+1`. A game shader building its own TBN from the contract is being handed a lie.
- **Normal-map green channel: OpenGL `+Y` or DirectX `−Y`.** Completely untested here. It inverts lighting in a way that reads as an authoring mistake, and it is the same family of question as D33's handedness.
- **`fHandness` is in a stated disagreement with Diligent's own comment** (T0165). Nothing reaches the branch that reads it because the importer refuses a mesh without `NORMAL`; the trigger is the first mesh that has none.

## Done when

- [ ] A mirrored UV shell and a non-mirrored one, **same texture, same lighting**, agree — proven by a gpu case, not by reasoning about the algebra
- [ ] Parallax and normal mapping agree about the tangent frame on **both** shells
- [ ] Whatever `Tangent.w` means is **true**, or the contract says plainly that it is not data and what a shader should do instead
- [ ] The engine has rendered at least one asset **nobody in this project authored**, and what was found is written down — including "nothing"
- [ ] `13-shader-capability-matrix.md`'s `HpTangentFrame` row has an owner and a status

## Subtasks

- [ ] 166.1 **Scan every finding first, before touching anything.** T0165's `## Notes / findings` and its "not verified" list, the two convention audits dispatched 2026-08-07 (camera/+Z assumptions, and winding/tangent/normals — their reports land in the session transcript, not on disk), the capability matrix's unowned rows, and D33 as amended. **This subtask exists because the diagnosis above was found by reading and the rest probably can be too** — cheaper than a render, and it decides what the later subtasks are actually for
- [ ] 166.2 **Fix the determinant sign**, in `HpSurface.slang` and `rock_pom.slang`, matching Diligent's shape: divide by the signed `det` with a `!= 0.0` guard. **Small, and worthless without 166.3**
- [ ] 166.3 **The controlled case**: one asset, two shells, one mirrored, generated the way `make_cube_gltf.py` generates the cube so it is reviewable as source. Assert parallax shifts the **same** direction on both, and that the normal-mapped lighting matches. This is the subtask that matters; 166.2 without it just moves the guess
- [ ] 166.4 **Decide `Tangent.w`.** Either widen the vertex path so glTF's sign survives, or state in the contract that `w` is not data and give a shader the derivative-based route instead. **A field that always reads `+1` is worse than an absent one**, because a shader will trust it
- [ ] 166.5 **Pin the normal-map green channel** with a case that would fail if the convention flipped, and write the answer where an artist will find it — `11-material-format.md`, not a comment
- [ ] 166.6 **Vendor conformance assets, or decide not to.** Khronos's `glTF-Sample-Assets` contains `NormalTangentMirrorTest` and `NormalTangentTest` — built for exactly the question 166.3 asks, by the format's own authors. The full set is ~2 GB, so this is a build-harness call (shallow sparse checkout, a small curated copy, or nothing) and it belongs to `03-build-harness.md`'s constraints. **Record the rejection if it is one**
- [ ] 166.7 **Render something nobody here authored.** `third_party/meshoptimizer/demo/pirate.glb` is genuine DCC output already in the tree, needs no network fetch and no submodule decision, and is enough to answer "does a real model come out the right way round"
- [ ] 166.8 **Take the `HpTangentFrame` row**, and settle or re-record `fHandness` with its trigger

## Not in scope

- **A Sketchfab model, or any asset chosen for how it looks.** [T0167](0167-sketchfab-asset-validation.md) owns that, deliberately second: a production model has several things wrong at once, so it says *something* is wrong rather than *which*. Land a clean signal here first — reversed, the first real asset renders wrong for two reasons and the wrong one gets debugged. That is the trap that cost T0157 a day.
- **Per-vertex tangent generation** (MikkTSpace or otherwise) for meshes that ship without `TANGENT`. Real, larger, and only worth deciding once 166.4 has settled what the engine does with the tangents it is *given*.
- **Re-tuning parallax.** T0158 owns the look; this ticket owns whether the frame it marches in is correct.

## Notes / findings

### The reverted sign correction was right to revert, and here is the argument rather than the measurement

T0165 wrote a `sign(dot(cross(ddx, ddy), N))` compensation, measured it breaking the rock cube's self-shadow (max drop 124.6 → exactly 0), and reverted it. The reasoning that makes that more than an empirical result:

`dot(cross(ddx(P), ddy(P)), N)` is a **pseudo-scalar** — a cross product dotted with a vector — so under a mirror of the world it flips sign *by definition*. T0165 mirrored the content; the flip is the signature of a correct mirror, not evidence of a defect. Multiplying the frame by its sign injects a chirality dependence into a construction that has none.

The empirical half agrees and is worth keeping: the self-shadow's max drop was **124.6 before and after** the handedness change. Had the frame silently mirrored, that directional quantity would have moved.

**So the defect this ticket fixes is not a T0165 regression.** It predates the handedness work and would be there either way. Anyone tempted to reintroduce the `sign()` factor should read this section first — the argument for it is convincing and wrong.

### Why the fix is not simply "copy Diligent"

Their function returns a **world-space normal**; ours returns a **UV offset** after marching. The shared part is the frame's construction and the signed division; the consumers differ. Copying the whole function would not compile against `HpParallaxMarch`'s needs, and the mirror-discipline rule (T0145) applies if any of it is pinned.
