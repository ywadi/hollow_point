# T0165 — The engine becomes right-handed, matching glTF

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 466 |
| **Created** | 2026-08-07 |
| **Refs** | [0152-winding-convention.md](0152-winding-convention.md) — **the ticket this closes.** 152.6's chirality probe measured the display mirror on hardware and put the decision to the owner; this ticket is the answer and the sweep; [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) **D33** (amended here — the amendment *is* this ticket's decision record), **D35** (why the import mirror was rejected: a boundary conversion is the silent-failure class it exists to prevent), D29 (Vulkan-only, which makes Diligent's internal viewport flip a constant), D21 (why the convention headers include nothing); `engine/include/hp/HandednessConvention.hpp` — the constant, and `DepthConvention.hpp`/`WindingConvention.hpp` the precedent it is modelled on; [../open/0086-shadows.md](../open/0086-shadows.md) — its bias tuning and shadow-pass cull choice are made against the convention this ticket settles; [../../documentation/13-shader-capability-matrix.md](../../documentation/13-shader-capability-matrix.md) — gained the `HpTangentFrame` row this ticket's one unresolved finding argues for |

## Why

**The owner's decision, taken on T0152.6's evidence:** the engine becomes
right-handed, matching glTF — Godot's convention, not Unity's.

The reasoning, recorded in full in D33's amendment: the entire
content-authoring ecosystem is right-handed — Blender, Maya, Houdini, glTF
itself — and Unity and Unreal are the outliers. Matching the format we import
means a Blender artist's model arrives as authored, a position debugged in the
engine has the same sign as in Blender, and round-tripping is lossless. The
rejected alternative (Unity's import mirror) makes the engine correct and leaves
the **boundary** permanently converted — every future attribute type would owe a
conversion, and a missed one renders subtly mirrored rather than failing. That
is the silent-failure class **D35** exists to prevent, and this codebase was
bitten by it three times in two days.

## The gate (165.1) — measured before anything was swept

**The instruction was to stop and report rather than work around it if
right-handedness and reverse-Z did not compose.** They compose exactly.

Diligent's `Matrix4x4::Projection` is commented *"Left-handed projection"*
(`BasicMath.hpp:1835`) and every rotation helper there is *"D3D-style
left-handed"*, so this is built against their grain. A right-handed projection
is `diag(1, 1, −1, 1) · M` — a negation of view-space Z applied *before* the
projection, which in this engine's row-vector convention is a negation of the
matrix's **third row**. `_31` and `_32` are zero in both the perspective and the
orthographic form, so `_33` and `_34` are the whole of it.

| Claim | Measured (`tests/fast/camera_test.cpp`, deviceless) |
|---|---|
| It differs from the left-handed form in the third row and nowhere else | `_33`, `_34` negated; `_11`, `_22`, `_43`, `_44` byte-identical |
| **It is not a screen-space mirror** | A point at `(1, 2, −5)` still projects to positive NDC x and y — world +X on screen right, +Y on top |
| `w` carries the sign | `_34 == −1`; `w > 0` in front, `w < 0` behind, so `worldToScreen`'s refusal still works |
| **Reverse-Z survives, perspective and orthographic alike** | `SetNearFarClipPlanes(far, near)` still gives near → 1, far → 0; the orthographic `_34` is already 0 so the mirror is entirely in `_33` |
| It is exactly one mirror | `det(RH) · det(LH) < 0` |

The byte-for-byte pin against Diligent's own helper was **kept** and its
*expectation* mirrored, rather than deleted — so an upstream change to their
algebra is still caught.

**`fHandness` becomes `+1.0F`**, and what reads it is narrower than its name:
only `GetPerturbNormalInfo`'s gradient-normal branch, which runs solely for a
fragment whose interpolated mesh normal is zero-length
(`PBR_Shading.fxh:163-176`). No asset in this engine reaches it — every mesh
carries `NORMAL` and the importer refuses one that does not. See "Not verified"
below for the disagreement this leaves standing.

## Done when

- [x] A right-handed projection is proven to compose with reverse-Z **before**
      any content moves, or the work stops and reports *(2026-08-07 — the table
      above; `camera_test.cpp`'s gate case is the first thing in the file and is
      deliberately the only one that touches coefficients)*
- [x] `kRightHandedCameraSpace` exists as a declared constant in its own header,
      and `hp::projectionMatrix` reads it *(2026-08-07 —
      `engine/include/hp/HandednessConvention.hpp`, modelled on
      `DepthConvention.hpp`, includes nothing)*
- [x] The `static_assert` expresses the **real** invariant rather than being
      satisfied by flipping a constant that should not move *(2026-08-07 — see
      "The assert" below; `kChainMirrorCount` and a parity check)*
- [x] Every scene, asset, probe and doc in the tree agrees with the new
      convention *(2026-08-07 — 16 gpu quads, one hand-authored scene, the
      editor's demo scene, one integration fixture, six documents)*
- [x] The chirality probe measures the *absence* of the mirror, and can still
      fail *(2026-08-07 — and it needed re-deriving, not re-baselining; see
      below)*
- [x] `zig build all`, `test -Dtest=all`, `test -Dtest=gpu`, `docs` clean on both
      targets, with every changed value explained *(2026-08-07 — fast 324,
      integration 92, gpu 66; the value table below)*
- [x] D33 amended deliberately, keeping the original argument as history
      *(2026-08-07 — D27's pattern: banner, original entry, "Why the handedness
      was reopened")*

## Subtasks

- [x] 165.1 **The gate** — a right-handed projection against reverse-Z, measured
      deviceless before the sweep. Done 2026-08-07, evidence above.
- [x] 165.2 **The convention headers and the projection** —
      `HandednessConvention.hpp`; `WindingConvention.hpp` rewritten around
      `kChainMirrorCount`; `makeRightHanded` in `Camera.cpp`.
- [x] 165.3 **The engine's Z-sign consumers** — `fHandness`, `HpViewDepth`, the
      no-normals fallback normal, `ResolvedLight::direction`'s default.
- [x] 165.4 **The sweep** — content, tests, probes.
- [x] 165.5 **The screen-basis measurement** — a new gpu case pinning
      `sign(dot(cross(ddx(P), ddy(P)), N))`, which nothing in the tree made
      visible and which two rounds of reasoning got wrong.
- [x] 165.6 **Docs and the D33 amendment.**

## The assert that would have fought the correct fix

`WindingConvention.hpp` chained `kFrontFaceCounterClockwise ==
kImportMirrorsContent`, which models **the importer as the only possible
mirror**. A projection handedness change is a mirror it cannot see, so
satisfying it would have meant flipping `kImportMirrorsContent` to `true` —
declaring an import mirror that does not exist, to make a compile-time check
happy.

The real invariant is **parity**:

> `kFrontFaceCounterClockwise` is true exactly when the number of mirrors
> between authored glTF space and the framebuffer is even.

`kChainMirrorCount` names the two terms — the importer, and the camera chain's
chirality — and `kFrontFaceCounterClockwise` stays a literal so the assert is a
check rather than a tautology. Per-node mirrors are deliberately not counted:
they vary per draw and are handled there (T0152.5), and a compile-time constant
cannot honestly describe them.

`tests/fast/rockcube_mesh_test.cpp`'s convention case had the same defect and
was rewritten the same way. It asserted `CHECK_FALSE` on both constants and said
*"if either of these moves, the asset above is wrong and this test is the thing
that says so"* — true under the two-mirror model, false of a handedness change:
the cube is authored in object space and is still correct. **A check that models
a cause it cannot see reports a correct asset as a broken one**, which is worth
recording because the check was this project's own and three days old.

## What the sweep touched, and the three places "move it" was the wrong answer

**Mechanical:** sixteen gpu quads from `z = +3` to `z = −3`, normals −Z → +Z,
indices `{0, 2, 1, 0, 3, 2}` → `{0, 1, 2, 0, 2, 3}`. That is **not a revert of
T0152**: the rule (winding agrees with the authored normal) never changed, only
which normal faces the lens. The sample scene's two light quaternions and camera
pitch were **re-solved**, not sign-flipped — a quaternion is not a coordinate —
and the solver was calibrated by reproducing the *old* directions from the *old*
quaternions before being trusted with new ones. The editor's demo quad was
backwards-wound all along (`{0,1,2,0,2,3}` against a −Z normal, hidden by
`doubleSided`) and comes out correct, because the mirror it was wrong by is the
one that went away.

**Every π-yaw light compensation is deleted.** A lamp travels down its local −Z
(glTF's `KHR_lights_punctual`) and the camera now looks down its own −Z, so an
identity lamp beside an identity camera lights exactly what the camera sees.
`lit_surface`'s half-turn, `extended_material`'s, and `lighting_stage`'s entire
`faceTheQuad` flag are **gone** rather than defaulted off. `textured_surface`
keeps a rake but negates its yaw with the half-turn's removal, because dropping
the turn alone moved the light to the other side of the subject (travel x
−0.49 → +0.49) and would have moved every recorded value for no reason.

### 1. The chirality probe measures something other than what the brief expected

**"World +X lands on screen right" is true under *both* conventions and is not
the mirror.** `_11` stays positive through a handedness change — 165.1 pins
exactly that — so the probe's assertions do **not** flip, and simply moving its
glyph would have left a test that cannot distinguish the two conventions at all.
The mirror was in the *pairing* of that with the camera's forward: a physical
observer's right hand is `cross(forward, up)`, which is −X with `forward = +Z`
and +X with `forward = −Z`. A rendered image cannot see a forward vector.

So the probe anchors to the one thing that is both authored in the content and
visible on screen: **which side of the surface you are looking at.** The glyph
is now **single-sided**, authored the way a DCC tool exports a readable face
(+Z normal, `cross(v1−v0, v2−v0)` along +Z), and its coverage floor is half the
verdict — it renders only if the camera is on its authored front side. Both
halves together are "a Blender artist's asset arrives as authored", and under
the old convention the same asset placed in front of the camera showed its back,
which `CULL_MODE_BACK` discards.

The file used to be double-sided on the grounds that *"facing and lighting are
T0152's other probes, and this one asks only where positions land"*. That
separation is exactly what left it unable to tell the two conventions apart.

### 2. Three gpu failures were the scene, one was a synthetic normal, one was the fix

Of 65 cases, **three** failed after the sweep, and none of them was the engine:

- `lighting_stage`'s cel-shading dome returned
  `normalize(float3(u, v, -1.0))` — a synthetic normal written as a literal,
  facing the old camera. Every `dot(S.Normal, L.ToLight)` clamped to zero and
  the smooth control collapsed to one colour. That is what a literal normal
  costs when the convention moves.
- `rockcube_sample` asserted `direction.z > 0` for "travelling away, from the
  camera's side". Now `< 0`; the sentence beside it did not change.
- The self-shadow case's posed yaw. **Solved, not swept:** at −0.45 the key
  sits 11.5° above the *same object face's* horizon (`N·L = 0.200`, against
  0.758 on the well-lit face) as it did at +0.45 before, so it is the same
  patch of the same height map and not merely something that also works. Left
  at +0.45 the key stood 28° off every visible face, nothing occluded, and the
  case measured a mean difference of 0.0001 with zero darkened pixels — a real
  failure, correctly reported, of the scene rather than of the march.

### 3. The tangent-frame sign — the finding that cost the most and shipped as a revert

Schüler's cotangent frame — `HpParallaxUv`, and `rock_pom.slang`'s copy of it —
is **odd in the normal**: flip `N` and both tangent vectors come back negated. A
handedness change moves which side of a surface faces the camera, so a
compensating `sign(dot(cross(ddx, ddy), N))` looks mandatory, and a derivation
was written for it that looked airtight.

**It was wrong, and the rock cube's self-shadow is what said so** — the only
directional consumer in the tree, because it marches *toward the light* and
darkens what is occluded. With the correction: max drop **0**. Without it:
**124.6**, 50 pixels darkened beyond 10 luminance, `meanOn < meanOff`. Reverted;
the frames ship exactly as they were before this ticket.

What the sign *is* is now measured rather than argued. A new gpu case
(`custom_shader_material_test`, "the screen basis's chirality against the
surface normal is pinned") renders `dot(cross(ddx(P), ddy(P)), N)` to a pixel:
**negative** on a camera-facing surface, where it was positive before T0165.
That case exists because the quantity is invisible from any source file — it
depends on which way `ddy` runs in framebuffer coordinates, which Diligent's
Vulkan backend decides by flipping the viewport internally — and because it
would move silently under a driver or upstream change with "every normal map and
every parallax effect rotated half a turn" as the symptom.

**The gap this exposed is a game-facing one and has a row now.** A game shader
that needs a tangent frame must rebuild it, and nothing in the contract tells it
about this sign: the engine knows the answer and has no way to say it.
`13-shader-capability-matrix.md` carries `HpTangentFrame(worldPos, uv, N, out T,
out B)` as an unowned row, with the hazard stated rather than the cost — per
D35, the row goes in before anything is built.

### `HpViewDepth` was not on anyone's list

It unprojects through `mProjInv` to a view-space Z, which is now negative in
front of the lens. Its contract says "distance along the view axis, in metres"
and `glass.slang` subtracts two of them. A silently negated depth does not look
like a depth bug — a `saturate` clamps it to zero and a contact fade simply
stops fading. Negated at the source so the contract holds, and
`screen_inputs_test` is what pins the sign (its `Surface::z` is now documented
as a *distance*, negated at placement, so every "the pane is at 5 and the wall
at 6, so the gap is 1 m" comment in that file stays literally true — and those
are the same metres `HpViewDepth` returns).

## Measured 2026-08-07 — the run, and every changed value

**NVIDIA GeForce RTX 2080, Vulkan, both targets** (Linux native on x11, the
Windows suite under wine — a plain Linux development box, so wine is the correct
and only answer there, not a fallback). `zig build all` exit 0 with zero
`^FAILED:|error:` lines; `docs` clean.

| Suite | Before | After |
|---|---|---|
| fast | 323 | **324** (+1: the gate case) |
| integration | 92 | **92** |
| gpu | 65 / 1552 assertions | **66 / 1556** (+1: the screen-chirality measurement) |

**Byte-identical across the change, which is the strongest evidence the sweep
was a true mirror**: the lit red quad `(242, 25, 25)`, its 0.5-intensity
`(107, 5, 5)`, the point light's `(255, 39, 39)` / `(90, 4, 4)` / `(0, 0, 0)`,
the spot's `(255, 58, 58)` / `(0, 0, 0)`, the material-assignment exact greens
and checkerboard counts, the base-colour channel `(116, 108, 69)` var 16.382,
and the cube's frame coverage 18.079%. Same incidence, same view, same BRDF.

**Everything that moved, and why:**

| measurement | before | after | why |
|---|---|---|---|
| `present_blit` source luma, top band | 27.6 | **219.2** | The scene's lamp has an identity transform. Before, its −Z and the camera's +Z disagreed and the quad was lit **from behind**; now an identity lamp lights what an identity camera sees. The test deliberately does not assert which band is bright — only that they differ — and the gap widened. **This is the change, rendered.** |
| mesh-normal debug view | (188, 188, **0**) | (188, 188, **255**) | The debug view encodes `N * 0.5 + 0.5`. The quad's authored normal moved from −Z to +Z, so blue goes 0 → 255. The view reporting it faithfully is the point of the view. |
| shading-normal debug view | (186, 183, 31) var 12.95 | (186, 188, 252) var 13.71 | Same cause; the variation is the normal map's perturbation about a normal that changed sign. |
| rock cube, four-yaw luminance | 96.9 … 101.3 | 87.4 … 93.4 | The scene's lights mirrored in z, so the camera sees different *faces* of the cube at each yaw — different UV islands of the same texture. **The assertion is the ratio** (basis consistency across yaws, `high/low < 1.25`): 1.046 → 1.068, still far inside. This is the test that once caught a 5× per-face basis error. |
| rock / metal channel variation | 18.56 / 7.98 | 20.77 / 8.15 | Same cause — different faces lit. |
| self-shadow: darkened / max drop / mean diff | 85 / 55.6 / 0.0505 | 50 / 124.6 / 0.0399 | The grazed face and its incidence are identical (0.200); the *other* visible face changed from object −Z to object +Z, so the frame around the shadow differs. Black speckle 431 → 15, for the same reason. Bounds (>30, >20) hold with margin. |
| glass pane centre | (70,70,31) vs (53,45,22) | (27,29,8) vs (35,33,23) | The pane refracts the cube behind it, which is lit differently; and its contact fade now computes on a **positive** clearance, which without the `HpViewDepth` fix would have clamped to zero. Footprint 5.748% → 5.753% — the same geometry. |
| depth fade over the wall | 188 | 187 | One LSB. Same 1 m gap, same arithmetic, different rounding through the negated `HpViewDepth`. |
| reference-plane difference | 25.16 / 33207 px | 20.72 / 29737 px | The rock cube is posed differently; the assertion is a floor and holds. |
| triplanar parallax displacement | 15.47 / 7.38 / 3.35 | 16.42 / 7.36 / 3.58 | Terrain and oriented quads mirrored in z; the assertions are thresholds on displacement magnitude. |
| sway silhouette | gained 1177, lost 1346 | gained 1294, lost 1139 | The vertex hook leans the cube's top; which side that reaches on screen follows the pose. |
| cooked archive bytes | 14.4M / 16.2M | 20.9M / 22.8M | **Not this change.** The archive is cooked from the developer cache, which persists on disk and accumulates across runs; this ticket ran the suite five times. Nothing asserts the size. |
| light-loop and signature timings | — | — | Noise; all within their existing bounds. |

## Notes / findings

- **The brief's expectation about the chirality probe was wrong, and so was
  mine.** Both assumed its assertions would invert. They do not, because a
  handedness change is not a screen-space mirror. Finding that out is what
  turned the probe from a test that would have silently passed either way into
  one that can fail.
- **The tangent-frame correction is the ticket's real cost**, and it shipped as
  a revert plus a test. The derivation was checked numerically against the
  asset's own UV invariant (`cross(dP/du, dP/dv) == N`, which
  `rockcube_mesh_test` asserts) and *still* disagreed with the hardware — which
  is the project's own rule about measuring rather than reasoning, arriving from
  an unusually well-argued direction.
- **`Light.hpp` carried two different defaults for the same field**
  (`ResolvedPlacement::direction` = `{0,0,−1}`, `ResolvedLight::direction` =
  `{0,0,+1}`) under identical documentation. Harmless — every path assigns from
  `resolvePlacement` — and fixed on sight rather than left as a second answer to
  the same question.
- **The editor's demo quad had been backwards-wound since it was written**,
  surviving only because the demo material is double-sided. It is correct now by
  arithmetic rather than by luck (`cross((3,0,0), (3,3,0)) = (0,0,9)` against a
  +Z normal).

### Not verified, and not claimed

- **`fHandness` is left in a *stated* disagreement rather than a hidden one.**
  Diligent's comment says `cross(ddx, ddy) * fHandness` faces the viewer; with
  `cross` measured facing **away** (165.5), `−1` is the value that obeys the
  comment while `+1` is what their own "+1.0 for right-handed coordinate
  system" semantics call for. It is set to `+1`. Nothing in this engine reaches
  the branch that reads it — every mesh carries `NORMAL` and the importer
  refuses one that does not — so changing it on this reasoning alone would be
  guessing in the other direction. The screen-chirality case carries the
  measurement and names itself as where the next person starts. **The first mesh
  authored without normals is the trigger.**
- **Whether the reconstructed tangent frame is *absolutely* right, as opposed to
  right relative to what shipped before, is not established.** The evidence is
  that the uncorrected frame makes a directional effect (parallax
  self-shadowing) work and the corrected one makes it vanish; there is no test
  in the tree that asserts "T points along +u" directly. `HpTangentFrame` is
  where that would be settled once and for all, which is part of why the
  capability-matrix row exists.
- **A camera under a mirrored parent** still mirrors the whole view and is still
  not handled — `WindingConvention.hpp`'s item 5, unchanged from T0152.5. No
  scene here can author one; the first game that does is the trigger.
- **No editor frame was looked at.** The sample scene's re-solved quaternions
  are verified by `rockcube_sample_test`'s assertions (coverage, luminance
  ratios, light directions), not by a human looking at the rock cube. Running
  the editor is a GUI on somebody's desktop and was deliberately skipped.
