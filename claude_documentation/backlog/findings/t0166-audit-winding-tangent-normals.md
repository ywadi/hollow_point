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

# Winding / facing / handedness audit — HollowPoint

Scope searched: `engine/`, `apps/`, `samples/`, `tests/`, `tools/`, `docs/`, `claude_documentation/`, `CLAUDE.md`. Skipped `third_party/`, `build/`, `.harness/`.

**Headline finding first**, because it changes how you read everything below: the codebase models exactly **two** mirror sources (import mirror, per-node determinant) and hard-codes the belief that the projection contributes **zero**. A projection handedness flip is a *third*, unmodelled mirror. The `static_assert` at `WindingConvention.hpp:71` chains only the first two, so it will actively **fight** the correct fix (you need `kFrontFaceCounterClockwise = true` while `kImportMirrorsContent` stays `false`, and that assert refuses it).

---

## 1. `kFrontFaceCounterClockwise` / `kImportMirrorsContent` / `FrontCounterClockwise` / `WindingConvention`

### The single source of truth
| Path:line | What | Why affected |
|---|---|---|
| `engine/include/hp/WindingConvention.hpp:56` | `inline constexpr bool kFrontFaceCounterClockwise = false;` | **Must become `true`.** The whole derivation in its doc comment ("the left-handed projection has no XY mirror … a glTF front face arrives at the rasteriser clockwise as displayed") is exactly the premise the RH switch invalidates. |
| `.../WindingConvention.hpp:69` | `inline constexpr bool kImportMirrorsContent = false;` | Stays `false` (importer still converts nothing) — but see next row. |
| `.../WindingConvention.hpp:71-74` | `static_assert(kFrontFaceCounterClockwise == kImportMirrorsContent, ...)` | **This assert will break the correct fix.** It encodes "the only mirror is the importer's". With a projection mirror the invariant becomes `kFrontFaceCounterClockwise == (kImportMirrorsContent XOR kProjectionMirrors)`. The assert needs a third term or the header needs a `kProjectionIsRightHanded` constant. |
| `.../WindingConvention.hpp:20-27` | Prose: "Measured through this engine's chain (T0152.1) … the left-handed projection has no XY mirror, reverse-Z touches only the Z column" | The measured chain is re-measured. Every sentence about "zero winding reversals" becomes false. |
| `.../WindingConvention.hpp:29-55` | The five-item "must agree" list (rasterizer flag, cull BACK, two-sided flip, shadow bias, mirror compensation) | This is your checklist; item 5 ("a mirror introduced anywhere between authored space and the framebuffer") is the item the projection change trips, and it explicitly says the flag must toggle with it. |

### Consumers of the constant
| Path:line | Why affected |
|---|---|
| `engine/src/SurfacePipeline.cpp:10` (include), `:1100-1101` | The **only** surface PSO's `RasterizerDesc.FrontCounterClockwise` — every scene draw. Picks up the flip automatically once the constant flips. |
| `engine/src/Render.cpp:7` (include), `:353-354` | Fullscreen-blit PSO. `CULL_MODE_NONE`, so behaviourally inert, but it is the declared-not-defaulted precedent. |
| `tests/fast/rockcube_mesh_test.cpp:26, :310-311` | `CHECK_FALSE(hp::kFrontFaceCounterClockwise); CHECK_FALSE(hp::kImportMirrorsContent);` — **hard-fails** the moment you flip the constant. See §7. |
| `tools/make_cube_gltf.py:18-21` | Docstring asserts both constants are false and derives the asset's authoring rule from that. Rule itself (glTF right-hand rule) survives; the *justification* text doesn't. |

### Documentation carrying the now-stale derivation
- `CLAUDE.md:28`, `:348-387` — the "Traps that will cost you an hour" section; `:359-363` asserts "zero winding reversals"; `:369-371` mandates `{0, 2, 1, 0, 3, 2}` for a −Z-facing quad; `:375-378` explains away Diligent's `FrontCounterClockwise = true` via GLTFViewer's `InvYAxis` mirror — **that explanation inverts**: under RH you become the case that needs `true`.
- `docs/api/WindingConvention.md:1-80` — generated doc, regenerate.
- `docs/api/index.md:72, :786-787` — symbol index.
- `claude_documentation/documentation/06-engine-conventions.md:333-388` — the "Winding, facing and chirality" section, including the chirality claim and the "recorded options" for the mirror decision.
- `claude_documentation/documentation/02-decision-log.md:2022-2130` — **D33 itself**. `:2055` (Vulkan passthrough), `:2071-2077` (why `true` was rejected), `:2113-2115` (the two constants + static_assert), `:2129` (`CULL_MODE_BACK` means what the spec means).
- `claude_documentation/backlog/inprogress/0152-winding-convention.md` — the live ticket: `:61-62` (viewport/flag trace), `:85` (GLTFViewer), `:127`, `:160` ("flips apparent winding once and requires `kFrontFaceCounterClockwise` to…"), `:167-168`, `:207-209`, `:229`, `:249`, `:315`, `:357-395` (header verbatim), `:460`, `:469`, `:498-499`. This ticket is *open on exactly this decision*.
- `claude_documentation/backlog/README.md:285` — states the remaining work is "the owner's answer to the mirror question".
- `claude_documentation/backlog/open/0086-shadows.md:12` — shadow-bias policy inherits the convention; `claude_documentation/backlog/open/0045-culling-and-render-queues.md:11` — queue cull-mode selection.
- `claude_documentation/backlog/completed/0141-custom-shader-materials.md:1156-1181`, `:587-590`; `claude_documentation/backlog/completed/0028-scene-draw-submission.md:163`; `claude_documentation/backlog/completed/0134-pbr-renderer-adoption.md:112`.

---

## 2. `CULL_MODE_` uses and every rasterizer state construction

| Path:line | Construct | Why affected |
|---|---|---|
| `engine/src/SurfacePipeline.cpp:1095` | `RasterizerDesc.CullMode = key.GetCullMode();` | The one real rasterizer state. Cull face is chosen per draw upstream (row below); this just spends it. |
| `engine/src/SurfacePipeline.cpp:1100-1101` | `RasterizerDesc.FrontCounterClockwise = hp::kFrontFaceCounterClockwise;` | Where the flip lands. |
| `engine/src/SceneRenderer.cpp:1393-1398` | `det3` of node matrix upper-3×3 → `singleSidedCull = det3 < 0 ? CULL_MODE_FRONT : CULL_MODE_BACK` | **Determinant rule, CPU half.** This is *node*-determinant only. If the projection flip is compensated by flipping `FrontCounterClockwise`, this stays correct as-is. If instead you compensated at import (`kImportMirrorsContent = true`), this logic and the shader half must both re-derive. |
| `engine/src/SceneRenderer.cpp:1550-1563` | `PSOKey{..., doubleSided ? CULL_MODE_NONE : singleSidedCull}` | Main draw path. |
| `engine/src/SceneRenderer.cpp:1595-1602` | Same key for the error-shader fallback draw | Must stay in step with the main path. |
| `engine/src/SceneRenderer.cpp:1546-1548` | `doubleSided` resolution (missing-material inherits imported sidedness) | Decides whether cull mode is consulted at all. |
| `engine/src/SceneRenderer.cpp:330` | `gltf.DoubleSided = material.doubleSided;` | Authored `Material` → Diligent material sidedness. |
| `engine/src/Render.cpp:349` | `RasterizerDesc.CullMode = CULL_MODE_NONE` (blit) | Inert but declared. |
| `engine/src/ShaderSources.cpp:320` | `PSOKey{..., CULL_MODE_BACK}` in `buildEngineSurfacePipeline` (validation/warm key) | Hard-codes BACK as "the single-sided default"; still correct under the flag-flip fix, wrong under an import-mirror fix. |
| `tests/gpu/module_signature_cost_test.cpp:452` | `info.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;` | **The one rasterizer state in the tree that does NOT declare `FrontCounterClockwise`** — violates WindingConvention rule 1. Harmless today (CULL_NONE), but it's the drift vector the header warns about. |
| `tests/gpu/lit_surface_test.cpp:284` | comment: mirrored-node case relies on `CULL_MODE_NONE` | |
| `tests/gpu/material_assignment_test.cpp:348-349` | comment: default `doubleSided = false` → `CULL_MODE_BACK`, exact green only if facing is right | This test is the CPU-half determinant probe. |

There is **no shadow pass yet** (T0086 open), so no second cull-mode policy site exists — but `claude_documentation/backlog/open/0086-shadows.md:12` records that cull-as-bias-policy must apply the same per-draw flip.

---

## 3. Every place triangle indices are authored (with the exact winding)

**Convention in force:** camera-facing quad with authored `NORMAL = (0,0,-1)` over corners BL, BR, TR, TL winds `{0, 2, 1, 0, 3, 2}`.

### Generators / assets
| Path:line | Indices | Corner order & normal | Verdict |
|---|---|---|---|
| `tools/make_cube_gltf.py:99` | `FACE_INDICES = [0, 1, 2, 0, 2, 3]` | corners `(-t-b), (+t-b), (+t+b), (-t+b)`, with `cross(t,b) == n` (`:121-125`) → `cross(v1-v0, v2-v0) = 4·half²·n`, outward | **glTF-conformant.** Asserted per triangle at `:181-191`. |
| `tools/make_cube_gltf.py:88-96` | face basis table; bitangent pinned to world-down on the four sides | | Drives tangent-frame handedness — see §5. |
| `samples/rockcube/content/models/cube.gltf` + `cube.bin` | committed output of the above; `"doubleSided": false` at `:39` | | Object-space winding is projection-independent, so **the bytes need no change**; what changes is which side the rasterizer calls front. |

### C++ authored geometry — the `{0, 2, 1, 0, 3, 2}` cohort (all quads, BL/BR/TR/TL, normal −Z)
All of these are **conformant today** and all of them place geometry at **positive z** viewed by a camera looking **+Z**, which is the LH assumption:

- `tests/gpu/lit_surface_test.cpp:109` (comment `:105-108`)
- `tests/gpu/textured_surface_test.cpp:143` (comment `:140-142`)
- `tests/gpu/scene_draws_mesh_test.cpp:96` (comment `:93-95`; `:79` "the engine is left-handed and a default camera at the origin looks down +Z")
- `tests/gpu/material_assignment_test.cpp:116` (comment `:113-115`)
- `tests/gpu/present_blit_test.cpp:118` (comment `:115-117`)
- `tests/gpu/render_stack_composites_test.cpp:140` (comment `:137-139`)
- `tests/gpu/extended_material_test.cpp:100`
- `tests/gpu/triplanar_test.cpp:107` (comment `:105-106`)
- `tests/gpu/parallax_test.cpp:122` (comment `:120-121`)
- `tests/gpu/cooked_shaders_test.cpp:99` (comment `:88`)
- `tests/gpu/custom_shader_material_test.cpp:123` (comment `:91`)
- `tests/gpu/lighting_stage_test.cpp:120` (comment `:102-104` explicitly names the trap)
- `tests/gpu/screen_inputs_test.cpp:125` (comment `:107-109`)
- `tests/gpu/module_signature_cost_test.cpp:609`
- `tests/gpu/chirality_test.cpp:132` — `for (const std::uint16_t i : {0, 2, 1, 0, 3, 2})`, three rects, normal `(0,0,-1)`, plane **z = +6**

> Why affected: winding is authored against the *authored normal*, which is projection-independent — so the index arrays themselves stay valid. What breaks is the **placement**: every one of these sits at positive z with a camera looking +Z. A RH camera looking −Z renders **nothing** at all in all of them unless z is negated (and normals flipped to `(0,0,+1)`, which then re-flips the required index order back to `{0,1,2, 0,2,3}`).

### C++ authored geometry — the `{0, 1, 2, 0, 2, 3}` cohort
| Path:line | Detail | Verdict |
|---|---|---|
| `apps/editor/src/EditorMain.cpp:281` | `{0, 1, 2, 0, 2, 3}` over BL,BR,TR,TL at z = 4 with `NORMAL = (0,0,-1)` (`:277-280`), material `"doubleSided":true` (`:292`) | **Backwards-wound, latent.** This is exactly the pre-T0152 asset bug that survived only because it is double-sided. It contradicts `CLAUDE.md:369-371`. Under RH it stays backwards (and additionally sits behind the camera). |
| `tests/gpu/triplanar_parallax_test.cpp:224` | `{0, 1, 2, 0, 2, 3}` but corners are `(-t-b),(+t-b),(+t+b),(-t+b)` with `b = cross(n, t)` (`:203-204`) → `cross(v1-v0,v2-v0) = +n` | **Conformant** (different corner order than the −Z quads). Comment at `:221-223`. |
| `tests/gpu/triplanar_parallax_test.cpp:315-316` | terrain grid `{v00, v01, v10}` + `{v10, v01, v11}`, comment "cross(v01-v00, v10-v00) = +Y" | **Conformant**, front faces up. Spans z ∈ [4, 16] — a +Z-looking camera assumption. |
| `tests/gpu/asset_import_test.cpp:113` | `{0, 1, 2}` over `(0,0,0),(1,0,0),(0,1,0)` with `NORMAL = (0,0,+1)` | **Conformant** (`cross = +Z`). Never rasterized — import-only test. |
| `tests/fast/third_party_libraries_test.cpp:63` | `{0,1,2, 2,3,0, 4,5,6, 6,7,4}` | meshoptimizer plumbing only; no rendering, no facing meaning. |

---

## 4. Determinant / mirror / handedness / chirality

| Path:line | What | Why affected |
|---|---|---|
| `engine/src/SceneRenderer.cpp:1378-1398` | The glTF determinant rule, CPU half — full comment + `det3` expansion + `singleSidedCull` | Per-*node* determinant. Correct under a flag-flip fix; must be re-derived if the compensation goes anywhere else in the chain. |
| `engine/shaders/HpSurface.slang:1556-1572` | Shader half: `if (determinant((float3x3)g_Primitive.Transforms.NodeMatrix) < 0.0) gltfFrontFace = !gltfFrontFace;` | Same. Note it deliberately reads the node matrix (not a CPU flag) so the halves can't drift — a projection-level mirror is **not visible to this expression**, which is precisely why it must be handled by `kFrontFaceCounterClockwise`. |
| `engine/src/SceneRenderer.cpp:1961-1962` | `camera.fHandness = -1.0F;` // "Left-handed, matching the engine's convention (T0056/T0130)" | **Must become `+1.0F`.** DiligentFX's `GetPerturbNormalInfo` uses `fHandness` to orient the reconstructed tangent frame; leaving it at −1 under a RH projection inverts normal-map lighting everywhere. High-risk, easy to miss. |
| `engine/shaders/HpSurface.slang:1221`, `:1275` | `GetPerturbNormalInfo(In.WorldPos, geometricNormal, isFrontFace, g_Frame.Camera.fHandness)` | The two consumers of that value (base normal + clearcoat normal). |
| `engine/include/hp/Camera.hpp:398-401` | "**Left-handed, +Z into the screen**, which is Diligent's camera-space convention and not ours to reopen" | The doc being reopened. |
| `engine/src/Camera.cpp:59-87` | `projectionMatrix` — `float4x4::Projection(...)` / `float4x4::Ortho(...)`, Diligent's **LH** helpers; `kNegativeOneToOneZ = false`; reverse-Z via swapped near/far | **The change site.** Diligent's `Projection`/`Ortho` are LH-only; a RH projection means either negating the Z column/row or a different helper. Note a fast test pins this matrix "byte-for-byte against Diligent's own helper" (`:64-67`) — that test will need rewriting. |
| `engine/include/hp/Math.hpp:70-74` | Row-major, `World * View * Proj`; "getting it backwards is the single most common way a transform ends up mirrored" | Multiplication order interacts with any hand-rolled RH projection. |
| `engine/src/Scene.cpp:401-407` | `Scale * Rotation * Translation * parent`; "Reversing this is the classic way a hierarchy comes out mirrored" | Feeds the node matrix whose determinant both halves read. |
| `engine/src/CameraSystem.cpp:238` | hard refusal because "world-space marker ends up mirrored behind the player" | Screen-projection helper; `:234` `world * viewProjection`, `:272` inverse-unproject — both change meaning under RH. |
| `tests/gpu/chirality_test.cpp` | whole file — see §8 | |
| `tests/gpu/lit_surface_test.cpp:281-316` | "a mirrored node shades like the unmirrored one — the determinant rule"; `(-1,1,1)` entity scale, double-sided | GPU probe of the shader half. |
| `tests/gpu/material_assignment_test.cpp:175-176`, `:391-427` | "a mirrored node keeps its single-sided faces"; exact-green assertion through `CULL_MODE_BACK`/`FRONT` | GPU probe of the CPU half. Exact-pixel — the most brittle assertion in the set. |
| `tests/fast/camera_system_test.cpp:16`, `:93`, `:392` | "looking down +Z (the engine is left-handed)"; "must land at positive view-space z"; mirrored-marker case | Direct LH assertions. |
| `tests/gpu/scene_draws_mesh_test.cpp:79` | "The engine is left-handed and a default camera at the origin looks down +Z" | |
| `engine/src/Light.cpp:25-33`, `engine/include/hp/Light.hpp:20`, `:130-132`, `:158-160` | glTF's "light points along **negative Z**" convention, taken from row 2 of the world matrix and negated | Already glTF/RH-native. Under an LH world this is the one place already speaking RH — worth re-checking it isn't double-compensating once the projection agrees. |

---

## 5. Tangent frames, normal maps, facing

### Derivative-reconstructed tangent frames (no vertex tangents)
| Path:line | What | Why affected |
|---|---|---|
| `engine/shaders/HpSurface.slang:449-482` | `HpParallaxUv`: `dp1=ddx(WorldPos)`, `dp2=ddy(WorldPos)`, `dp2perp=cross(dp2,N)`, `dp1perp=cross(N,dp1)`, `T = dp2perp*duv1.x + dp1perp*duv2.x`, `B = ...` | **Screen-space derivative frames are the classic handedness casualty.** `ddx`/`ddy` are framebuffer-space; a determinant flip in the object→framebuffer chain reverses the sign of the (ddx, ddy) basis, flipping the reconstructed `T`/`B` handedness. Result: relief marches the wrong way / normal-map green channel inverts. |
| `engine/shaders/HpSurface.slang:458-469` | Comment: N forced toward viewer explicitly, *not* by `SV_IsFrontFace`; cites T0152's correction | Reasoning text tied to the current chain. |
| `engine/shaders/HpSurface.slang:471-481` | `float3 viewTS = float3(dot(T,ViewDir), dot(B,ViewDir), dot(N,ViewDir));` → `HpParallaxMarch` | The march consumes the possibly-flipped frame. |
| `samples/rockcube/content/shaders/rock_pom.slang:276-305` | `tangentFrame()` — a verbatim copy of the above, including `cross(dp2,N)` / `cross(N,dp1)` | Same exposure, in a *sample* shader, i.e. the pattern game authors will copy. `:281-283` comment even says "in this engine hardware facing and geometric facing disagree" — that sentence is a leftover from the pre-T0152 misdiagnosis and is already wrong; it gets more wrong. |
| `samples/rockcube/content/shaders/rock_pom.slang:313`, `:248-260` | frame cached into `frameT/frameB/frameN`, reused by the self-shadow march | |
| `samples/rockcube/content/shaders/rock_pom.slang:400-445` | `lightTS = float3(dot(frameT,toLight), dot(frameB,toLight), dot(frameN,toLight))`, `reach = lightTS.xy / lightTS.z` | Self-shadow direction lives in the reconstructed frame; a flipped `T`/`B` shadows from the mirrored direction. |
| `engine/shaders/HpSurface.slang:560-605` | Triplanar parallax: frames are **constant world axes** (`tangent = world X, bitangent = world Y, normal = world Z`), `abs()` on the normal component | The one frame construction that is **immune** — it never touches `ddx`/`ddy` for the basis, only for mip gradients. Useful control case for diagnosing. |
| `engine/shaders/HpSurface.slang:382-388` | `HpParallaxMarch` contract: "view vector already rotated into the projection's tangent frame" | |

### Facing
| Path:line | What | Why affected |
|---|---|---|
| `engine/shaders/HpSurface.slang:2283` | `in bool IsFrontFace : SV_IsFrontFace` (PS entry) → `:2304` `evaluateSurface(material, VSOut, IsFrontFace)` | `SV_IsFrontFace` is defined *by* `FrontCounterClockwise`. If the flag flips with the projection, this stays correct; if the flag is left at `false`, every fragment reports inverted facing. |
| `engine/shaders/HpSurface.slang:1554-1572` | `gltfFrontFace` derivation (node determinant XOR) | |
| `engine/shaders/HpSurface.slang:1605-1628` | The two-sided flip: `surfaceIn.Normal = geometricNormal * (gltfFrontFace ? 1.0 : -1.0)`, with the long comment about flipping twice | **The single highest-consequence line.** A wrong `gltfFrontFace` here produces pure black on lit surfaces (documented at `:1613-1620`), not an obviously-wrong image. |
| `engine/shaders/HpSurface.slang:1629-1635` | no-normals fallback `surfaceIn.Normal = float3(0.0, 0.0, -1.0)` // "facing the camera" | **−Z is "toward the camera" only under LH.** Under RH (camera looks −Z) this becomes `(0,0,+1)`. |
| `engine/shaders/HpSurface.slang:1204-1236` | `HpStandardMaterial::shadingNormal` — takes `isFrontFace`, calls `GetPerturbNormalInfo(..., fHandness)` then `PerturbNormal(normalInfo, ddx(normalUv), ddy(normalUv), ...)` | Both the facing bit *and* `fHandness` *and* screen-space UV derivatives converge here. This is where a handedness error surfaces as "normal map lit from the wrong side". |
| `engine/shaders/HpSurface.slang:1264-1290` | `clearcoatNormal` — identical structure | |
| `engine/shaders/HpSurface.slang:1723`, `:1732` | call sites passing `gltfFrontFace` into both hooks | |

### Vertex tangents
| Path:line | What | Why affected |
|---|---|---|
| `engine/shaders/HpSurface.slang:1637-1650` | `surfaceIn.Tangent = float4(normalize(VSOut.Tangent), 1.0)` — **`w` hard-coded +1**, because Diligent's `VSTangentAttrib` is `VT_FLOAT32, 3` and glTF's handedness `w` never survives the loader | **Pre-existing latent bug, and RH makes it more likely to bite.** glTF authors bitangent handedness as `TANGENT.w = ±1`; the engine discards it and assumes `+1`. Mirrored UV islands already light wrong; a handedness change in the surrounding chain will not fix it and may unmask it. |
| `engine/shaders/HpSurface.slang:2153-2156`, `:2188`, `:2244-2247` | tangent passthrough VS→PS, `mul(vertexOut.Tangent, float3x3(Transform...))` | Object→world rotation of the tangent; note **no** inverse-transpose and **no** determinant compensation on the tangent (unlike the normal at `:72`). |
| `engine/shaders/HpSurface.slang:1996-2019` | Anisotropy basis: `anisoBasisBitangent = cross(anisoBasisTangent, shaded.Normal)`, then `AnisotropyBitangent = cross(shaded.Normal, AnisotropyTangent)` | Two `cross` calls define a chirality; if the tangent frame's handedness flips, the anisotropic highlight rotates 90°/mirrors. |
| `engine/shaders/HpMaterial.slang:494-501` | `HpVertexInput::Tangent` — "handedness `w` never reaches this engine (T0159.4…)" | Contract doc for the above. |
| `engine/shaders/HpMaterial.slang:633-656` | `HpSurfaceInput::Tangent` — "`w` reserved for the bitangent's handedness", "`bitangent = cross(Normal, Tangent.xyz)`", "a mesh whose UVs are mirrored (authored `w = -1`) will have its bitangent flipped … symptom (normal-mapped lighting inverted on mirrored islands)" | Names the exact symptom class the projection change can produce. |
| `engine/shaders/HpMaterial.slang:463-465` | "**Do not invert a triangle.** D33 is binding: `cross(v1-v0, v2-v0)` points…" — the vertex hook's contract, referencing `FrontCounterClockwise = false` | Contract text pinned to the current flag value. |
| `engine/shaders/HpMaterial.slang:729-730`, `:771`, `:786-788`, `:1077-1087` | `shadingNormal`/`clearcoatNormal`/anisotropy-direction/`AnisotropyTangent`/`AnisotropyBitangent` contract fields | |
| `docs/shaders/IHpMaterial.md:81`, `:165`, `:174-179`, `:208`, `:219` | Public game-author documentation of `isFrontFace` = "glTF facing, not raw `SV_IsFrontFace`" and the winding prohibition | Must be regenerated/re-argued. |

### Asset-side tangent-frame handedness (no `TANGENT` attribute anywhere)
- `tools/make_cube_gltf.py:37-56`, `:148-172` — UV islands oriented so `cross(dP/du, dP/dv) == N`; the "first cut used `(1 - sb)/2`, flipped handedness on all six faces" postmortem. **This invariant is object-space and survives the projection change** — but it is only *correct* if the shader's reconstructed frame has the matching handedness, which is what §5's `ddx`/`ddy` concern threatens.
- `tests/fast/rockcube_mesh_test.cpp:244-304` asserts it (§7).
- `tests/gpu/rockcube_sample_test.cpp:1027-1046` — the four-yaw luminance test, `CHECK(high/low < 1.25)`, which caught a 5× basis inconsistency. **This is your best empirical detector** for a tangent-handedness regression after the switch.
- No `TANGENT` attribute exists in any asset; `USE_VERTEX_TANGENTS` is only reachable for a mesh that carries them (none do today).

---

## 6. Every shader file, and what it assumes

| Path | Assumptions |
|---|---|
| `engine/shaders/HpSurface.slang` (~2300 lines) | (a) `SV_IsFrontFace` semantics defined by `FrontCounterClockwise = false` (`:2283`); (b) node-determinant XOR is the *only* mirror (`:1569`); (c) `fHandness = -1` (LH) fed to `GetPerturbNormalInfo` (`:1221`, `:1275`); (d) `(0,0,-1)` is camera-facing (`:1633`); (e) `ddx`/`ddy` frames have a fixed handedness (`:472-473`); (f) tangent `w = +1` always (`:1645`); (g) normals rotated by the primitive's `NormalMatrix` (inverse-transpose, `:72`) while tangents are rotated by the plain 3×3 (`:2244`). |
| `engine/shaders/HpMaterial.slang` | Contract/doc only, no executable facing logic. Assumes `FrontCounterClockwise = false` (`:464`), that the vertex hook may not invert a triangle (`:463`), that `Tangent.w` is meaningless (`:499`, `:648-655`), that `bitangent = cross(Normal, Tangent.xyz)` (`:650`). |
| `samples/rockcube/content/shaders/rock_pom.slang` | Rebuilds the derivative tangent frame itself (`:284-305`); orients N toward viewer rather than by `SV_IsFrontFace` (`:281-283`, with an already-stale justification); light and view marches both live in that frame (`:313`, `:400-445`). **Most exposed of the four.** |
| `samples/rockcube/content/shaders/glass.slang` | No facing, tangent, cross-product, or normal-map logic — grep returns nothing. **Unaffected.** |

`tests/fixtures/upstream_shading.pinned` + `tools/pin_upstream_shading.py` + `tests/fast/upstream_drift_test.cpp` pin the mirrored DiligentFX light loop line-for-line; they don't encode handedness but will fail loudly if you edit `HpSurface.slang`'s mirrored regions (`:781-1000`, `:1441-1500`) while fixing something.

---

## 7. `tests/fast/rockcube_mesh_test.cpp` — exactly what it asserts

Path: `tests/fast/rockcube_mesh_test.cpp`. Reads the **committed** `samples/rockcube/content/models/cube.{gltf,bin}` (never regenerates — `:17-20`). Four test cases:

**1. "the rock cube's faces wind outward, per triangle"** (`:104`)
- Layout guards `:116-119`: `"byteStride": 32`, `POSITION: 0`, `NORMAL: 1`, `TEXCOORD_0: 2`.
- `:125` — `CHECK(text.find("TANGENT") == std::string::npos)` — **asserts no vertex tangents exist**, deliberately, "if this starts failing, something began requiring tangents".
- `:127` — `CHECK(text.find("\"doubleSided\": false")...)` — single-sided, "which is what makes winding observable at all".
- **Winding, `:148-166`:** for all 12 triangles, `area = cross(v1-v0, v2-v0)`, then
  - `:160` `CHECK(dot(area, v0) > 0.0F)` — area points **away from the origin** (outward). Deliberately checked against the *position* not the normal, "checking against the normal alone would pass for a cube whose normals are also inverted".
  - `:165` `CHECK(dot(area, n) > 0.0F)` — **the authored normal agrees with the winding**, explicitly "which is what the two-sided flip in HpSurface.slang assumes (D33, point 3)".
- `:169-182` extent/centre; `:184-202` six hard-normal faces, axis-aligned, 24 vertices not 8.

**2. "each face carries its own 0..1 UV island"** (`:205`) — per-face UV min/max exactly 0 and 1.

**3. "the UV frame has the handedness a tangent-space normal map needs"** (`:244`)
- Solves `dP/du`, `dP/dv` from the four corners (`:270-292`).
- `:302` — **`CHECK(dot(cross(dPdu, dPdv), n) > 0.0F)`** — "Positive, not merely non-zero: anti-aligned is the failure being guarded against, and it has the same magnitude as correct."
- Header comment `:244-260` records the real bug it exists for: v along `-b` flipped handedness on all six faces, relief lit from the wrong side, caught **by eye**, no other assertion could see it.

**4. "the cube's winding rule is the one the engine declares"** (`:306`)
- `:310` `CHECK_FALSE(hp::kFrontFaceCounterClockwise);`
- `:311` `CHECK_FALSE(hp::kImportMirrorsContent);`
- Comment `:307-309`: "If either of these moves, the asset above is wrong and this test is the thing that says so".

**Impact of the RH switch:** cases 1–3 assert *object-space* invariants and are **projection-independent — they should still pass unchanged**. Case 4 **will fail immediately** on `CHECK_FALSE(kFrontFaceCounterClockwise)`. Note the test's own framing ("if either of these moves, the asset is wrong") is the trap: under a *projection* mirror the constant moves and the asset is still **right**. This test encodes the two-mirror model and needs the same third term the `static_assert` does.

---

## 8. `tests/gpu/chirality_test.cpp` — exactly what it measures and asserts

Path: `tests/gpu/chirality_test.cpp`.

**Setup**
- Generates an asymmetric **"F" glyph** (three axis-aligned rects) into a temp glTF at `writeGlyphGltf` (`:110-187`). All geometry in the **z = +6** plane (`:130`), normals `(0,0,-1)`, wound `{0, 2, 1, 0, 3, 2}` (`:132`).
- Stem `x ∈ [-2,-1], y ∈ [-3,3]`; top arm `x ∈ [-1,2], y ∈ [2,3]`; mid arm `x ∈ [-1,1], y ∈ [0,1]` (`:104-106`). **Every row shares its world −X edge; arms extend toward +X.**
- Material: **unlit, double-sided, green** (`:231-234`, `:163`) — "facing and lighting are T0152's *other* probes, and this one asks only where positions land" (`:32-33`).
- Camera: default `hp::Camera{}` at origin, **looking +Z**, 60° vFOV (`:99-102`, `:239`). 128×128 target, black clear.

**Measurement** (`:265-331`)
- Green-pixel histogram per readback row → `RowSpan{count, minX, maxX}`.
- `REQUIRE(total > 1500)` (`:280`) — coverage floor; also the magenta-error-shader guard.
- `minWidth` = narrowest row (the 1-unit stem, `:293-299`).
- Rows split into **narrow** (stem only) vs **wide** (stem + arm) at 1.8× `minWidth` (`:307`).
- `lowColumnSpread = max(minX) - min(minX)` across rows; `highColumnSpread = max(maxX) - min(maxX)` (`:314-323`).
- `narrowMeanRow` / `wideMeanRow` — mean readback row of each class.

**Assertions**
- `:342` `CHECK(lowColumnSpread < minWidth / 2 + 2)` — all rows' **left** edges align.
- `:343` `CHECK(highColumnSpread > (minWidth * 3) / 2)` — the **right** edges vary.
  Together: stem on screen left, arms extending right ⇒ **world +X lands on screen right** ⇒ the display is **chirally mirrored in X** (a physical observer facing +Z has +X on their left). `:333-340` explicitly notes an unmirrored display fails these "in the opposite pattern (varying low, aligned high), which is what makes this a measurement and not a tautology".
- `:352` `CHECK(wideMeanRow < narrowMeanRow)` — arms (world y > 0) at *smaller* readback rows ⇒ **world +Y lands on screen top**; vertically unmirrored. Row-0-is-top is inherited from `present_blit_test`'s measurement (`:26-30`; see `tests/gpu/present_blit_test.cpp:242-285`).

**Impact of the RH switch — this test is the primary oracle and it will fail in two stages:**
1. The glyph is at **z = +6**; a RH camera at the origin looks **−Z**, so the glyph is behind the camera → zero green pixels → `REQUIRE(total > 1500)` at `:280` fails first, masking the real signal. **Move the glyph to z = −6 (and normals to `(0,0,+1)`) before reading anything into the result.**
2. With the glyph correctly placed, the **X mirror is exactly what the RH projection removes**. `:342` and `:343` should then **swap**: `highColumnSpread` becomes small and `lowColumnSpread` large. That inversion is the *success* criterion for the migration, and the test as written reports it as a failure. `:352` (world +Y → screen top) should be unchanged.

This test, plus `material_assignment_test.cpp:391` (CPU determinant half), `lit_surface_test.cpp:281` (shader determinant half), and `rockcube_sample_test.cpp:858` (cull-from-inside), are the four measurements that together verify the migration.

---

## Ranked risk list for the migration

1. `engine/include/hp/WindingConvention.hpp:71` — the `static_assert` **blocks the correct fix**; needs a third term for projection handedness.
2. `engine/src/SceneRenderer.cpp:1962` — `fHandness = -1.0F` is the easiest thing in the tree to miss; symptom is "normal maps lit from the wrong side", not "geometry missing".
3. `engine/shaders/HpSurface.slang:449-482` and `samples/rockcube/content/shaders/rock_pom.slang:284-305` — `ddx`/`ddy`-reconstructed tangent frames; handedness follows the framebuffer chain, not the object.
4. `engine/shaders/HpSurface.slang:1633` — `(0,0,-1)` no-normals fallback is LH-literal.
5. `apps/editor/src/EditorMain.cpp:281` — already backwards-wound (`{0,1,2,0,2,3}` with −Z normals), hidden by `doubleSided:true`.
6. Every gpu test's geometry at **positive z** with a **+Z-looking** camera — mechanical but ~16 files.
7. `tests/fast/rockcube_mesh_test.cpp:310-311` and `tests/gpu/chirality_test.cpp:342-343` — the two tests that assert the *current* answer and must be re-derived, not just re-baselined.
8. `engine/shaders/HpSurface.slang:1645` — `Tangent.w = +1` hard-code; a pre-existing latent bug in the same blast radius.