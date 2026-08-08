# T0166 — Tangent frames, the conventions underneath them, and the first real asset this engine has ever rendered

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 462 |
| **Created** | 2026-08-08 |
| **Blocked by** | nothing |
| **Refs** | [../completed/0165-right-handed-engine.md](../completed/0165-right-handed-engine.md) — left `HpTangentFrame` as the capability matrix's unowned row and `fHandness` in a stated disagreement; **this ticket owns both**; [../completed/0152-winding-convention.md](../completed/0152-winding-convention.md) — the convention this is measured against; [../completed/0157-rock-cube-sample.md](../completed/0157-rock-cube-sample.md) — lost a session to a tangent basis nobody could see was wrong; [../completed/0159-open-the-material-contract.md](../completed/0159-open-the-material-contract.md) 159.4 — gave `Tangent` real data and recorded that `w` does not survive the loader; [../inprogress/0158-parallax-depth-cues.md](../inprogress/0158-parallax-depth-cues.md) — the self-shadow is the only directional consumer, and the thing a wrong fix breaks first; [0167-sketchfab-asset-validation.md](../open/0167-sketchfab-asset-validation.md) — **runs after this**, and will find what a controlled case cannot; [0162-shader-authoring-docs.md](../open/0162-shader-authoring-docs.md) — must document whatever `Tangent.w` ends up meaning; [../../documentation/13-shader-capability-matrix.md](../../documentation/13-shader-capability-matrix.md); **D26**, **D33** (as amended), **D35** |

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

- [x] A mirrored UV shell and a non-mirrored one, **same texture, same lighting**, agree — proven by a gpu case, not by reasoning about the algebra *(2026-08-08 — `tests/gpu/tangent_frame_test.cpp`; and the case asserts the **absolute** direction too, which is what caught the real defect)*
- [~] Parallax and normal mapping agree about the tangent frame on **both** shells — **not met, and measured not met.** Parallax now uses DiligentFX's own construction, so the two agree about `T` on every chart. They still disagree about the **bitangent** on a mirrored one: DiligentFX takes `b = cross(t, n)`, which is a fixed chirality rather than the chart's. Measured: the same tangent-space normal shades the two shells 0 against 130.2. → **[T0168](0168-asset-import-coverage.md)**
- [x] Whatever `Tangent.w` means is **true**, or the contract says plainly that it is not data and what a shader should do instead *(2026-08-08 — the contract says it is not data, says why with the line that imposes it, and gives the derivative route; widening it is [T0168](0168-asset-import-coverage.md))*
- [~] The engine has rendered at least one asset **nobody in this project authored** — **not done.** Scope cut; `pirate.glb` → **[T0168](0168-asset-import-coverage.md)**, the Sketchfab model was already [T0167](../open/0167-sketchfab-asset-validation.md)
- [x] `13-shader-capability-matrix.md`'s `HpTangentFrame` row has an owner and a status *(2026-08-08 — **T0168**, with the status rewritten: the row is now about the duplication, because the sign it used to be about is fixed)*

## Subtasks

- [x] 166.1 **Scan every finding first, before touching anything.** The material is on disk and none of it needs a build: the two recovered convention audits — [`findings/t0166-audit-camera-and-plus-z.md`](../findings/t0166-audit-camera-and-plus-z.md) and [`findings/t0166-audit-winding-tangent-normals.md`](../findings/t0166-audit-winding-tangent-normals.md) — then T0165's `## Notes / findings` and its "not verified" list, the capability matrix's unowned rows, and D33 as amended. **This subtask exists because the diagnosis above was found by reading and the rest probably can be too** — cheaper than a render, and it decides what the later subtasks are actually for
- [x] 166.1a **Reconcile the audits against what actually landed.** They were written **before** T0165 and describe the tree at `5f20700`, so every item is a claim about the *old* convention — some were swept, some were not, and some were wrong. The winding audit's §5 (tangent frames, normal maps, facing) and its "no `TANGENT` attribute anywhere" finding are the ones that bear directly on this ticket, and they were reached **independently** of the analysis above, which makes them a cross-check rather than an echo. **Produce a list of what is still true**; that list is what 166.2–166.8 are actually against
- [x] 166.2 **Fix the determinant sign**, in `HpSurface.slang` and `rock_pom.slang`, matching Diligent's shape: divide by the signed `det` with a `!= 0.0` guard. **Small, and worthless without 166.3**
- [x] 166.3 **The controlled case**: one asset, two shells, one mirrored, generated the way `make_cube_gltf.py` generates the cube so it is reviewable as source. Assert parallax shifts the **same** direction on both, and that the normal-mapped lighting matches. This is the subtask that matters; 166.2 without it just moves the guess
- [x] 166.4 **Decide `Tangent.w`.** Either widen the vertex path so glTF's sign survives, or state in the contract that `w` is not data and give a shader the derivative-based route instead. **A field that always reads `+1` is worse than an absent one**, because a shader will trust it
- [~] 166.5 → **[T0168](0168-asset-import-coverage.md)**. Measured, not landed; the numbers are in the notes. **Pin the normal-map green channel** with a case that would fail if the convention flipped, and write the answer where an artist will find it — `11-material-format.md`, not a comment
- [~] 166.6 → **[T0168](0168-asset-import-coverage.md)**. Not started. **Vendor conformance assets, or decide not to.** Khronos's `glTF-Sample-Assets` contains `NormalTangentMirrorTest` and `NormalTangentTest` — built for exactly the question 166.3 asks, by the format's own authors. The full set is ~2 GB, so this is a build-harness call (shallow sparse checkout, a small curated copy, or nothing) and it belongs to `03-build-harness.md`'s constraints. **Record the rejection if it is one**
- [~] 166.7 → **[T0168](0168-asset-import-coverage.md)**. Not started. **Render something nobody here authored.** `third_party/meshoptimizer/demo/pirate.glb` is genuine DCC output already in the tree, needs no network fetch and no submodule decision, and is enough to answer "does a real model come out the right way round"
- [x] 166.8 **Take the `HpTangentFrame` row**, and settle or re-record `fHandness` with its trigger

## Not in scope

- **A Sketchfab model, or any asset chosen for how it looks.** [T0167](../open/0167-sketchfab-asset-validation.md) owns that, deliberately second: a production model has several things wrong at once, so it says *something* is wrong rather than *which*. Land a clean signal here first — reversed, the first real asset renders wrong for two reasons and the wrong one gets debugged. That is the trap that cost T0157 a day.
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

---

## 166.1a — the two audits reconciled against what T0165 landed

They describe the tree at `5f20700`. Read as claims, checked against `a9941d1`:

**Still true, and load-bearing here:**

- **`HpParallaxUv` and `rock_pom.slang`'s copy of it are the exposed pair.** The winding audit's §5 put them at risk 3 and named the duplication. Both were still there, both were wrong, and both had to be corrected in the same commit — which is the argument for `HpTangentFrame` in one line.
- **`Tangent.w` is hardcoded `+1`.** Risk 8, still true. Its *stated cause* was wrong in the audit and in the ticket and in three places in the tree; see below.
- **The anisotropy basis is two `cross` calls that define a chirality** (`HpSurface.slang:2038`, `:2043`). Untouched by T0165 and untouched here. Nothing renders anisotropy from vertex tangents in any test, so it is unmeasured.
- **`tests/gpu/module_signature_cost_test.cpp` still builds the one rasterizer state in the tree that does not declare `FrontCounterClockwise`.** `CULL_MODE_NONE`, so inert; still the drift vector `WindingConvention.hpp` rule 1 warns about.
- **The normal-map green channel is completely untested.** Now measured — see below — and it is worse than untested.
- **`fHandness` sits in a stated disagreement with nothing reaching the branch that reads it.** Re-recorded rather than settled (166.8).

**No longer true:**

- **"No `TANGENT` attribute anywhere."** T0159.4 added two: `tests/gpu/custom_shader_material_test.cpp` (`VEC4`, `w = 1`) and `tests/gpu/textured_surface_test.cpp`. `tests/fast/rockcube_mesh_test.cpp:133` still asserts the *cube* has none, which is a different and still-true claim.
- **Every risk in §1–§4 about the winding constant, the `static_assert`, `fHandness`'s value, the `(0,0,-1)` fallback and the gpu quads' placement.** All swept by T0165, verified present in the current tree.
- **The camera audit's §7 documentation list.** Regenerated and rewritten.

**Wrong when written:**

- The winding audit's headline — *"the `static_assert` will actively fight the correct fix … you need `kFrontFaceCounterClockwise = true` while `kImportMirrorsContent` stays `false`, and that assert refuses it"* — is right about the problem and wrong about the resolution. T0165 did not add a third term to a two-term equality; it replaced the model with `kChainMirrorCount` and a **parity** check, so the assert is still a check rather than a tautology.
- The chirality probe's predicted inversion (winding audit §8). T0165 measured that a handedness change is not a screen-space mirror, so the probe's assertions do **not** flip; it had to be re-derived around which *side* of a surface is visible.

**The one thing neither audit reached, and it is this ticket's finding:** both correctly identified derivative-reconstructed frames as *"the classic handedness casualty"* and stopped at "check the sign". Neither noticed that the construction itself carries the chart's chirality, which is why "check the sign" was the wrong instruction — see below.

## What the defect actually was, and why the ticket's own diagnosis was half of it

The ticket said: `HpParallaxUv`'s positive `invMax` drops `det`'s sign, so a **mirrored** shell marches backwards. Work the expansion through properly and Schüler's construction is

```
T_schueler = det · cross(B, N) = (det · h) · dP/du
```

where `h = sign(dot(cross(dP/du, dP/dv), N))` is the **chart's own chirality**. And `det · h` is exactly `dot(cross(dp1, dp2), N)`, which in a framebuffer whose `y` runs **down** — Diligent's Vulkan backend flips the viewport to D3D's convention — is negative on every front-facing fragment.

So the frame came back as `-dP/du`, `-dP/dv` **on every asset in the tree**, mirrored or not: a global 180° rotation. The mirrored-shell story is a special case of it.

**Dividing Schüler's output by the signed `det` — the obvious reading of the diagnosis, and the first thing implemented — fixes the mirrored charts and leaves the plain ones inverted.** Both were measured on the two-shell asset before the final form was written:

| construction | plain shell edge | mirrored shell edge |
|---|---|---|
| Schüler, positive `invMax` (shipped until now) | 128 → **139** | 128 → **139** |
| Schüler ÷ signed `det` | 128 → **139** | 128 → **118** |
| DiligentFX's solve ÷ signed `det` | 128 → **118** | 128 → **118** |

118 is the correct answer and it is decided from first principles, not from a baseline: **parallax samples further along the view ray**, so on a plane yawed `+0.6` about `Y` — which turns the plain shell's `+u` side *away* from the camera — a texture feature must appear shifted toward the **near** side, which is a smaller column.

**The fix is not new algebra.** DiligentFX ships it correct in this tree — `ShaderUtilities.fxh:51-55` solves `dp = T·du + B·dv` directly, divides by the signed `d`, guards `d != 0`, and contains no cross product to be chirally dependent. Their function cannot be *called* from here (it applies a tangent-space normal and returns a world one; the march needs the frame), so the algebra is borrowed and the comment says where from.

**This is not the `sign(dot(cross(ddx, ddy), N))` factor T0165 wrote and reverted**, and the difference is worth stating because both the factor and the revert were reasoning about the wrong thing. The factor patched the symptom by multiplying in a pseudo-scalar; the construction above removes the pseudo-scalar. But T0165's revert was also wrong, and the evidence that persuaded it is explained below.

## Why the rock cube's self-shadow said the correct frame was broken

T0165 reverted on one measurement: the self-shadow's max drop went 124.6 → 0. That measurement was real, and it does not mean what it was read to mean.

**The sample scene's key light travels almost along the view axis** (`(0.15, -0.62, -0.77)`, against a camera looking down its own `-Z`). A light at the eye casts no visible self-shadow — the shadow ray retraces the path the view ray has just proved clear. That is physics.

The old frame marched the shadow ray *away* from that path, which is where the 124.6 came from. With the frame corrected the effect is gone **at every cube yaw**: swept 25 of them from −1.56 to +1.56 rad, best result 3 darkened pixels. Under the old frame the same sweep gives 40–250 darkened at every single yaw.

Re-aiming the key restores it, which is the check that the march itself is fine:

| key yaw | darkened > 10 | max drop |
|---|---|---|
| −1.8 | 29 | 126.7 |
| −0.6 | 0 | 0 |
| **+0.6 (chosen)** | **90** | **116.5** |
| +1.8 | 0 | 0 |
| +2.4 | 101 | 212.0 |

The zeros are the same physics as the 25-yaw sweep: those aims put the key back near the view axis. `CLAUDE.md`'s own rule applies — *re-aim the light rather than re-baseline against it* — so the gpu case re-aims the key for its own render, exactly as it already re-poses the cube. **The committed sample is untouched.** Whether the shipped scene should light the cube from somewhere a viewer can see relief is a question about the sample's look, and [T0158](../inprogress/0158-parallax-depth-cues.md) owns it.

## Measured 2026-08-08 — every value that moved

NVIDIA GeForce RTX 2080, Vulkan, both targets (Linux native, the Windows suite under wine — a plain Linux development box, so wine is correct there and not a fallback).

| suite | before | after |
|---|---|---|
| fast | 324 | 324 |
| integration | 92 | 92 |
| gpu | 66 / 1556 | **67 / 1580** (+1: the two-shell case) |

| measurement | before | after | why |
|---|---|---|---|
| rock cube coverage | 18.079% | 18.079% | geometry untouched |
| cube luminance variation | 24.50 | 26.55 | the relief marches the other way, so a different set of texels is shown; the assertion is a floor |
| four-yaw luminance | 87.4 … 93.4 | 91.5 … 96.7 | same cause. **The assertion is the ratio** (basis consistency across yaws): 1.068 → 1.057, still far inside the 1.25 bound. This is the test that once caught a 5× per-face basis error |
| rock / metal channel variation | 20.77 / 8.15 | unchanged | that quad is not parallaxed |
| glass pane centre | (27,29,8) vs (35,33,23) | (69,75,65) vs (28,27,14) | the pane refracts the cube behind it, and the cube's surface moved |
| reference-plane difference | 20.72 / 29737 px | 20.98 / 30282 px | the same knob against a different march; the assertion is a floor |
| self-shadow: darkened / max drop / mean abs | 50 / 124.6 / 0.0399 | 90 / 116.5 / 0.0824 | the key is re-aimed, above |
| self-shadow black on/off | 15 / 15 | 35 / 26 | see below |
| sway silhouette | gained 1294, lost 1139 | unchanged | vertex stage, not the frame |
| parallax / triplanar-parallax displacement | — | unchanged assertions | those cases are differential and sign-blind, which is *why* they never caught this |

**The `blackOn == blackOff` assertion was relaxed, and it was never a law.** It held because the march was reaching nothing: with the frame rotated, no texel was ever *fully* occluded. `kShadowStrength = 1.0` is documented as letting a full occluder reach black, and now nine pixels do (35 against 26, against a darkened population of 90). Every key yaw with a real effect adds some; tuning the yaw to dodge it would have been preserving an accident. The check's intent survives as a bound — the march may only darken, and the black tail must stay a tail.

## The normal map's bitangent — measured wrong, and handed to T0168

The two-shell asset's **lit** half was written and run before the scope was cut. Only `u` is mirrored between the shells, so a tangent-space normal tilted along **v** must shade them identically. It does not:

| tangent-space tilt | key | shell A (plain) | shell B (mirrored) |
|---|---|---|---|
| `+v` | from above | **0** | **130.2** |
| `−v` | from above | **131.6** | **0** |
| `+u` | from `+X` | 130.5 | 0 |
| `−u` | from `+X` | 0 | 131.3 |

The `u` rows are correct — mirroring `u` must invert the `u` response. The `v` rows are the defect: **DiligentFX takes `b = cross(t, n)`, a fixed chirality rather than the chart's**, so the bitangent flips with the mirror even though `v` did not.

The light directions are **measured, not asserted from quaternion algebra** — the case tilts the *geometry* with no normal map and checks which way is brighter (leaning toward `+Y`: 127.3 against 37.5; facing vs leaning toward `−X`: 101.8 against 38.0). That control is in the committed file.

Two consequences, both T0168's:

1. **On a plain chart, `+Y` in a tangent-space normal map points *down* the image** — the DirectX convention — while `tools/pack_test_textures.py` deliberately packs **NormalGL** (`green = +Y = up`) and says so, citing glTF. So every normal map in this project is applied with its green channel inverted. That is 166.5's question, answered, and not fixed.
2. The correct construction fixes both at once: `b = cross(n, t) · sign(det)` is glTF's `bitangent = cross(N, T) · w` with `w` read from the parameterisation instead of from a vertex attribute the engine drops.

The asset, the PNG writer, the normal maps and the lit render path are all committed in `tangent_frame_test.cpp`; only the parallax case reads them. The lit case is a `TEST_CASE` away rather than a rewrite.

## `Tangent.w` — the stated reason was wrong, in four places

The ticket, `HpSurface.slang:1676`, `HpMaterial.slang:499` and `:648` all said glTF's handedness "never survives the loader" because "Diligent's vertex path is `float3`". **It is our omission, not their limit.** `ModelCreateInfo::VertexAttributes` is a settable pointer documented *"if null is provided, default vertex attributes will be used"*, and `AssetImport.cpp:365` passes null.

**But widening it is not one line, and this is the line that resists:**
`PBR_Renderer::GetVSInputStructAndLayout` generates the `VSInput` struct this engine's shaders are compiled against from `constexpr VSAttribInfo VSTangentAttrib {…, VT_FLOAT32, 3, …}` (`PBR_Renderer.cpp:1660`) — hardcoded, unlike the vertex **colour** three lines above it, whose component count is read from the input layout — and a `DEV_CHECK_ERR` in the same function requires the layout element to match the struct exactly. Widening the buffer alone feeds a four-component stream into a `float3` declaration.

So per the ticket's own rule — a field that always reads `+1` is worse than an absent one — the contract now says plainly that `w` is **not data**, says which line stops it, and gives a shader the route that needs no vertex attribute at all: the signed determinant of `(ddx(uv), ddy(uv))`, which is where a mirrored shell announces itself anyway. `w` is a per-vertex copy of the same fact. Widening is **T0168**.

## The finding this ticket is really about

Three gaps were found in one evening — spec-gloss, the tangent frame, Draco — and [T0168](0168-asset-import-coverage.md)'s `## Why` tabulates all four with the file and line for each. **Every one was a case where DiligentEngine already had the answer and this engine was not asking.** `CLAUDE.md` opens with the check that would have caught them and it did not happen.

**The fifth, and it is this ticket's own:** the check failed here in a way the others did not. The tangent frame was not an *absence* we filled in; it was a correct upstream implementation sitting one directory away, in a file the engine's shader already `#include`s, while the engine ran a hand-copied snippet from a 2013 blog post that was wrong for its own framebuffer. The ticket then spent its first hours re-deriving that algebra from scratch — and got it half right, because the derivation was harder than reading `ShaderUtilities.fxh:51-55`. **The check is not only "does Diligent do this" but "does Diligent already do this *better than the thing we wrote*".** The ticket shrank by more than half once it was asked.

## Not verified, and not claimed

- **No editor frame was looked at.** Every claim here is a gpu-suite measurement. The rock cube's relief now marches the opposite way, and nobody has seen it.
- **The unexplained black-and-speckled top face** the brief mentioned is **not explained**. It was not investigated: the sweep and the re-aim both moved the frame under it, and attributing it would have been a guess. [T0167](../open/0167-sketchfab-asset-validation.md).3 still owns it.
- **Whether the corrected frame is right for a *curved* mesh is untested.** Every asset in the tree is flat-shaded, so the two Gram-Schmidt lines that confine `T` and `B` to the tangent plane are exactly zero-valued everywhere they run today.
- **The anisotropy basis** (`HpSurface.slang:2038`) still builds a chirality from two `cross` calls against a vertex tangent whose `w` is a constant. Nothing renders it from vertex tangents in any test.
- **The `+u` / `−u` half of the normal-map measurement passed**, but it passes for a frame that is mirrored *and* for one rotated 180°, which is exactly the trap the parallax case had to be rewritten to escape. It should not be read as "the `u` axis is confirmed correct".
