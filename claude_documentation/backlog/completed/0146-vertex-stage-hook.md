# T0146 — The vertex stage: own the vertex main, and give games a vertex hook

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 456 |
| **Created** | 2026-08-06 |
| **Refs** | **Establishes D36** ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)) — the hook works in object space, one way, no mode switch, and the four fixed custom interpolators; **T0161** ([../completed/0161-game-resource-model.md](../completed/0161-game-resource-model.md)) / **D35** — a vertex module's own resources (a wind field, a displacement map) come from `buildModuleSignatureDesc` (`engine/src/ModuleResourceSignature.hpp`): pass this stage's base-signature names and a `ModuleSignaturePolicy`, bind the result beside the pass signature — do **not** invent a second mechanism, and note the sampler palette is already declared `VS_PS` so vertex-stage sampling needs no base-signature change; **D30**/**D32** ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)); [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) — 141.7's vertex-displacement note names this exact change; [../completed/0142-slang-shader-language.md](../completed/0142-slang-shader-language.md) — one compiler for both stages is what makes this cheap; T0041 — skinning must be built against *our* vertex shader, see "Sequencing"; [0155-terrain-rendering.md](../open/0155-terrain-rendering.md) — **the likely first consumer of 146.7's tessellation**, whose 155.2 LOD decision is where it is first *wanted*; [0156-parallax-under-triplanar.md](../completed/0156-parallax-under-triplanar.md) — 156.6 evaluated silhouette POM (2026-08-06) and came back **negative**, so 146.7's tessellation is the only silhouette answer — see 146.7; [0145-lighting-stage-own-the-light-loop.md](../open/0145-lighting-stage-own-the-light-loop.md) — the same ladder, one stage earlier |

## Why

**The vertex shader is the one stage still Diligent's.** `SurfacePipeline.cpp`
names it: `kVertexShader = "RenderPBR.vsh"` — with a comment saying vertex
displacement (141.7) is what changes it. D26 stopped one stage short of its own
principle: the engine owns the pixel `main` and not the vertex one, so a game
can displace *texture coordinates* per fragment and cannot move a *vertex* at
all.

That forecloses the hook Godot developers use most. In Godot, `vertex()` is
where wind sway, billboarding, breathing, jelly, flag flap and every cheap
vertex animation lives — it is the workhorse of stylised motion, and it is rung
zero of vertex-stage control. HollowPoint has no equivalent, and no ticket owned
the gap (T0141.7's vertex-displacement half assumed it in passing).

**The cost is bounded, and the shape is exactly D26's.** `RenderPBR.vsh` is
257 lines including the skinning block. The transform itself is public library
code — `Shaders/PBR/public/VertexProcessing.fxh` provides
`GLTF_TransformVertex` — so the engine's vertex main mirrors the orchestration
and calls their public helper, the same our-main-their-library split the pixel
shader already made. The skinning block (~30 lines of joint blending) is
mirrored with it.

**Measured, 2026-08-06:** slang compiles a module carrying both
`[shader("vertex")]` and `[shader("fragment")]` entry points to **one SPIR-V
module with two `OpEntryPoint`s** in a single compile (probe on the pinned
2026.14.1). Today the engine issues two compile requests per pipeline — one for
their `.vsh`, one for our `.slang`. Owning both stages in one file makes one
compile of one module, which is a real bite out of the measured 2–4x cold
compile (142.6), on top of the capability.

## Sequencing

- **Before T0041 (skinning runtime).** T0041 will build the joint-palette
  draw path against whatever vertex shader exists. If that is Diligent's, the
  move here relocates skinning afterwards — the same pay-twice shape as
  T0086 against T0145, and the reason this sits early in the order.
- **Motion vectors will land in this file.** T0111's TAA prerequisites (motion
  vectors, T0101.5) are vertex-stage work — previous-frame positions come out
  of the vertex shader. Owning the file first means T0111's remainder lands in
  our code once; this ticket only leaves the seam named, it builds nothing of
  TAA.

## Done when

- [x] The engine's own vertex shader drives every draw, and the rendered
      output is **byte-identical** to the current build (same baseline
      discipline as 141.10/142.3)
- [x] The contract has a vertex method with a default — a material that says
      nothing gets today's behaviour exactly
- [x] A worked example displaces vertices (wind sway or a breathing scale) and
      renders correctly, silhouettes included — the thing 141.7's parallax
      explicitly cannot do
- [x] The **space the hook works in is decided and documented** — Godot's
      `world_vertex_coords` / `skip_vertex_transform` question: what the
      method receives (object or world space), what it returns, and what the
      engine does after. One decision, written, not three defaults discovered
- [x] **Custom interpolators are decided deliberately.** `VSOutput` is
      generated per permutation by `PBR_Renderer`, so a game cannot add fields
      to it today. Either a fixed number of custom slots is added, or the
      limitation is recorded with a trigger — silence is the one wrong answer
- [x] The skinned path is guarded to whatever extent it can be before T0041 —
      stated plainly if that is "compiles, cannot render" (no skinned asset
      path exists yet)
- [x] Both stages compile as **one slang module in one compile**, and the
      compile-count/time change is measured against 142.6's numbers

## Subtasks

- [x] 146.1 Mirror the vertex main: `GLTF_TransformVertex` via their public
      header, the joint-blending block, the `VSOutput` population — then the
      byte-identical guard before anything else changes
- [x] 146.2 The vertex method on the contract, with the default that is
      today's path; decide its space and document it (the Godot
      `world_vertex_coords` decision)
- [x] 146.3 Worked example: wind sway in the sandbox, asserting the silhouette
      actually moves (a pixel far from the rest position)
- [x] 146.4 The custom-interpolator decision, recorded either way
- [x] 146.5 Single-module compile: move `kVertexShader` to the engine's
      `.slang`, measure the compile-request count and cold-compile delta
- [x] 146.6 Name the motion-vector seam for T0111's remainder (a comment and a
      line here, not an implementation)
- [~] 146.7 **Tessellation / displacement, or an explicit decision not to**
      — *closed on the decision half only, 2026-08-07. **Nothing was built.**
      The explicit decision is: **not now**, and the trigger below is
      re-affirmed with its scope narrowed by what this ticket delivered —
      146.1–146.6 move silhouettes at the density a mesh already carries, so
      the remaining case is specifically an outline that stays faceted
      however far it is pushed. No hull or domain stage exists, and none was
      attempted. Carried by T0155 (155.2) and T0156's notes, both of which
      reference this line.*
      (was T0141.9, inherited 2026-08-06 when T0141 closed — see "Inherited
      from T0141" below). **Deferred with a named trigger, not scheduled**:
      *when a silhouette must change at a density the mesh does not carry.*
      `PBR_Renderer` creates no hull or domain shaders at all, so this is new
      **pipeline** work rather than new shader code — which is why it lands
      here, where the pre-rasteriser stages become the engine's, and not on the
      ticket that happens to want it first.
      **T0156.6's silhouette-POM evaluation came back negative (2026-08-06)**,
      so this trigger is the *only* silhouette answer and the pressure behind
      it is real: silhouette POM does not compose with triplanar (no single
      height field to march), its `discard` degrades hierarchical-Z for
      everything drawn behind — terrain being the scene's dominant occluder —
      and nobody ships it for this problem (Unreal's answer is Nanite runtime
      tessellation; CryEngine gates its version to the very-high tier). The
      full evaluation with sources and this engine's march-cost anchors is on
      T0156's notes; do not reopen silhouette POM without new evidence

## Inherited from T0141, 2026-08-06 — tessellation, and a trigger that needed sharpening

**141.9 came here when T0141 closed**, and the choice of destination was
argued rather than defaulted, because there were two plausible ones.

**Why here and not T0155 (terrain).** Terrain is the obvious *consumer* —
displacement is what makes a heightmap read as landscape, and 155.2 is
literally the decision "clipmap, chunked quadtree, or theirs extended", where
GPU tessellation is a candidate. But what tessellation actually needs is
**hull and domain stages in the PSO**: `PBR_Renderer::CreatePSO` sets `pVS` and
`pPS` and nothing else, and D26 is what made adding stages possible at all.
That is this ticket's code — the one that stops the vertex shader being
Diligent's and moves `kVertexShader` into the engine's own module. Homing a
general pipeline capability on terrain would have shaped it like terrain, and
the second consumer (a hero asset, a wall that must have a real silhouette)
would have found it in the wrong place. **T0155 gets the cross-reference
instead**, on 155.2, which is where the want will first appear.

**The trigger needed sharpening, and that is the useful part of the move.**
T0141 deferred this *"when a silhouette must change"*. This ticket **delivers
silhouette change** — 146.3's worked example asserts a pixel moves far from its
rest position, and the Done-when says "silhouettes included, the thing 141.7's
parallax explicitly cannot do". So the trigger as written is discharged by the
ticket that inherited it, which would have read as "do it now" to anyone
arriving cold.

What tessellation adds over 146.1–146.6 is **new geometry**, not moved
geometry: vertex displacement is bounded by the mesh's existing density, so a
low-poly wall pushed along its normals still has a faceted outline. The trigger
is therefore restated: **when a silhouette must change at a density the mesh
does not carry.** Two things would pull it in — T0155's LOD scheme choosing
tessellation, and T0156.6's silhouette-POM evaluation coming back negative
(if pixel-shader silhouettes work, the pressure drops; if they do not, this is
the remaining answer). Both tickets say so on their own side.
**The second of those has now happened**: 156.6 evaluated silhouette POM on
2026-08-06 and recommended against it — see 146.7's subtask text for the
short form and T0156's notes for the full evaluation. This ticket's
tessellation is the remaining answer to silhouettes past mesh density.

**What T0141 established and this inherits unchanged:** tessellation is
reachable *only* because D26 chose C2 — we own PSO creation. C1 (patching
DiligentFX's shaders) could never have reached it, because the missing piece
was C++ PSO construction rather than shader text, and that was the finding that
settled C1-vs-C2 as a capability question rather than a maintenance one.

## Notes / findings

### What landed, 2026-08-07 — and what did not

**146.7 is deliberately left unticked.** It is the tessellation deferral, and
its own text says "deferred with a named trigger, not scheduled". Nothing here
discharged that trigger — *when a silhouette must change at a density the mesh
does not carry* — so ticking it would claim work nobody did. **The trigger did
get sharper, in the direction that makes it less urgent**: this ticket delivers
silhouette change at the density a mesh already has, and the rock cube sample
now demonstrates it, so the remaining case is specifically a low-poly outline
that stays faceted however far it is pushed. See "Where 146.7 goes" below.

Everything else on the list is done and measured.

### The compile measurement (146.5), against 142.6's numbers

**Compile requests per pipeline: 2 → 1.** The engine issued one request for
DiligentFX's `RenderPBR.vsh` and one for `HpSurface.slang`; it now issues one
with two entry points. Pinned by a gpu case that counts the compiler's own
per-entry-point log lines: `vsMain 1, psMain 1, RenderPBR.vsh 0`.

**Cold compile, like for like.** `HP_SPIRV_CACHE=0`, the same 43 gpu cases
(the six new T0146 cases and the two cook cases excluded from both sides),
three runs each, Linux target on an RTX 2080:

| | before | after |
|---|---|---|
| whole run, median of 3 | **54.22 s** (54.19, 54.22, 54.71) | **51.60 s** (51.57, 51.60, 51.75) |
| `each surface channel can be inspected on its own` — the most permutations, and 142.6's own heaviest case | 8.123 s | **7.458 s** |
| `a lit surface is the colour it was authored as` | 3.136 s | 2.996 s |
| `parallax under triplanar: cost per weight configuration` | 2.926 s | 2.695 s |
| `a world layer and a HUD layer composite correctly` | 3.009 s | 2.847 s |
| `frame targets and the render stack work` | 1.823 s | 1.761 s |
| `the scene renderer builds and submits` | 2.183 s | 2.195 s |
| `the scene view publishes an offscreen frame` | 1.751 s | 2.018 s |

**4.8% off the whole run, 8.2% off the heaviest case.** Two of the small cases
moved the wrong way, which is what a 1.8-second test measured three times on a
shared machine looks like; the aggregate and the heavy case are the signal.

**Against 142.6, honestly: this recovers a slice of the 2–4x, not the 2–4x.**
142.6 measured slang's cold compile as 2–4x glslang's, with that same
debug-channel case at 3.20 s under glslang and 7.90 s under slang. It is 7.46 s
now. The saving is one *front-end* pass — parse, preprocess, include walk —
and the file the vertex stage now parses is `HpSurface.slang` with all of
`PBR_Shading.fxh` and `PBR_Textures.fxh` behind it, where before it was
`RenderPBR.vsh` at 257 lines. So half the win is given straight back by the
vertex stage becoming a much bigger translation unit, and the ticket's
expectation that this would be "a real bite out of the 2–4x" was **optimistic**.
It is a real 5–8%, and it is recorded as that.

**Bytecode.** Per-stage SPIR-V, custom-material permutation:

| | before | after |
|---|---|---|
| vertex | 6948 (`RenderPBR.vsh`) | **7280** (`vsMain`) |
| pixel | 19168 | **19172** |

The vertex module gains 332 bytes: the hook's plumbing plus the four custom
interpolators. The pixel shader gains 4 — one SPIR-V word, the interpolator
reads. A **standard** material's pixel shader is unchanged.

### Byte-identical: how it was checked, and what it showed

Not by inspection. Every numeric `MESSAGE` the gpu suite prints was captured
before the change (159 unique lines across both targets) and diffed after.
**Every rendered-frame number is identical.** What moved: timings, the two
SPIR-V byte counts above, the cooked-archive size, `cook run: 1 compile(s)` →
`2` (the cook now seals two entry points of one file instead of one file plus
`RenderPBR.vsh`), and the new cases' own lines.

The load-bearing survivors, quoted because they are the guards the brief named:

- `no-height vs zero-scale: 0` on all three parallax baselines — unchanged;
- rock cube sample: `cube covers 18.079% of the frame, luminance variation
  26.7341` — unchanged, and this is the sample whose shader now *has* a vertex
  hook, resting at `Time == 0` because `sin(0)` is zero;
- self-shadowing: `covered: on 46723, off 46723`, `mean abs difference
  0.0449892`, `darkened>10 72`, `max drop 51.7406` — unchanged;
- `channel_basecolor` `(116, 108, 69), variation 16.382` and the seven other
  debug channels — unchanged.

Counts: fast **321**, integration **92**, gpu **51** (was 45; six new cases).
Both targets: Linux natively, Windows under wine, both on the RTX 2080.

### The measurement that surprised: `HP_SPIRV_CACHE=0` breaks the cook suite

**Pre-existing, found while measuring, not caused here.** Running the whole gpu
suite with the developer cache disabled fails
`cooked_shaders_test.cpp:353 REQUIRE(hp::cookShaders(...))` — the cook refuses
with *"HP_SPIRV_CACHE=0 keeps no compiled bytecode, so there is nothing to
seal"* — and the **next** test case then dies with SIGSEGV, because doctest's
`REQUIRE` abort leaves the VFS and the device mounted. Verified on the
pre-T0146 build as well, so it is not a regression.

Two separate things worth someone's time, neither of them this ticket's:

1. the cook cases should skip rather than fail when the cache is off, which is
   the environment saying "this run cannot cook";
2. **a fatal assertion inside a gpu case takes the next case with it.** The
   teardown is in the test body after the `REQUIRE`, so it never runs. That is
   a landmine under every `REQUIRE` in this suite.

### 146.1 — what the mirror actually cost

257 lines of `RenderPBR.vsh` became ~180 in `HpSurface.slang`, and the
difference is entirely the parts that are *theirs*: `GLTF_TransformVertex` is
included from the public `VertexProcessing.fxh` rather than rewritten, exactly
as `PBR_Shading.fxh` is on the pixel side. What was mirrored: the joint-blend
block, the packed-normal and packed-position decoders, the vertex-colour
overloads, the `VSOutput` population, and their comment about copying
`PBRPrimitiveAttribs` into a local (*"causes huge performance degradation on
Vulkan because glslang/SPIRV-Tools are apparently not able to eliminate the
copy"*), which is kept verbatim because rediscovering it would be expensive.

**One thing was dropped and one was added.** `PSMainFooter.generated` is gone:
it existed because `RenderPBR.psh` includes it unconditionally, and nothing
this path compiles includes it any more. And `PRIMITIVE_ARRAY_SIZE > 0` now
`#error`s rather than being silently unhandled — the pixel shader already
assumed the non-array form, so turning `PrimitiveArraySize` on would have piled
every draw at primitive 0's transform with no diagnostic.

### The skinned path: compiles in no permutation, and that is the honest answer

The Done-when asked for it stated plainly, so: **`PSO_FLAG_USE_JOINTS` is never
set.** `drawModel` derives the vertex flags from the mesh's attributes and maps
`NORMAL`, `TEXCOORD_0/1`, `COLOR_0` and `TANGENT` — joints are not among the
cases. So `#if MAX_JOINT_COUNT > 0 && USE_JOINTS` is false in every permutation
the engine builds, and the block below it is not merely untested, it is
**uncompiled**. It is mirrored anyway so T0041 inherits the palette lookup
against our vertex shader instead of relocating it afterwards, which is the
sequencing argument this ticket was ordered on.

### 146.4 — the interpolator decision, and the option that was rejected

**Four fixed `float4` slots**, appended to `PBR_Renderer`'s generated
`VSOutput` string for custom-material permutations only. D36 carries the
reasoning; the rejected option is worth naming here: letting a module
**declare** its varyings, Godot-style, is not reachable, and not for effort
reasons. Reflection rides the compile (T0160's finding, unchanged), so what a
module declares is known only *after* the compile that would have to be told
about it. The two-pass workaround — compile to reflect, then recompile with the
right macro — doubles the cold compile for every custom material, which is the
cost 146.5 exists to reduce.

The slots are free for a standard material and always-on for a module. That
asymmetry is deliberate and measured: the standard material's `VSOutput` is
byte-identical to what it was, which is what makes the whole byte-identical
claim above hold at all.

### 146.6 — where the motion-vector seam is

`transformVertex`'s `#if COMPUTE_MOTION_VECTORS` block, with the comment T0111
needs: the previous clip position is computed from the **displaced** position
through the previous transform, which is right for a displacement constant in
time (a morph pose, a per-instance offset) and **wrong for one that animates** —
wind wants the hook re-run with the previous frame's `Time`, and nothing writes
a previous `Time` into the frame constants. The flag is masked off in
`kFeatureMask`, so the branch compiles in no permutation and the inaccuracy
ships nowhere. A line and a comment, as the subtask asked; no TAA was built.

### What T0161's helper needed, and why it is not a D35 violation

`ModuleResourceSignature.hpp` predicted this ticket would "add exactly two
inputs — their base signature's names and their policy". It did not, and the
reason is a cardinality error rather than a design one: **a surface module is
one module compiled to two stages**, not a second module at a second stage. The
same `HpMaterial` implements `vertex()` and `baseColor()`, so a texture it
declares may be sampled in either and there is one signature because there is
one module.

`ModuleSignatureRequest` therefore takes a *list* of compiled stages and unions
the `ShaderStages` bits per name. Still one mechanism, still stage-neutral —
and the header now says so, because T0148 and T0150 will read that prediction
and should read the correction with it.

**One bug this shook out**, worth knowing before T0148/T0150 hit it:
`IShaderResourceBinding::GetVariableByName` takes **one** stage bit, not a
mask. It indexes a per-stage variable manager, so passing `VS_PS` finds nothing
and logs about an inconsistent shader type. Diligent stores one cache slot per
resource whatever its stage mask, so setting through any single stage sets it
for all — `ensureModuleSrb` now passes the lowest set bit.

### Where 146.7 goes, and why it is not going anywhere yet

Unchanged in substance, sharper in scope. Silhouette POM stays closed (T0156.6,
negative, do not reopen without new evidence). What this ticket delivers is
silhouette change **at the density the mesh carries** — the rock cube leans
because it has vertices along its height to lean. A low-poly wall pushed along
its normals still has a faceted outline, and that is the case tessellation
answers. The two things that would pull it in are unchanged: T0155's LOD scheme
choosing it, or a hero asset that must have a real silhouette. Both tickets
carry the reference.

### The authoring gotcha the sample found

**Displacing a flat-shaded mesh along its normals splits it open.** The cube
carries three vertices per corner with three different face normals, so pushing
along them sends the three copies to three different places and the edges gape.
The sample leans instead — wind sway, which keeps the surface watertight — and
`rock_pom.slang` records why in the hook's own comment. It is the first thing
anyone writing a vertex shader against hard-edged geometry will hit.

### Not verified

- **No skinned asset was rendered**, because none exists (T0041). "Compiles,
  cannot render" is generous: it does not compile either, because the
  permutation is never requested.
- **Motion vectors were not exercised.** The flag is masked off; the branch was
  read, not run.
- **The four interpolators' occupancy cost was not measured.** They are always
  present for a custom material, and whether four unused ones cost anything
  measurable on dense geometry is unknown — recorded on D36 as the revisit
  trigger.
- **Tessellation (146.7) was not attempted.**
- **The editor was not run.** The sample's sway is asserted by a gpu test at two
  clock times, not watched.


### What the probe showed (2026-08-06, pinned slangc 2026.14.1)

A file with `[shader("vertex")] vsMain` and `[shader("fragment")] psMain`
compiles in one invocation to one SPIR-V module containing two
`OpEntryPoint`s; Diligent accepts per-entry-point bytecode
(`ShaderCreateInfo::ByteCode` + `EntryPoint`). The reflection JSON from the
same compile lists both entry points — one compile can feed both stages *and*
the inspector.

### Godot reference points (4.7.1, surveyed 2026-08-06)

`vertex()` runs per vertex with `VERTEX`/`NORMAL`/`UV`/`COLOR` writable;
`world_vertex_coords` switches the space, `skip_vertex_transform` hands the
transform to the shader. **Custom interpolators: Godot has them** — a shader
author declares `varying` values passed vertex → fragment — and HollowPoint
does not, because `VSOutput` is generated by `PBR_Renderer` per permutation.
This is a row where Godot currently *exceeds* this engine, which is why 146.4
is a decision and not a footnote.
