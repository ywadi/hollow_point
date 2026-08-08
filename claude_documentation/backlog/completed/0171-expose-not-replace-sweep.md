# T0171 — Expose, do not replace: sweep the backlog against what DiligentEngine already ships

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 458 |
| **Created** | 2026-08-08 |
| **Closed** | 2026-08-08, same day. Eleven tickets rescoped, two closed, one decision recorded (**D41**), one extended (**D35**), and the matrix made binding |
| **Blocked by** | [0170-diligent-owns-the-render-loop.md](../inprogress/0170-diligent-owns-the-render-loop.md) — **the blocker resolved, and not in the direction it predicted.** This ticket said every verdict changes *"once the engine is a `GLTF_PBR_Renderer` subclass"*. T0170 measured that it **cannot be** — the base class silently destroys the engine's resource signature and its `Render()` draws nothing under reverse-Z — so the engine keeps the submission walk. The verdicts below were taken against *that* answer, which is why the ticket waited: taken a day earlier they would have been written against a subclass that does not exist |
| **Refs** | [../../documentation/12-vendored-capabilities.md](../../documentation/12-vendored-capabilities.md) — **the document this makes binding**; [../completed/0168-asset-import-coverage.md](../completed/0168-asset-import-coverage.md) — the worked precedent: the same sweep for the import path ended that class of surprise in one afternoon; [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md), [../completed/0142-slang-shader-language.md](../completed/0142-slang-shader-language.md), [../completed/0145-lighting-stage-own-the-light-loop.md](../completed/0145-lighting-stage-own-the-light-loop.md) — **the attachment pattern this generalises**; [../open/0045-culling-and-render-queues.md](../open/0045-culling-and-render-queues.md), [../open/0061-debug-draw.md](../open/0061-debug-draw.md), [../open/0086-shadows.md](../open/0086-shadows.md), [../open/0088-sky-atmosphere-time-of-day.md](../open/0088-sky-atmosphere-time-of-day.md), [../open/0091-volumetric-fog.md](../open/0091-volumetric-fog.md), [../open/0094-gameplay-extensible-rendering.md](../open/0094-gameplay-extensible-rendering.md), [../open/0096-hdr-pipeline-and-tonemapping.md](../open/0096-hdr-pipeline-and-tonemapping.md), [../open/0148-post-process-stack.md](../open/0148-post-process-stack.md), [../open/0150-compute-pipelines.md](../open/0150-compute-pipelines.md), [../open/0155-terrain-rendering.md](../open/0155-terrain-rendering.md) — the tickets this rescoped; [0047-evaluate-render-graph.md](0047-evaluate-render-graph.md) and [0087-environment-lighting.md](0087-environment-lighting.md) — **the two it closed**; **D26** (as amended), **D30**, **D35** (extended here), **D40** (the governing rule), **D41** (recorded here) |

## Why

**The owner's instruction, and it is the whole ticket:**

> *"I don't want us doing all those crappy tasks in our backlog that are covered by Diligent. We only need the shaders to be exposed to the game devs and abilities we clearly stated to game devs."*
>
> *"**EXPOSE** capabilities to game devs, not **replace**… we already did that with Slang, and we can do that for others in the same method of attachment and not rewriting."*

**`CLAUDE.md` already names most of what is vendored** — *"Tone mapping, bloom, DoF, SSAO, SSR, TAA, atmospheric scattering, four shadow-filter modes and a whole terrain implementation are already vendored. The check has failed twice here."* It has now failed **six** times, and the backlog still contains tickets to build several of those from scratch.

## The principle, stated once so every verdict can be checked against it

**Attachment, not rewriting.** The pattern already exists and it is `IHpMaterial`: the engine declares an interface whose **default implementation *is* the standard path**, and a game overrides only what it wants. Nothing is reimplemented; a seam is opened. T0142 did it for Slang, T0145 did it for the light loop, T0159/T0160/T0161 did it for the material contract.

**Every capability below gets the same treatment.** Diligent's shadows, IBL, tone mapping, bloom, SSAO, atmosphere and terrain are the implementation; our job is the **seam** a game reaches them through, and nothing else. A ticket that proposes to *build* one of these is wrong by construction after this sweep.

**And the keep-list is short by design:** what a game developer needs to reach the shader, plus the abilities explicitly promised them. Everything else Diligent does and we expose.

## Done when

- [x] **Every open render-layer ticket has exactly one verdict**, with the
      upstream symbol or file named — see the verdict table below
- [x] **A table exists** that answers *"is this already in Diligent?"* —
      [`12-vendored-capabilities.md`](../../documentation/12-vendored-capabilities.md),
      rewritten in the shape `14-asset-import-matrix.md` proved, with a state
      per row that **names who owns the gap**
- [x] **Every rescoped ticket says what its seam is** — and the sweep's central
      finding is that there are **two** seams, not one, so several tickets were
      told they have *no* seam and only settings. See below
- [x] **`12-vendored-capabilities.md` is binding** rather than advisory, and
      `CLAUDE.md` points at it — plus a new `CLAUDE.md` row for the two seams,
      because that is what nine tickets each got wrong independently
- [x] **The Current ticket sequence contains nothing Diligent already does** —
      reordered, and T0047 and T0087 came off it by closing

## Subtasks

- [x] 171.1 **Sweep by reading, not by building** — every phase-4 ticket and
      every matrix row read against the pinned source. It was an afternoon, as
      predicted, and like T0168 the reading corrected the sweep's own premises
- [x] 171.2 **Verdict the known candidates**, each with the upstream symbol —
      the table below
- [x] 171.3 **Design the seam once, not nine times** — done, and the answer is
      **two seams plus a third category**, which is the finding this subtask was
      written to produce. The prediction that it would be *"one attachment
      pattern, `IHpMaterial`-shaped"* was **wrong**, and usefully so: see below
- [x] 171.4 **Close what should be closed** — T0087 (on IBL, remainder to
      T0088) and T0047 (answered "no", D41). Both with `## Descoped` sections
      naming the receiving subtask, references both ways
- [x] 171.5 **Make the check unavoidable** — **D35 extended a third time**, and
      two `CLAUDE.md` rows
- [x] 171.6 **Record what Diligent does *not* have** — a state (⬆️) rather than
      a footnote, plus an *Open gaps with no owner* section, which is where the
      volume family, the null `IRenderStateCache` and Radient's build cost now
      live

## The verdicts

| Ticket | Verdict | What it actually needs now |
|---|---|---|
| **T0047** render graph | ❌ **closed — answered "no" (D41)** | Nothing. 3 passes against a trigger of 15; barriers free, aliasing unreachable by API; upstream's own 22-task renderer uses a hand-ordered vector |
| **T0087** IBL / skybox | ❌ **closed on IBL** | IBL delivered and measured (10.4 → 112.9 mean luma). The sky is **not** delivered — `EnvMapRenderer` is never constructed — and moved to T0088 |
| **T0086** shadows | 🔌 **expose**, and it split in two | The *shading* half is `Shadows.fxh`, called from our own `HpGetLight`. The *system* half — depth pass from the light, allocation, caster cull — is ours. **Point/spot shadows are ⬆️ and must be scoped or declined** |
| **T0096** tone mapping | 🔌 **expose** | A pass calling `ToneMap()`. Eleven operators ship. **The operator is a compile-time macro**, so selecting it is a variant decision (T0151), and T0086/T0155 inherit that answer |
| **T0148** post-process | 🔌 **expose** | Construct `PostFXContext`/`GBuffer`, run the five components, settings as reflected data. **The new part is a game's own effect → T0094** |
| **T0088** sky / atmosphere | 🔌 **expose** | Construct `EnvMapRenderer` *and* `EpipolarLightScattering`; choose which techniques; 41 fields to triage. **Inherited T0087's sky remainder** |
| **T0091** volumetric fog | 🔌 **for the sun**, ⬆️ **for the rest** | Epipolar scattering is **one directional light**, so sun shafts are T0088's configuration. Only point/spot beams remain, and **declining is a valid close** |
| **T0155** terrain | 📄 **sample source** | The only 📄 on the board. Decide adopt-vs-build with the source open; expose heightmap and splat as `hp::` assets either way |
| **T0045** culling / queues | 🔌 **for the maths**, ⬆️ **for the policy** | Frustum test via `AdvancedMath`. **The sorting half shrank**: T0170.4's depth prepass took most of it; what remains is glass against glass. OIT closed on T0170.3 |
| **T0061** debug draw | ✅ **genuinely ours** — *corrected* | Three vendored *visualisations* are not a debug-draw API, and one box per draw call is the trap this ticket forbids. Three rows handed to T0032/T0033/T0111 |
| **T0150** compute | ✅ **genuinely ours** | The API is standard; everything above it is undesigned. **Unblocks `DepthRangeCalculator` and T0088's high-quality techniques**, which was not previously recorded |
| **T0094** pass seam | ✅ **genuinely ours, and it moved to the front** | **The frame seam, and four tickets ride it.** Interface built, **never once crossed by a real app** |

## Not in scope

- **Doing the rescoped work.** This ticket produces verdicts and seams; the tickets it rescopes do the work.
- **The non-render backlog.** Scene, ECS, VFS, modules, editor, import, gameplay — Diligent has no opinion on any of it.
- **Reopening T0170's decision.** This sweep assumes the engine is a subclass; that is why it is blocked on it.

## Notes / findings

### Why this is blocked on T0170 rather than running now

Every verdict depends on it. As a reimplementation of Diligent's loop, adopting their shadows means porting a subsystem into a foreign walk. As a subclass, it is a settings field and a seam. **The same ticket gets a different verdict either side of T0170**, so sweeping first produces a table that is wrong before anyone reads it.

### The counter-risk, named so the sweep does not overcorrect

**"Diligent has it" is not automatically "we do not need the ticket."** Their implementation may be wrong for us — the wrong quality bar, the wrong extension shape, or fighting a recorded decision. T0155's own title already says it: *"their reference implementation is the floor, not the ceiling."* So the verdict ✅ **genuinely ours** must stay reachable, and where it is chosen the reason gets written down. **The failure this ticket corrects is reimplementing by default; replacing it with adopting by default would be the same mistake facing the other way.**

---

## What the sweep found, 2026-08-08

### 171.3's answer, and the prediction it falsified

**This ticket predicted one attachment pattern, `IHpMaterial`-shaped, for all
nine capabilities. That is wrong, and finding out why is the sweep's main
result.**

`IHpMaterial` is a **per-fragment** interface: Slang generics, defaults that *are*
the standard path, a game overrides methods. It fits anything about a **surface**.
**It fits nothing about a frame** — and a frame is what shadows-as-a-pass,
post-processing, the sky, fog and tone mapping all are. Stretching it to cover
them would have produced exactly the silent-failure shape **D35** was written
against: an interface whose method has nothing meaningful to default to.

So the engine has **two seams and one non-seam**, and the distinction is now in
`CLAUDE.md` and at the top of the capability matrix:

| | Surface seam | Frame seam | Settings |
|---|---|---|---|
| **What** | `IHpMaterial` | a pass — `IRenderLayer` in `RenderStack` | reflected data |
| **Granularity** | per fragment | per frame | per project/scene/camera |
| **Default** | the standard shading path | the engine's own layers | a value |
| **Status** | **built** | **interface built, never used by a real app** | T0078 |
| **Owner** | closed tickets | **T0094** | T0078 |

**Most of what the nine tickets wanted is the third column.** Which tonemap
operator, which shadow filter, bloom threshold, cascade count, scattering
technique: **settings, not seams.** The rescopes say so explicitly, because
"expose it to game devs" was being read as "give it a hook", and nine hooks is
the outcome D40 exists to prevent.

**And D32 needed reinterpreting rather than honouring literally.** It promised
the sky shader would be *"authored against an interface a game can implement —
the same default-methods shape as `IHpMaterial`"*. Under D40 there is **no sky
shader of ours**, so the promise is discharged by T0094: a game that wants its
own sky writes a pass. Recorded on T0088 so it is not rediscovered as a
contradiction.

### T0094 is not one more item on the list — it is what four of the others wait on

T0094's own `## Why` said it before this ticket existed: *"minimaps, portal and
mirror views, security-camera monitors, custom post effects, decal buffers and
player-drawn markers all need the same three capabilities."* T0148's game post
effects, T0088's game-authored sky, and T0091's and T0096's chain all ride that
transport.

**It is not a hard blocker on their engine halves** — an engine pass can go into
`RenderStack` today. **It is a design blocker**, which under D35 is the stronger
kind: build the chain first and its insertion API is retrofitted; build the seam
first and the chain plugs into something that exists. D35 exists because that
retrofit has already been paid for twice.

### The thing nobody was looking for: `DiligentFX/Radient`

**21,746 lines across 103 files** — a complete alternative engine inside the
pinned DiligentFX: scene, components, importer, asset cache and resolver, draw
list, frame render targets, SRB cache, light list, a PBR renderer, and a
three-pass render pipeline. Genuine upstream, authored by Diligent's maintainer
and dated 2026-07-27. **It is compiled by every build of this project** —
`DILIGENT_NO_RADIENT` defaults `OFF` and nothing here sets it — and it is named
in **no document and no ticket**.

**Not adopted**, and the reasons are specific rather than territorial: its object
model is a C ABI with its own scene and importer, so adopting it replaces
everything D40 lists as ours (D12, D13, D23, D26); `RadientRenderPipeline` is
four concrete members in a fixed order with **no insertion point**, so
`RenderStack` is strictly more extensible; and `RadientPostProcessPipeline` is an
**empty stub** — both methods are `(void)` casts returning OK — so it is not even
a reference for T0148.

**Two cheap things follow.** Re-read it on every pin bump, because if upstream
matures it the calculus changes. And consider `DILIGENT_NO_RADIENT=ON`: we
compile 21,700 lines we never link, on every cold build and every pin bump.
**Not done here** — this was a documentation sweep and that is a build change —
so it is recorded as an unowned gap in the matrix.

**This is the row that argues for the whole document.** No test could have found
it. No amount of care would have. Only a table with a *who owns the gap* column
does — which is why **D35 now extends to the render layer** and the matrix is
binding rather than advisory.

### Corrections to the previous survey — measured, not restated

The 2026-08-06 pass of `12-vendored-capabilities.md` summarised where it should
have pointed, and got four things wrong:

1. **`ShadowMapManager` is in `DiligentFX/Components/`, not DiligentCore.** So
   are `GBuffer` and `DepthRangeCalculator`, both filed under
   `PostProcess/Common/`.
2. **The debug renderers are not "unreferenced"** — `HnRenderBoundBoxTask` and
   `HnPostProcessTask` drive all three. They are unreferenced *by this engine*,
   and Hydrogent is the worked example of how to drive them.
3. **They are also not a debug-draw API.** `BoundBoxRenderer` draws **one box
   per `Prepare`+`Render` pair**; `CoordinateGridRenderer` is a full-screen
   ray-marched editor grid; `VectorFieldRenderer` reads a motion-vector texture.
   **No line renderer, no text renderer, nothing batched, anywhere.** T0061 was
   expected to be the cheapest win on the board and is in fact ⬆️ genuinely
   ours.
4. **DiligentFX ships two unconnected shadow implementations.** `Shadows.fxh`
   has cascade selection (`:65`), cross-cascade blending (`:238`) and all four
   filter modes; `PBR_Shading.fxh` includes `PCF.fxh` alone and samples **one
   array slice per light** with no cascades (`:645-656`). Only the second is
   wired to `PBR_Renderer`. **Owning our own pixel shader is what lets us call
   the better one** — which is the clearest single vindication of D26's
   surviving half.

Two further findings with no previous claim to correct:

- **`TONE_MAPPING_MODE` is a compile-time macro.** `ToneMappingAttribs::iToneMappingMode`
  is never read by `ToneMap()`; upstream keys the PSO on it instead. So "let the
  game pick an operator" is a **variant** question (T0151), and the same shape
  governs `SHADOW_MODE` and terrain's `TEXTURING_MODE`.
- **`EpipolarLightScattering` takes one directional light** (`FrameAttribs::pLightAttribs`).
  It is sun shafts, not volumetrics — which halves T0091 and moves the other
  half onto T0088.

### What this ticket did *not* do, stated rather than implied

- **No engine code was written.** Every verdict is from reading source; nothing
  was compiled to prove a rescope, and no test was added or changed.
- **`DILIGENT_NO_RADIENT=ON` was not applied.** It is a build change, it would
  trigger CI, and the saving is build time rather than correctness.
- **The `textured_surface_test` metal assertion T0087 enabled was not written**
  — recorded on that ticket instead.
- **T0170 is still in progress** and this sweep did not touch it. Its 170.6
  deletion pass is still owed, and its brief for the `InitMaterialSRB` /
  `GetMaterialPSOFlags` follow-up is on its own notes.
- **The counter-risk this ticket named was live and was used.** *"Diligent has
  it" is not automatically "we do not need the ticket."* Three verdicts came
  back ✅ genuinely ours — T0061, T0150 and T0094 — and one of those (T0061)
  reversed an expectation that it was mostly vendored. The sweep did not
  overcorrect into adopting by default.
