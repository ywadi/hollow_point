<!-- Recovered verbatim from a subagent transcript. Do not hand-edit the body. -->

> **Provenance.** This is the complete, unedited report of a convention audit
> dispatched on **2026-08-07**, before [T0165](../completed/0165-right-handed-engine.md)
> swept the handedness change through the engine. Both audits finished, and
> **their completion was never delivered** — the session kept reporting them as
> running, they were assumed lost, and T0165 landed without them. Recovered
> 2026-08-08 from the agent transcripts and committed unchanged.
>
> **It is a snapshot, not a live document.** It describes the tree as it was at
> `5f20700`, *before* the right-handed conversion, so it states the old
> convention as current throughout. That is exactly what makes it useful: it is
> an independent inventory of everything that assumed the old convention, and
> it is [T0166](../completed/0166-tangent-frames-and-real-assets.md).1's input —
> **read it as "what was claimed", then check each item against what landed.**
> Absolute paths have been rewritten repo-relative; nothing else was touched.

---

I have a complete picture. Here is the audit.

---

# Handedness-flip audit — HollowPoint (LEFT-handed, camera looks +Z → RIGHT-handed, camera looks −Z)

**Scope searched:** `engine/`, `apps/`, `samples/`, `tests/`, `tools/`, `docs/`, `claude_documentation/`. Skipped `third_party/`, `build/`, `.harness/`. (You said "except for specific Diligent files I name below" — no Diligent files were actually named, so `third_party/` was not searched. The Diligent entry points the engine relies on are listed in §7 so you can name them next round.)

---

## 1. Scene files (`.hpscene` / scene JSON)

**There is exactly ONE scene file in the whole tree** (verified with `git ls-files` + a full-tree `find`; nothing outside `build/`, nothing untracked):

### `samples/rockcube/content/scenes/rockcube.hpscene`

| Line | Entity | position | rotation (x,y,z,w) | Positive Z? |
|---|---|---|---|---|
| 55–65 | **Camera** (guid `…0101`) | `[0, 1.5, 0]` | `[0.145213, 0, 0, 0.9894]` = `Rot(X, +16.7°)` | camera at z=0, but its **forward is +Z** and the pitch sign is derived from `Rot(X,a): (0,0,1) → (0,−sin a, cos a)` |
| 67–86 | **Sun** (Directional) | `[-2, 4, -1]` | `[-0.914385, -0.221222, 0, 0.33906]` → light travels `(0.15, −0.62, **+0.77**)` | **YES** — deliberately travelling **+Z**, "from the camera's side" |
| 88–105 | **Fill** (Directional) | `[4, 2, 2]` | `[-0.197141, 0.83785, 0, 0.509061]` → travels `(−0.85, −0.20, **+0.48**)` | **YES** (position z=+2, travel +Z) |
| 107–130 | **GlassPane** (MeshRenderer) | `[0, 0.481, **3.4**]` | identity | **YES** |
| 132–155 | **RockCube** (MeshRenderer) | `[0, 0, **5**]` | `[0, 0.258819, 0, 0.965926]` = `Rot(Y, 30°)` | **YES** |

**Why affected:** every placement is "in front of the camera" only under +Z-forward. After the flip the cube, pane and both lights are all *behind* the camera. The file's 50-line header comment (lines 1–52) explicitly states *"The camera looks along **+Z** with an identity rotation"*, derives the camera pitch sign from `Rot(X,a)` mapping forward `(0,0,1)`, and derives both light quaternions from the +Z view axis — **all of that prose becomes wrong, not just the numbers.** Line 121–124 additionally derives GlassPane's `y = 0.481` from the view ray at `z = 3.4`.

Documented example scene (not loaded, but authoritative to readers): `claude_documentation/documentation/10-scene-file-format.md:26-52` — Sun at `[0,10,0]` identity rotation, Player at `[0,0,0]`, Weapon at `[0.3,1.2,0]`. Neutral in Z but the identity-rotation Sun changes meaning.

---

## 2. C++ that builds a camera transform, view matrix, look-at, projection or "forward" vector

**There is no `lookAt`/`LookAt` anywhere in the tree** — the view matrix is always `worldMatrix.Inverse()`. That is handedness-agnostic *mechanically*, but every consumer of its sign is not.

| File:line | What | Why the flip breaks it |
|---|---|---|
| `engine/src/Camera.cpp:59-87` | `projectionMatrix()` — calls `float4x4::Projection(fov, aspect, clipNear, clipFar, false)` | **Diligent's `Projection` is the left-handed form** (`_34 = +1`, maps +Z view space into clip). A RH camera needs `_34 = −1` (or a Z-negating view). This is the single most load-bearing line. |
| `engine/src/Camera.cpp:70-78` | reverse-Z near/far swap via `SetNearFarClipPlanes` semantics | Diligent solves the mapping assuming LH +Z; under RH the swap alone no longer produces near→1/far→0. |
| `engine/include/hp/Camera.hpp:397-421` | Doc block: **"Left-handed, +Z into the screen … not ours to reopen"** | The normative statement of the convention being reversed. |
| `engine/src/CameraSystem.cpp:184-196` | `buildView()`: `resolved.view = cameraWorld.Inverse(); resolved.viewProjection = view * projection` | Correct either way, but produces +Z-forward view space today; every downstream sign assumption hangs off it. |
| `engine/src/CameraSystem.cpp:200-230` | `extractFrustum()` Gribb-Hartmann | Comment claims it is convention-free "because the matrix carries it" — true for the *plane extraction*, but line 222 hardcodes near as `z >= 0` for [0,1] clip. Survives only if the new projection still produces [0,1] clip with the same row signs; must be re-verified, not assumed. |
| `engine/src/CameraSystem.cpp:232-254` | `worldToScreen()` — refuses `clip.w <= 0` | Under LH, `w = view.z` and is positive in front. Under RH `w = −view.z`; if the projection's `_34` is not flipped to −1 this test inverts and **every visible point is refused**. |
| `engine/src/CameraSystem.cpp:256-302` | `screenToWorldRay()` — reverse-Z near=1/far=0 unprojection | Direction of the produced ray flips sign with the projection. |
| `engine/src/SceneRenderer.cpp:1928-1937` | `camera.mView/mProj/mViewProj/…Inv`, `f4Position` from `view.Inverse()` row 3 | Feeds DiligentFX; `mProjInv` is used by `HpViewDepth`. |
| `engine/src/SceneRenderer.cpp:1955-1962` | `camera.SetClipPlanes(far, near)` and **`camera.fHandness = -1.0F;`** with comment *"Left-handed, matching the engine's convention (T0056/T0130)"* | **The literal handedness constant sent to every shader.** Must become `+1.0F`. It is consumed by DiligentFX's `GetPerturbNormalInfo` (see §6). |
| `engine/src/Light.cpp:17-35` | `resolvePlacement()`: `forward = row2 (m20,m21,m22)`, `direction = -forward` | glTF −Z light convention. This one is *already* RH-correct and is therefore the piece that will stop needing the π-yaw compensation everywhere (see §4). |
| `engine/include/hp/Light.hpp:130-132` vs `:158-160` | `ResolvedPlacement::direction` defaults `{0,0,-1}`; **`ResolvedLight::direction` defaults `{0,0,+1}`** | Pre-existing inconsistency between the two structs' documented-identical defaults. Worth fixing in the same sweep since both comments say "along the entity's negative Z". |
| `engine/src/SurfacePipeline.cpp:1097-1100` | `RasterizerDesc.FrontCounterClockwise = kFrontFaceCounterClockwise` | See `WindingConvention.hpp` below — a view-space Z flip **is a mirror** and flips apparent winding once. |
| `engine/include/hp/WindingConvention.hpp:16-30, 55-77` | `kFrontFaceCounterClockwise=false`, `kImportMirrorsContent=false`, joined by a `static_assert` | The header's own reasoning chain explicitly names *"the **left-handed projection** has no XY mirror"* as one of the five steps counted to zero. Flipping to RH inserts a mirror; **item 5 of the header's list applies and the `static_assert` will need both constants to move.** This is the highest-risk coupled item in the whole audit. |
| `engine/src/SceneRenderer.cpp:1382-1398, 1543-1563, 1595-1602` | per-draw `singleSidedCull` from `det3 < 0` of the node matrix | The per-node determinant rule is unaffected, but its *baseline* (BACK) is defined against the zero-mirror chain above. |
| `engine/include/hp/DepthConvention.hpp:32-56` | `kReverseZ = true`, `kDepthClearValue`, the 4-things-must-agree list | Reverse-Z itself is orthogonal, but the doc's derivation (item 1, "swaps near and far — `hp::projectionMatrix` does this") assumes the LH `Projection` helper's algebra. |
| `engine/src/SceneRenderer.cpp:1793-1810` | `DepthFunc = COMPARISON_FUNC_GREATER_EQUAL` + `static_assert(kReverseZ, …)` | Only breaks if the flip is implemented by negating clip Z rather than view Z — flag it during review. |
| `engine/src/SceneView.cpp:199-202`, `engine/src/RenderStack.cpp:122` | depth clears to `kDepthClearValue` | Same caveat as above. |

No hits for `RotationX(`/`RotationY(`/`RotationZ(` as free calls; all rotations are `Quaternion::RotationFromAxisAngle` (see §4).

---

## 3. Tests placing geometry or a camera at a specific Z

### 3a. `tests/fast`

| File:line | Case | Coordinates | Why affected |
|---|---|---|---|
| `tests/fast/camera_system_test.cpp:16` | helper doc | *"A camera entity at a position, **looking down +Z (the engine is left-handed)**"* | The suite's premise, stated in prose. |
| `…camera_system_test.cpp:77-95` | "a view is built from the entity's world transform" | camera at `(0, 2, −10)`; asserts origin lands at **view-space z = +10** with comment *"the engine is left-handed, so it must land at positive view-space z"* | **Sign inverts to −10.** |
| `…camera_system_test.cpp:100-119` | "a camera inherits its parent's transform" | rig at `(5,0,0)`; asserts `viewSpace.z == +4.0` | Same inversion. |
| `…camera_system_test.cpp:263-289` | "the frustum contains what is in front and rejects what is not" | contains `(0,0,10)`, `(0,0,99)`; rejects `(0,0,−10)`, `(0,0,200)`, `(0,0,0.5)`, `(100,0,5)` | **Every in/out verdict flips.** Line 285 comment names this as "the sign gets backwards, and which culls geometry in front of the player". |
| `…camera_system_test.cpp:291-311` | "…sphere…" | `(0,0,105) r=10` inside; `(0,0,−2) r=5` reaches near plane; `(0,0,−500) r=10` out | Flips. |
| `…camera_system_test.cpp:313-330` | "frustum planes are normalised" | camera default | Survives (magnitude only). |
| `…camera_system_test.cpp:332-366` | worldToScreen/screenToWorldRay round-trip | world point `(1.5, −0.75, **+12.0**)` | Point moves behind the camera; `worldToScreen` will refuse. |
| `…camera_system_test.cpp:368-388` | "…projects to screen…" | `(0,0,10)` and `(0,1,10)` | Behind camera after flip. |
| `…camera_system_test.cpp:389-403` | **"a point behind the camera is refused, not projected"** | asserts `(0,0,**−5**)` is refused | **Exactly inverted** — `(0,0,−5)` becomes the in-front case. |
| `tests/fast/camera_test.cpp:32-35` | `ndcDepth(projection, viewZ)` helper — `float4(0,0,viewZ,1) * projection` | Called throughout with **positive** `viewZ` (`nearPlane`, `farPlane`) | Under RH, in-front view-space Z is negative; **every depth-endpoint assertion in this file (lines 63-99, 141-160, 286-297) needs the sign of `viewZ` negated.** |
| `…camera_test.cpp:111-131` | "…pins the construction byte-for-byte against Diligent's own helper" | `float4x4::Projection(fov, aspect, far, near, false)` and `Ortho(...)` | **The byte-for-byte pin against Diligent's LH helper is the test that will hard-fail first** and is the intended guard. |
| `…camera_test.cpp:141-160` | reverse-Z vs conventional comparison | uses `Projection(..., near, far, false)` | Same. |
| `tests/fast/light_test.cpp:81-91` | "an identity transform points down negative Z" | asserts `direction.z == −1` | Already RH-correct; will stop needing compensation elsewhere. |
| `…light_test.cpp:92-99` | "position comes from the fourth row" | `(3, −4, **+5**)` | Positional only, no camera; safe. |
| `…light_test.cpp:114-121` | degenerate transform default facing | asserts `direction.z == −1` | Safe. |
| `tests/fast/scene_test.cpp:303, 318-325, 447` | hierarchy transform maths | `(0,0,**+3**)`, `(0,0,**+5**)`, `(0,0,**+1**)` | No camera involved — **arithmetic only, unaffected.** Listed for completeness. |
| `tests/fast/rockcube_mesh_test.cpp:133-175` | cube winding/normal verification | reads `cube.gltf` positions ±1 | Winding check couples to `kFrontFaceCounterClockwise` (§2). |
| `tests/fast/scene_serialize_test.cpp:47, 52, 229` | round-trip fixtures | `(1,2,3)`, `(4,5,6)`, `[0.3,1.2,0]` | Serialization only; safe. |
| `tests/fast/draw_submission_test.cpp:136,142` | `(10,0,0)`, `(5,0,0)` | X only; safe. |

### 3b. `tests/gpu` — quad meshes authored at **+Z with `(0,0,−1)` normals**

Every one of these writes a glTF quad in the **z = +3 plane** with normal `(0,0,−1)` and winding `{0,2,1, 0,3,2}`, rendered by a default camera at the origin:

| File:line (vertex array) | accessor `min`/`max` | Extent |
|---|---|---|
| `tests/gpu/extended_material_test.cpp:94-99` (`:140`) | `[-4,-4,3] / [4,4,3]` | ±4 |
| `tests/gpu/lighting_stage_test.cpp:111-116` (`:156`) | `[-4,-4,3]` | ±4 |
| `tests/gpu/cooked_shaders_test.cpp:93-98` (`:136`) | `[-4,-4,3]` | ±4 |
| `tests/gpu/material_assignment_test.cpp:106-111` (`:156`) | `[-4,-4,3]` | ±4, +UVs |
| `tests/gpu/render_stack_composites_test.cpp:130-135` (`:181`) | `[-4,-4,3]` | ±4 |
| `tests/gpu/lit_surface_test.cpp:99-104` (`:151`) | `[-4,-4,3]` | ±4 |
| `tests/gpu/scene_draws_mesh_test.cpp:86-91` (`:137`) | `[-4,-4,3]` | ±4 |
| `tests/gpu/custom_shader_material_test.cpp:104-118` (`:168`) | `[-4,-4,3]` | ±4, ± tangents |
| `tests/gpu/triplanar_test.cpp:98-103` (`:144`) | `[-4,-4,3]` | ±4 |
| `tests/gpu/module_signature_cost_test.cpp:599-604` (`:635`) | `[-0.1,-0.1,3] / [0.1,0.1,3]` | tiny quad |
| `tests/gpu/present_blit_test.cpp:108-113` (`:159`) | `[-4,**0.5**,3] / [4,5,3]` | deliberately **off-centre in Y** to detect vertical flips — this one also pins row order |
| `tests/gpu/textured_surface_test.cpp:104,118,131-141` (`:192-195`) | `kQuadDistance = 3.0F`; `kQuadHalf = 3.0 * tan(fov/2)` | **half-extent is derived from the camera** so it exactly fills the frame — the derivation `distance * tan(fov/2)` needs `|z|`, not `z`, after the flip |
| `tests/gpu/chirality_test.cpp:99-136` (`:180`) | glyph "F" in the **z = 6** plane, `[-2,-3,6] / [2,3,6]`, normals `(0,0,−1)` | see below |
| `tests/gpu/asset_import_test.cpp:108-112` (`:154`) | triangle at **z = 0** with normal `(0,0,+1)` | CPU import only, never rendered; the one asset whose winding and normal both point +Z |

**Per-case Z placements and camera moves:**

| File:line | Case | Coordinates |
|---|---|---|
| `tests/gpu/chirality_test.cpp:1-35, 98-100, 198` | **"the chirality probe: an F pins where each world direction lands on screen"** | Header states *"with an identity camera looking **+Z**, world +X lands on screen right"*; glyph at **z = 6**; camera default at origin. **This is the test that exists precisely to detect this flip and will invert its verdict** (it asserts the shared stem edge lands at low columns). Note it uses `min/max` at z=6 and `6·tan(30°) ≈ 3.46` framing maths. |
| `tests/gpu/parallax_test.cpp:272-274, 295` | "parallax occlusion displaces texels by the height map, by heightScale" | quad **`(0,0,+3.0)`**, `Rot(Y, 0.7)`; camera default | Quad goes behind camera; the oblique-yaw rationale ("parallax scales with the tangent-plane component of the view direction") reverses. |
| `tests/gpu/lit_surface_test.cpp:21, 221, 240` | "a lit surface is the colour it was authored as" | header: *"The light points down **+Z** and the quad faces the camera at **z = 3**"*; camera `Camera{}` at origin; sun `Rot(Y, π)` | The π-yaw exists solely to turn the glTF −Z light into +Z travel — **it becomes redundant/inverted.** |
| `…lit_surface_test.cpp:297-299` | mirrored-scale subcase | `scale = (−1, 1, 1)` | Interacts with the determinant cull rule and the new mirror. |
| `…lit_surface_test.cpp:362-400` | "a point light attenuates with distance and stops at its range" | lamp at **z = +1.0** (near), **z = −6.0** (far), **z = −34.0** (past range) | Distances measured from a quad at z=+3; **near/far ordering inverts**. |
| `…lit_surface_test.cpp:405-420` | "a spot light lights its cone and not the world outside it" | lamp at **`(0,0,+1.0)`** with `Rot(Y, π)` | Same. |
| `tests/gpu/lighting_stage_test.cpp:226-231` | `Lamp::position{0,0,0}` + **`bool faceTheQuad = true`** → `Rot(Y, π)` at `:315-317` | doc: *"Half-turn about Y, so the lamp travels **+Z** onto the quad's −Z face"* | The whole compensation vanishes. |
| `…lighting_stage_test.cpp:570-575` | "the interface-shaped light loop's cost is measured" | 16 point lamps on a grid at **z = 0.0**, `x,y ∈ [−1.5, 1.5]` | Lamps end up behind a z=+3 quad relative to a −Z camera. |
| `…lighting_stage_test.cpp:646, 589` | "a per-light override cel-shades…" (spot subcase) | spot at **`(0,0,0)`**, cones 0.15/0.25 vs 1.0/1.3 | Cone axis is the light's −Z; with `faceTheQuad` it aims +Z. |
| `…lighting_stage_test.cpp:776-810` | "light index 0 is the dominant light" | two directional lamps, default `faceTheQuad` | Same π-yaw dependence. |
| `tests/gpu/textured_surface_test.cpp:341-395` | "each surface channel can be inspected on its own" / "a textured surface renders its texture" | sun rotation = `Rot(Y,π) · Rot(Y,−0.7) · Rot(X,−0.7)`; asserts **`direction.y < 0`** | Comment at `:345` says *"an identity transform points straight down **+Z**"* (about the light, and is itself wrong — `Light.cpp` gives −Z; the π yaw is what makes it +Z). The composed rotation and the assertion must be re-derived. |
| `tests/gpu/extended_material_test.cpp:242-244` | all extended-material subcases | sun `Rot(Y, π)`, comment *"so the light travels +Z onto the quad's front"* | Same. |
| `tests/gpu/triplanar_parallax_test.cpp:240-243` | "parallax under triplanar works on a terrain-shaped surface" | **terrain grid spans z ∈ [+4, +16]**, x ∈ [−6,6], base y = −3, two gaussian hills at `(1.5, 9.5)` and `(−2.5, 12.0)`; comment: *"The camera at the origin looks down **+Z** across it"* | **Entire terrain is behind the camera after the flip.** |
| `…triplanar_parallax_test.cpp:537-538, 515` | "parallax under triplanar displaces texels on a UV-less quad" | oriented quad centre **`(0,0,+4)`**, normal `(0.644, 0, −0.766)` | Normal points back at a +Z camera. |
| `…triplanar_parallax_test.cpp:649-653, 621` | "parallax under triplanar: cost per weight configuration" | `{0,0,**+5**}` n`(0,0,−1)`; `{2.5,0,**+5**}` n`(0.707,0,−0.707)`; `{1.2,1.2,**+5**}` n`(−0.55,−0.55,−0.63)` | All three centres +Z, all three normals −Z-ward. |
| `tests/gpu/screen_inputs_test.cpp:224-225, 325` | all six cases | `Surface::z` default **5.0**; instantiations at **z = 6.0, 5.0, 3.0** (lines 419, 430-431, 438-439, 545-546, 603, 662, 673, 740-741, 751-752, 762-763, 828-829) | Doc: *"Where it sits on the view axis."* Refraction/depth-fade layering (wall behind, pane in front) **reverses** — and `HpSceneViewDepth − HpViewDepth` clearance goes negative. |
| `tests/gpu/rockcube_sample_test.cpp:268, 336-345` | "the rock cube sample renders its committed content" | asserts **`lights[i].direction.z > 0.0F`** with comment *"travelling away, from the camera's side"*, plus `direction.y < 0` and opposite `direction.x` signs | **Direct sign assertion on Z — inverts.** |
| `…rockcube_sample_test.cpp:448-458` | same case, glass pane | *"The pane sits on the camera's view axis"*, footprint 2%–30% of frame, corners untouched | Depends on the pane at z=+3.4 being in front. |
| `…rockcube_sample_test.cpp:464, 566-568` | "parallax self-shadowing darkens the frame" | cube re-posed to **`(0,0,+5.0)`**, `Rot(Y, 0.45)`; the yaw was chosen so the key light grazes the visible face at `(0.15,−0.62,+0.77)` | The chosen yaw and the "~11 degrees above its horizon" derivation are camera-relative. |
| `…rockcube_sample_test.cpp:858, 911-915` | **"the rock cube's faces cull from inside, so they wind outward"** | camera moved to **`(0,0,+5.0)`** = cube centre; asserts `covered(pixels) == 0` | Position stays valid (it's the cube's centre), but the *result* couples to `kFrontFaceCounterClockwise`, which the flip forces to move (§2). **This case will fail if the winding constant is not flipped with the handedness.** |
| `…rockcube_sample_test.cpp:936, 1013-1017` | "the cube's lighting is anchored to the world, not to the mesh" | cube at **`(0,0,+5.0)`** at yaw 0/90/180/270 | Behind camera. |
| `…rockcube_sample_test.cpp:697, 1054` | ".hpmat parameter" / "vertex hook moves the silhouette" | load committed scene as-is | Inherit §1. |
| `tests/gpu/render_stack_composites_test.cpp:125, 295-314, 461` | "a world layer and a HUD layer composite correctly" | doc: *"**The engine is left-handed and both cameras look down +Z, so it sits at z = 3.**"*; world lens perspective viewport `{0,0,0.5,1}`, HUD lens **orthographic** `orthographicSize=2` viewport `{0.5,0,0.5,1}` | Explicit statement; the orthographic HUD camera also has near/far clipping the quad at z=+3 must satisfy. |
| `tests/gpu/scene_draws_mesh_test.cpp:189, 232` | "a mesh is drawn and reaches the target" | default camera at origin, quad at z=+3 | Behind camera → nothing drawn. |
| `tests/gpu/triplanar_test.cpp:245, 272` | "triplanar projection textures a mesh that has no UVs" | default camera, quad z=+3 | Same. |
| `tests/gpu/cooked_shaders_test.cpp:232, 331, 405` | cooked-shader cases | default camera, quad z=+3 | Same. |
| `tests/gpu/material_assignment_test.cpp:186, 420` | "mirrored single-sided quad" | quad z=+3, scale **`(−1,1,1)`** | Mirror-determinant path; couples to the new mirror. |
| `tests/gpu/custom_shader_material_test.cpp:240, 307, 1155` | vertex-contract case | `worldOffset` default `(0,0,0)`, one case `(2,0,0)`; quad z=+3 | Quad behind camera. |
| `tests/gpu/scene_renderer_test.cpp:174, 221, 237, 290` | "builds and submits" / "publishes an offscreen frame" | `Camera{}` at origin, quad z=+3 | Same. |
| `tests/gpu/module_signature_cost_test.cpp:726` | signature-cost bench | 400 instances at `(i%20 * 0.01, …)` around the z=+3 quad | Same. |
| `tests/gpu/present_blit_test.cpp:296` | "present blit reproduces its source on Vulkan" | quad `y ∈ [0.5, 5]`, z=+3 | Quad behind camera; the Y-asymmetry check itself is unaffected. |

### 3c. `tests/integration`

| File:line | Case | Coordinates |
|---|---|---|
| `tests/integration/rockcube_module_test.cpp:83` | inline `.hpscene` YAML fixture | `position: [0, 0, **5**]` | Mirrors the sample scene; **the only other place a scene is authored in-tree.** No camera in the fixture (spin-composition test only), so it will still *pass* — but it will silently document a stale convention. |

---

## 4. Light direction / rotation authoring

The engine's light convention (glTF −Z, `Light.cpp:29-33`) is **already right-handed**. Under the current +Z camera, *every* call site compensates with a π yaw about Y. **Every one of those compensations becomes wrong after the flip.**

| File:line | Authored rotation | Why affected |
|---|---|---|
| `samples/rockcube/content/scenes/rockcube.hpscene:78` (Sun) | quaternion `[-0.914385,-0.221222,0,0.33906]` → travel `(0.15,−0.62,+0.77)` | Hand-derived against +Z view axis |
| `…rockcube.hpscene:96` (Fill) | `[-0.197141,0.83785,0,0.509061]` → travel `(−0.85,−0.20,+0.48)` | Same |
| `apps/editor/src/EditorMain.cpp:335-355` | `Rot(Y, 0.6) · Rot(X, 0.45)`, with a comment stating *"An identity transform points a light straight down −Z, exactly anti-parallel to the camera's view"* and describing the resulting dark radial specular smudge | **The stated failure mode inverts**: after the flip an identity lamp is *co*-parallel, not anti-parallel, so the "guaranteed to look wrong" arrangement moves. |
| `tests/gpu/lit_surface_test.cpp:234-240` | `Rot(Y, π)` on the directional sun | Compensation removed |
| `…lit_surface_test.cpp:411-418` | `Rot(Y, π)` on the spot, position `(0,0,1)` | Compensation removed |
| `tests/gpu/lighting_stage_test.cpp:228-231, 313-318` | `Lamp::faceTheQuad` → `Rot(Y, π)` for **every lamp in every case in the file** | Compensation removed |
| `tests/gpu/extended_material_test.cpp:242-244` | `Rot(Y, π)` | Compensation removed |
| `tests/gpu/textured_surface_test.cpp:343-375` | `Rot(Y,π) · Rot(Y,−0.7) · Rot(X,−0.7)`, plus the `direction.y < 0` probe at `:376+` | Compensation removed; rake signs re-derive |
| `tests/gpu/rockcube_sample_test.cpp:338-345` | **assertions** `direction.z > 0`, `direction.y < 0`, `dir[0].x * dir[1].x < 0` | `.z > 0` inverts |
| `tests/fast/light_test.cpp:81-91, 114-121` | `direction.z == −1` for identity/degenerate | **Stays correct** — becomes the natural case |
| `engine/include/hp/Light.hpp:20-22, 130-132, 158-160` | "points along the **negative Z**" doc + the two mismatched defaults | Doc becomes consistent with the camera for the first time; fix the `{0,0,+1}` default at `:160` |
| `engine/shaders/HpSurface.slang:878-879` | no-light permutation fallback `Direction=(0,0,−1)`, `ToLight=(0,0,+1)` | Consistent with glTF; verify against the new view space |
| `engine/shaders/HpMaterial.slang:981-1010` | `HpLight` contract docs (`Direction`, `ToLight`, `Attenuation`) | Prose only |
| `samples/rockcube/content/shaders/rock_pom.slang:216-224, 406` | `lightTS.xy / lightTS.z` self-shadow march, horizon fade tuned against the scene's key at `(0.15,−0.62,+0.77)` | Tuned constants (`horizonFade`, lateral cap) were measured against the current light/camera geometry. |

---

## 5. Editor demo / fallback quad and hardcoded scenes in `apps/`

**`apps/editor/src/EditorMain.cpp`** — the only hardcoded scene in `apps/`:

- `:236-243` — fallback trigger (`if (scene_->scene().size() == 0) populateDemoScene(render)`).
- **`:275-280`** — the quad's vertex array, generated to a temp `quad.gltf` at runtime:
  ```
  -1.5F, -1.5F, 4.0F, 0.0F, 0.0F, -1.0F,
   1.5F, -1.5F, 4.0F, 0.0F, 0.0F, -1.0F,
   1.5F,  1.5F, 4.0F, 0.0F, 0.0F, -1.0F,
  -1.5F,  1.5F, 4.0F, 0.0F, 0.0F, -1.0F,
  ```
  **z = +4.0, normals `(0,0,−1)`**, indices `{0,1,2, 0,2,3}` at `:281`. Note this quad still uses the **pre-T0152 winding** `{0,1,2,0,2,3}` — inconsistent with every re-wound test asset, and it survives today only because the demo material is `"doubleSided":true` (`:292`).
- `:288-296` — the glTF JSON embeds `"min":[-1.5,-1.5,4.0],"max":[1.5,1.5,4.0]`.
- `:328` — `scene.create("Camera").add<hp::Camera>(hp::Camera{})` — **default camera, identity transform at the origin**; the quad is visible only because it sits at +Z.
- `:335-355` — the sun rotation (see §4).
- `:272-274` — comment "A quad facing the camera."

**Why affected:** after the flip the editor's fallback shows an empty clear colour, and the "no gameplay module → still show something" guarantee silently disappears.

`apps/runtime/src/RuntimeMain.cpp` — **no camera, no geometry, no Z literals.** Clean.
`samples/sandbox/src/Sandbox.cpp` — no camera/geometry. Clean.
`samples/rockcube/src/RockCube.cpp:74, 238-269` — spin axis `(0,1,0)`, incremental `Rot(axis, radians) * next.rotation`. **Y axis, unaffected** — but the sign of a Y-yaw's *apparent* direction on screen reverses with the mirror.

---

## 6. Near/far, frustum, culling-by-Z, depth and clip space that assumes a sign

| File:line | Item | Why affected |
|---|---|---|
| `engine/src/Camera.cpp:68` | `kNegativeOneToOneZ = false` ([0,1] clip) | Diligent's `Projection` produces [0,1] **for the LH form**; must be re-pinned. |
| `engine/src/Camera.cpp:77-78` | `clipNear/clipFar` reverse-Z swap | See §2. |
| `engine/src/Camera.cpp:16-27` | `usable()` — requires `near > 0` and `far > near` | Planes stay positive distances under RH; no change needed, but confirm the projection helper agrees. |
| `engine/src/CameraSystem.cpp:219-224` | **near plane `= (m._13, m._23, m._33, m._43)` i.e. `z >= 0`; far `= row4 − row3`** | The [0,1]-clip-space assumption; only survives if the new projection keeps the same row-sign structure. |
| `engine/src/CameraSystem.cpp:29-43` | `Frustum::contains` / `intersectsSphere`, `distance < -r` | Verdicts flip with the planes. |
| `engine/src/CameraSystem.cpp:240` | **`if (!(clip.w > 0.0F)) return false;`** | The single "is it in front of the camera" test in the engine. |
| `engine/src/CameraSystem.cpp:277-281` | `nearZ = kReverseZ ? 1 : 0`, `farZ = kReverseZ ? 0 : 1` unprojection | Ray direction. |
| `engine/include/hp/CameraSystem.hpp:184-186` | doc: "false when the point is **behind the camera**" | Prose. |
| `engine/include/hp/DepthConvention.hpp:52-62` | `kReverseZ`, `kDepthClearValue`, `ClipSpace::yToV` | The 4-item agreement list; item 1 references `projectionMatrix`. |
| `engine/src/SceneRenderer.cpp:1793-1810` | `COMPARISON_FUNC_GREATER_EQUAL` + `static_assert(kReverseZ, …)` | See §2. |
| `engine/src/SceneRenderer.cpp:861-870` | unfed `g_SceneDepth` defaults to **black = device depth 0 = far plane under reverse-Z** | Semantics survive; verify. |
| `engine/src/SceneView.cpp:199-202`, `engine/src/RenderStack.cpp:122` | depth clears | See §2. |
| `engine/shaders/HpSurface.slang:344-358` | **`HpViewDepth`: `mul(float4(0,0,deviceDepth,1), mProjInv); return unprojected.z / unprojected.w;`** | Returns a **signed** view-space Z. Under RH this becomes **negative** for everything in front. Documented as "distance along the view axis, in metres" — it will start returning negative metres unless negated. |
| `engine/shaders/HpSurface.slang:330-340` | `HpSceneDepth`, reverse-Z near=1/far=0 | Prose + semantics. |
| `engine/shaders/HpSurface.slang:1221, 1275` | `…GetPerturbNormalInfo(…, g_Frame.Camera.**fHandness**)` — two call sites | Consume the constant flipped at `SceneRenderer.cpp:1962`. |
| `engine/shaders/HpSurface.slang:1655-1656, 2180` | `surfaceIn.ViewDir = normalize(CameraPos − WorldPos)` | World-space; mechanically fine, but **every consumer's tuned sign** (parallax `viewTS.z > 0` guards) depends on the geometry being in front. |
| `engine/shaders/HpSurface.slang:463-481` | `if (dot(N, In.ViewDir) < 0.0) N = -N;` then `viewTS = (dot(T,V), dot(B,V), dot(N,V))` → `HpParallaxMarch` | The forced-toward-viewer flip stays valid; the march's `1/viewTS.z` divergence budget shifts. |
| `engine/shaders/HpSurface.slang:1556-1572` | glTF determinant rule, shader half — `if (determinant((float3x3)NodeMatrix) < 0) gltfFrontFace = !gltfFrontFace;` | **Per-node only.** The *view-level* mirror the flip introduces is item 5 of `WindingConvention.hpp`'s list and is NOT handled here. |
| `engine/shaders/HpSurface.slang:1626-1634` | no-normals fallback `surfaceIn.Normal = (0,0,−1)` — "Facing the camera is the least wrong answer" | **Inverts**: `(0,0,−1)` stops facing the camera. |
| `engine/shaders/HpSurface.slang:2134-2136` | vertex-stage no-normals stand-in `(0,0,+1)` | Already the opposite of the pixel-stage fallback; re-derive both together. |
| `engine/shaders/HpSurface.slang:2203, 2222` | `mul(float4(WorldPos,1), mViewProj)` / `PrevCamera.mViewProj` | Motion vectors inherit the flip. |
| `samples/rockcube/content/shaders/glass.slang:110-118` | **`clearance = HpSceneViewDepth(In.ScreenUV) - HpViewDepth(In.ScreenPos.z); contact = saturate(clearance / contactRange);`** | Both terms flip sign → `clearance` goes negative → `saturate` clamps to 0 → **the pane's contact fade inverts to fully transparent/opaque.** The `contactRange` value in `glass.hpmat` was measured against the 0.6 m clearance derived in the scene file. |
| `samples/rockcube/content/shaders/rock_pom.slang:293, 323-372, 406-419` | `dot(N, ViewDir) < 0` flip, `viewTS.z <= 1e-4` early-out, `viewTS.xy / viewTS.z`, `numLayers = lerp(32,8,saturate(viewTS.z))`, `lightTS.z <= 1e-4` | Tangent-space guards; the tuned horizon-fade constants were measured on the current camera/light geometry. |
| `engine/src/DrawSubmission.cpp:7-31`, `engine/include/hp/DrawSubmission.hpp:71-129` | **No Z-based sorting or culling exists yet** — only `LayerMask` culling (`culledByLayer`) | Clean today; `claude_documentation/backlog/open/0045-culling-and-render-queues.md` is where depth sorting lands, and it should be written against the new convention. |
| `engine/src/AssetImport.cpp` | grepped for `axis|convert|flip|negate|mirror|handed` → **zero hits** | Confirms the importer does no conversion. If you choose "flip via import mirror" rather than "flip via projection", this is where it goes and `kImportMirrorsContent` must move with it. |

---

## 7. Documentation that states the convention (all must be rewritten)

Normative / hand-written:
- `claude_documentation/documentation/06-engine-conventions.md:334-386` — "Winding, facing and chirality"; line 342 *"the **left-handed projection** has no XY mirror"*; lines 373-374 *"With an identity camera looking **+Z**, world +X lands on screen right"*.
- `claude_documentation/documentation/02-decision-log.md:2046-2130` — **D33** in full: the zero-reversal count, the rejected alternatives, and lines 2122-2130 which explicitly leave the mirror decision open ("Left open, deliberately, for the owner"). **This flip is the resolution of that open item.**
- `claude_documentation/documentation/02-decision-log.md:1018-1026` — reverse-Z / `GLTF_PBR_Renderer` rationale.
- `claude_documentation/backlog/inprogress/0152-winding-convention.md:33-39, 57-101, 114-131, 139-159, 186-191, 208-239` — **T0152.6 is the open ticket this work closes.** Line 57 records `AssetImport.cpp:343` as the import point; line 59 records `float4x4::Projection` → `BasicMath.hpp:1835` as "Diligent's left-handed form (`_11`, `_22` positive, `_34 = +1`, no XY mirror)".
- `claude_documentation/documentation/08-frame-anatomy.md:75, 95-98` — frame step 10.2 and the determinant rule.
- `claude_documentation/documentation/05-verification-status.md:138` — "Reverse-Z is right | It draws at all."
- `claude_documentation/documentation/10-scene-file-format.md:15-52` — the complete example (§1).
- `claude_documentation/backlog/completed/0130-camera-lens-model.md:60-61, 77, 153-189, 242-279` — §130.3 "**Left-handed, [0,1] clip space, reverse-Z**"; line 155 *"Handedness is Diligent's and not ours to reopen. Left-handed, +Z into the screen"*.
- `claude_documentation/backlog/completed/0081-camera-system.md:164-169, 322` — frustum derivation and *"The projection convention (reverse-Z, depth range, handedness) must be…"*.
- `claude_documentation/backlog/completed/0157-rock-cube-sample.md`, `0159-open-the-material-contract.md:42-51`, `0147-engine-intermediates-for-shaders.md:206-217`, `0139-hand-authored-scenes.md` — the derivations behind the sample scene's numbers.
- `claude_documentation/backlog/completed/0134-pbr-renderer-adoption.md:80-138, 390` — reverse-Z compatibility findings.
- `claude_documentation/backlog/open/0045-culling-and-render-queues.md`, `0086-shadows.md`, `0063-editor-camera-and-picking.md`, `0033-viewport-panel.md` — **unwritten features that will bake in whichever convention is live when they land.**

**Generated** (regenerate via `tools/gen_api_docs.py` and `gen_shader_docs.py`; `tools/api_docs_baseline.txt` may need rebaselining):
- `docs/api/Camera.md:192-197` — *"**Left-handed, +Z into the screen**"*
- `docs/api/WindingConvention.md:27, 68-72`
- `docs/api/DepthConvention.md:19-64, 91`
- `docs/api/CameraSystem.md:141, 161-166`
- `docs/api/Light.md:62`
- `docs/api/SceneView.md:141`
- `docs/shaders/HpMaterial.md:231-232, 261, 541, 804-813`, `docs/shaders/engine-functions.md:30, 41`, `docs/shaders/IHpMaterial.md:177`

Also convention-bearing: `tools/make_cube_gltf.py:16-99` — the cube generator's winding rule (`cross(t,b) == n`, `FACE_INDICES = [0,1,2, 0,2,3]`, `FACE_INPUTS` with `SIDE_DOWN` bitangents and the `+Z`/`-Z` face entries at lines 94-95) is written directly against `kFrontFaceCounterClockwise == kImportMirrorsContent == false`. If those constants move, this script and the committed `samples/rockcube/content/models/cube.gltf` (positions `[-1,-1,-1] … [1,1,1]`) must be regenerated, and `tests/fast/rockcube_mesh_test.cpp:133-175` re-verifies it.

---

## The three highest-risk items

1. **`WindingConvention.hpp`'s `static_assert`** (`engine/include/hp/WindingConvention.hpp:77`). A view-space Z flip *is* the mirror the header's item 5 warns about. If `kFrontFaceCounterClockwise` does not move with it, **every single-sided mesh in the world vanishes** — and the only test that would catch it is `rockcube_sample_test.cpp:858`.
2. **`camera.fHandness = -1.0F`** (`engine/src/SceneRenderer.cpp:1962`) — a single literal, consumed by two `GetPerturbNormalInfo` call sites, that will silently invert every tangent-space normal map if missed.
3. **`HpViewDepth`** (`engine/shaders/HpSurface.slang:354-358`) returning negative metres — it has no test of its own, and its one real consumer (`glass.slang:117`) fails as a *material-looks-wrong* bug, not a depth bug.