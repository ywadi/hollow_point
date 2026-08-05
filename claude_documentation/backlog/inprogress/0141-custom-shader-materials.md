# T0141 — The surface stage: the standard material shader, and custom ones

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 455 |
| **Created** | 2026-08-05 |
| **Blocked by** | T0060.1 + T0060.6 only — the material *asset* and per-surface assignment. **Not** the rest of T0060, which was re-cut into this ticket on 2026-08-05 |
| **Blocks** | **T0086** — shadow sampling must be written against *our* pixel shader, not `RenderPBR.psh` (141.0 is decided; 141.10 is what T0086 now waits on). Height mapping, parallax occlusion, triplanar and vertex displacement — 141.7/141.8 — which no material parameter can express |
| **Refs** | [../completed/0060-material-system.md](../completed/0060-material-system.md) (split from it), [../completed/0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md), T0093, T0053, T0094, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D24 |

## Why

**The owner's concern, in their words:** *"im still worried that the engine wont
be complete enough for the editors with the PBR setup that it currently has, so
if an editor wants to use screen displacement for texture, thats a custom shader
or not even possible on the engine it self?"*

The honest answer as of 2026-08-05 is **not possible at all** — there is no
custom shader mechanism, and the standard material cannot do it either. That is
a real ceiling on what an artist can author, and this ticket is what lifts it.

The ceiling has a single cause. `RenderPBR.psh` and `RenderPBR.vsh` are private
to DiligentFX with **no hook before texture sampling**, and
`CreateInfo::GetPSMainSource` reaches only the pixel shader's output struct and a
footer — it can change what the shader *emits*, never how it *samples*. So every
technique that displaces UVs or positions before sampling is unreachable, no
matter what the material asset says.

### What the current PBR already has, so the ceiling is not where it looks

The "quad and a light" is what the engine has **enabled**, not what
`PBR_Renderer` implements. It already carries metallic-roughness *and*
spec-gloss workflows, clearcoat, sheen, anisotropy, iridescence, transmission,
volume, IBL, punctual lights, PCF shadows, order-independent transparency, two UV
sets with per-slot transforms, tonemapping, motion vectors and skinning — nearly
all of it masked off by D24 and switched on ticket by ticket (T0079 lights, T0086
shadows, T0087 IBL, T0096 tonemapping).

So "complete enough" is mostly a question of how fast that is unmasked, and the
base is not thin. What it genuinely **cannot** do at any setting is the list
above: parallax, height, triplanar, tessellation, vertex animation, custom BRDFs.
Those need the surface stage, which is this ticket.

### Re-cut 2026-08-05 — this ticket took the renderer half of T0060

The original split claimed this ticket *"blocks nothing"*. That was false: it
blocks the shape of 60.2 and it blocks T0086. See T0060's "Re-cut" section for
the full reasoning and the table of what moved. In short, T0060 is now the
material **data model**, and everything that decides how a surface is *shaded* —
the standard material shader included — is here.

Following Godot's model, which T0060 already cited: a standard material covers
the common case, **plus the ability to attach a custom shader**. The difference
after the re-cut is that the standard material is *also* written against our own
surface stage rather than being Diligent's fixed shader.

## Done when

- [ ] **Custom shader materials** — attach a shader to a material, declare its
      parameter interface
- [ ] Custom shader parameters appear in the inspector automatically
- [ ] Shader compilation is cached, not repeated every launch
- [ ] A shader that fails to compile renders the **same magenta checkerboard** a
      missing material does, **and** logs the compiler's error — never a crash,
      never a silently wrong surface
- [ ] Shader hot reload in the editor
- [ ] **Custom shaders receive engine intermediates** — visibility (T0093),
      screen position, depth, world position — not just a finished colour
- [ ] Variant growth is bounded by a decision that is written down, not by
      whatever the permutations happen to be
- [ ] **The standard material renders through the surface stage** — absorbed
      from 60.2, and the reason this ticket exists before T0086
- [ ] **Parallax occlusion mapping works on a standard material** — the owner's
      "screen displacement", and the concrete proof the ceiling is lifted
- [ ] A **textured** mesh renders with its pixels asserted — absorbed from 60.11,
      the regression guard T0134 could not write
- [ ] **What is *not* delivered is written down with a trigger**, not left vague
      — see "Tessellation is deferred, with a named trigger"

## Capability answer 2026-08-05 — exactly what the surface stage buys, and what it does not

The owner asked, before agreeing the re-cut: *"if we do 141 we can get parallex
and screen displacement for textures and so on?"* Checked against the vendored
source rather than answered from memory, because the answer **changes the
C1/C2/C3 decision** in 141.0.

`PBR_Renderer::CreatePSO` sets `PSOCreateInfo.pVS` and `pPS` and nothing else.
There is no hull, domain or geometry shader anywhere in the PBR pipeline.

| Technique | What it needs | Delivered by |
|---|---|---|
| **Parallax / POM** — offset UVs by a height map and view vector before sampling | a pixel-shader hook before texture fetch | **C1, C2 or C3.** This *is* the surface stage |
| **Triplanar** projection | same hook | **C1, C2 or C3** |
| Detail maps, layer blending, vertex-colour splatting, dissolve | same hook | **C1, C2 or C3** |
| T0093 vision-based visibility | same hook | **C1, C2 or C3** |
| **Vertex displacement** — push existing vertices along normals | a hook in `RenderPBR.vsh` as well | **C1/C3 patching both shaders, or C2.** Limited by the mesh's existing vertex density |
| **Tessellation displacement** — generate new geometry | **hull + domain shaders, which the pipeline never creates** | **C2 only** |

**That last row is the finding, and it was missed the first time this was
costed.** For tessellation the missing piece is **C++ PSO construction, not
shader text**, so C1 cannot reach it however good the patch is. Only C2, where we
own PSO creation, can add those stages. C1-vs-C2 is therefore a **capability**
choice and not only a maintenance one, which is how it was first described.

### The honest limitation of POM, so nobody is surprised by it

Parallax occlusion mapping is an illusion computed in the pixel shader.
**Silhouettes stay flat** — a POM brick wall still has a straight edge against the
sky — and it degrades at grazing angles. It needs a height map and reasonable UVs.
It is genuinely convincing head-on and genuinely not geometry. If the requirement
is an object whose *outline* changes, that is tessellation, and that is C2.

### Tessellation is deferred, with a named trigger

**Recommendation: C1 first.** POM covers what is actually wanted the
overwhelming majority of the time — depth on brick, cobblestone, tile grout,
panelling — at pixel-shader cost, on flat geometry, with no extra triangles. True
displacement is expensive and is mostly a terrain-and-hero-asset technique.

C2 stays available because owning PSO creation is **additive** to owning the
shader source, not a rewrite of it.

**The trigger for revisiting is explicit rather than vague: when a silhouette
must change.** Recorded so nobody assumes tessellation arrived with the surface
stage, which is the misreading this paragraph exists to prevent.

## Decided-shaped finding 2026-08-05 — DiligentFX's *lighting* is a reusable library; its *material shader* is not hookable

Raised by the owner asking why height maps, parallax occlusion and tessellation
are not in T0060, and whether the engine's own shader ought to be the standard
material. Both halves were checked against the vendored source rather than
guessed, and the answer changes this ticket's shape.

**What is hookable.** `PBR_Renderer::CreateInfo::GetPSMainSource` is a callback
returning `{OutputStruct, Footer}` — enough to change what the pixel shader
*emits* (a G-buffer layout, say). It is **not** a hook before texture sampling,
so it cannot displace UVs and therefore cannot implement parallax. `RenderPBR.psh`
is under `Shaders/PBR/private` with no injection point in
`ReadBaseLayerProperties`. Tessellation is further out still: `PBR_Renderer`
creates no hull or domain shaders at all.

**What is reusable, and this is the part that matters.**
`Shaders/PBR/public/PBR_Shading.fxh` is public and is a library of free
functions: `GetSurfaceReflectanceMR`, `PerturbNormal`, `ApplyPunctualLight`,
`ApplyIBL`, `GetBaseLayerLighting`, `GetSpecularIBL_GGX`, and the sheen and
clearcoat equivalents. It takes surface properties in and returns radiance out.

So the split is already drawn for us, and it is the one every modern engine
uses:

| Stage | Whose | Contains |
|---|---|---|
| **Surface** | **ours, and this ticket's real subject** | sampling, UV displacement, parallax, triplanar, detail maps, blending, anything the artist writes |
| **Lighting** | Diligent's, kept per D24 | BRDF, punctual lights, IBL, sheen, clearcoat, shadows |

### So: should our shader be the standard material?

**Eventually yes, and the architecture permits it cheaply — but not by
rewriting PBR.** Three options were weighed:

- **A — keep `RenderPBR.psh` as the standard material, custom shaders alongside.**
  What T0060 does today. Cheapest, and the ceiling is exactly the one the owner
  hit: nothing can be added to the standard path without forking DiligentFX.
- **B — write our own uber-shader outright.** Reimplements split-sum IBL,
  punctual lighting, shadow filtering, tonemapping, every alpha mode and the
  glTF extensions. That is precisely the body of work D24 declined to write, and
  "do not reinvent wheels" applies at full force.
- **C — own the surface stage, `#include` Diligent's lighting.** Our pixel
  shader main, our surface function, their `PBR_Shading.fxh`. Height mapping,
  parallax and triplanar all become ordinary surface-stage code rather than
  upstream feature requests, and the standard material becomes *our* surface
  function feeding *their* lighting — which is Godot's and Unreal's shape.

**C is the recommendation**, and it reframes this ticket: it is not "bolt custom
shaders onto the side of `PBR_Renderer`", it is "own the surface stage, and the
standard material is the first shader written against it".

### Correction, same day: C is right but it is **not** cheap through the public API

The paragraph above was written after finding `PBR_Shading.fxh` and before
reading `PBR_Renderer::CreatePSO`. Reading it changes the cost, so the estimate
is corrected here rather than left to be discovered by whoever starts 141.1.

`CreatePSO` builds its shader source factory internally:

```cpp
CreateCompoundShaderSourceFactory({&DiligentFXShaderSourceStreamFactory::GetInstance(),
                                   pMemorySourceFactory});
```

DiligentFX's own factory is **first**, `pMemorySourceFactory` carries only the
four generated stubs (`VSInputStruct`, `VSOutputStruct`, `PSOutputStruct`,
`PSMainFooter`), and the whole thing is a local in a **private** method. There is
no public or protected way to add a search path or shadow `RenderPBR.psh`. So
"our PS main, their lighting" cannot be reached by configuration.

What subclassing *does* reach, since `PBR_Renderer` has a virtual destructor and
a useful `protected` section: `DefineMacros`, `GetVSInputStructAndLayout`,
`GetVSOutputStruct`, `GetPSOutputStruct`, `GetPSO`, and
`CreateCustomSignature` (already virtual). That is most of the permutation
machinery, which is the part that would be miserable to rewrite.

So option C splits into three, and they differ by more than taste:

| | What it is | Cost | Risk |
|---|---|---|---|
| **C1** | Vendored patch adding a surface-stage `#include` hook to `PBR_Textures.fxh` / `RenderPBR.psh` | Smallest — a few lines | A patch to carry across every DiligentFX bump, and `04-cross-compile-gotchas.md`'s rule about pinned third-party applies |
| **C2** | Subclass `PBR_Renderer`, own PSO creation and the PS main, `#include` `PBR_Shading.fxh` | Moderate — the protected surface covers the permutation setup, so this is less than it sounds | Ours to maintain against upstream changes to the protected API |
| **C3** | Upstream the hook to DiligentFX | Smallest to carry | Slowest, and not in our control |

**C1 and C3 are the same change**, which is the useful observation: write the
hook as a patch, use it, and offer it upstream. If it lands, the patch
disappears; if it does not, we were carrying it anyway.

### Second correction — "a few lines" was wrong, because Diligent is a submodule

Checked when the owner asked whether T0141 means customising Diligent. It does
for C1 and C3, and **not at all for C2**, and the cost table above understated
C1 badly enough to change the recommendation.

`third_party/DiligentEngine` is a **git submodule pointing at upstream**
(`DiligentGraphics/DiligentEngine`), and **there is no patch mechanism in this
tree** — no `patches/` directory, nothing that applies a diff at configure time.
So "a few lines" is not what C1 costs. C1 costs one of:

- **a fork** — repoint the submodule at `ywadi/DiligentEngine` and rebase our
  hook on every upstream bump, which is a fork of a large engine owned by a small
  studio, indefinitely; or
- **new build machinery** — a patch-apply step that must run before configure on
  every machine and in CI. Note that CI's build-tree cache key is
  `git submodule status --recursive` (T0121), so the patch content would have to
  enter that key too, or a changed patch silently reuses a build tree compiled
  without it.

C2 needs **neither**. It touches no Diligent source at all.

### And a precision about C2: `CreatePSO` is private, so this is not a plain override

Earlier this ticket said "subclass `PBR_Renderer` and own PSO creation" as if the
subclass substitutes shader creation. It cannot: `CreatePSO` is **private**, and
the protected `GetPSO` calls it. What a subclass actually reaches is the
*plumbing* — `DefineMacros`, `GetVSInputStructAndLayout`, `GetVSOutputStruct`,
`GetPSOutputStruct`, `CreateSignature`, `CreateCustomSignature` — which is the
permutation and resource-signature machinery, and is the genuinely unpleasant
part to rewrite.

So C2 is: **subclass for the plumbing and the buffers, create our own shaders and
PSOs beside it, `#include` their public `PBR_Shading.fxh` for the lighting.**
More work than an override; far less than reimplementing PBR; and no fork.

### Decided: C2, and the owner's constraint is what settled it — see D26

The earlier "C1 first" rested on C1 being nearly free, which the submodule makes
false. On the corrected costing:

- C2 is the only option that requires **no modification to a vendored
  dependency**, which is the ongoing cost `CLAUDE.md`'s library rule cares about;
- C2 is the only route to **tessellation**, so choosing C1 buys a second decision
  later;
- the price is that our PS/VS main must be kept in step with DiligentFX's
  structures across upgrades — real, but bounded, and it is *our* code failing to
  compile rather than a patch silently mis-applying.

The argument that survives for C1/C3 is inheritance: with their `RenderPBR.psh`
we get upstream improvements — OIT, new glTF extensions — for free, and with our
own main we do not. **That is the trade to put to the owner**, and it is why this
stays a decision rather than being settled here.

Offering the hook upstream (C3) is worth doing **regardless of which is chosen**:
if it lands, C1 becomes genuinely cheap and the choice can be revisited from a
better position.

**Sequencing consequence, and it is the reason this matters now.** T0086 builds
shadow sampling on `RenderPBR.psh`'s shadow path. Moving to our own PS main later
moves shadow sampling with it. **The decision is materially cheaper before T0086
than after**, and T0045 is shader-independent so it is safe to do first either
way.

Two things are genuinely the owner's call rather than technical, so they are
**left open here rather than decided**: how much shader maintenance the studio
takes on, and whether the standard material moving onto our own surface stage is
worth the regression risk to what T0134 already got working. Ask before starting
141.1.

### Consequence for T0060

**Nothing to change there**, which is the useful part — the material asset is
parameters and texture references, and it is the same asset either way. What
option C would add is a `shader` reference plus its declared parameters, which
T0060 already says it must not foreclose and does not.

## Subtasks

- [ ] 141.1 Custom shader material with a declared parameter interface (was 60.3)
- [ ] 141.2 Reflect shader parameters for the inspector (was 60.4)
- [ ] 141.3 PSO management via `RenderStateCache` and `BytecodeCache` (was 60.5)
- [ ] 141.4 Error shader on compile failure: the shared checkerboard, plus a
      console error naming the shader and the compiler's message (was 60.7)
- [ ] 141.5 Shader hot reload (was 60.8)
- [ ] 141.6 **Define `HpMaterial.fxh` — the contract a game shader compiles
      against** (D27). `HpSurfaceInput` is a *promise*: adding to it later is
      easy, removing from it breaks shipped games, so the list is decided
      deliberately. Must cover at least UVs, world position, normal, view
      direction, depth, and **T0093's per-pixel visibility**, which is the one
      most likely to be forgotten and most expensive to retrofit
- [ ] 141.15 **Unshaded as a game-facing option** — `#define HP_UNSHADED 1`,
      Godot's `render_mode unshaded`. Requested by the owner 2026-08-05 and
      declared in `HpMaterial.fxh` already, so the contract does not have to
      change to add it. **Compile-time, never a runtime branch**: an `if` still
      pays for the lighting code in registers and compile time, which is the
      entire thing unshaded exists to avoid. Costs one PSO permutation bit, and
      that is why it is decided now — variant count is what grows without limit
      here. Distinct from `Material::unlit`, which says the same for a
      *standard* material as data; both end in the same place
- [ ] 141.14 **Generate the shader contract's reference from `HpMaterial.fxh`,
      and gate it in CI** — the same mechanism `tools/gen_api_docs.py` and the
      "API reference is up to date" job give `engine/include/hp/`. Scoped to
      **public shader headers only**, mirroring the include/src split the C++
      side already has: `HpSurface.psh` is no more public than `engine/src/*.cpp`.
      **Not before 141.6 settles** — a generator written against a contract that
      is still moving is work done twice
- [ ] 141.13 **A VFS-backed shader source**, so a game's shader is content like
      any other (D13). Resolution order is **engine, then game, then DiligentFX**
      — a project must not be able to shadow `HpMaterial.fxh` and redefine the
      contract — which makes engine and DiligentFX header names reserved
- [ ] 141.7 **Height mapping and parallax occlusion** — needs the surface stage;
      `PBR_Renderer` has no path for it and `GetPSMainSource` cannot reach before
      texture sampling. Raised from T0060 on 2026-08-05
- [ ] 141.8 **Triplanar projection** — same reason: a surface-stage technique,
      not a material parameter. Raised from T0060 on 2026-08-05
- [x] 141.0 **Decide C1/C2/C3 and record it** — **C2, decided 2026-08-05 by the
      owner**: *"i dont want to modify dilligent"*. Recorded as **D26**, which
      amends D24. Diligent's source is never modified; we own the vertex and
      pixel shader mains and PSO creation, subclass `PBR_Renderer` for the
      plumbing, and `#include` its public `PBR_Shading.fxh` for the lighting
- [ ] 141.10 **The standard material shader**, written against the surface stage
      (was 60.2). Includes the two obligations T0060 inherited from T0134:
      un-inline `GetMaterialPSOFlags` in `SceneRenderer.cpp` the moment any
      extended-material setting is enabled, and decide `EnableEmissive`/`EnableAO`
      deliberately rather than by drift
- [ ] 141.11 **The textured-render regression test** T0134 could not write (was
      60.11) — a textured mesh with its pixels asserted, which is what catches an
      unwritten `PBRFrameAttribs::Renderer` and its garbage `MipBias`
- [ ] 141.12 **Render the missing-material fallback** — T0060.10 defines the
      convention and the three-state table; this draws it, and must reach the
      same code path as 141.4's failed-compile pattern
- [ ] 141.9 **Tessellation / displacement**, or an explicit decision not to.
      Further out than the other two: `PBR_Renderer` creates no hull or domain
      shaders, so this is new pipeline work rather than new shader code

## Decided 2026-08-05, with the owner — one pattern, three causes, and the console tells you which

A shader that fails to compile renders **the same magenta-and-black checkerboard**
as a missing or unloadable material (T0060), and writes an **error to the log**
naming the shader and the compiler's message.

**One visual convention, not three.** Missing material, unloadable material and
failed compile all look identical on the surface. That is deliberate: what a
developer needs from the *pixels* is "something here is wrong, go read the log" —
they do not need to diagnose the cause by squinting at it, and a second pattern
is a distinction nobody remembers under pressure. The console carries the detail,
which is what the console is for.

Reuses `makePlaceholderTexture` (T0023.6) exactly as T0060's fallback does, so
there is one function producing this pattern in the whole engine.

### The trap: log on the transition, never per draw

**A failed shader logged every frame at 60 Hz produces 3,600 lines a minute and
makes the console useless** — which is precisely the opposite of "look at the
logs", and it would arrive as a fix for the very feature that caused it.

So the error is logged **when the compile is attempted and fails**, once per
shader, not from the draw path that substitutes the fallback. The draw path must
be silent: it runs per object per frame and has no business logging at all. A
recompile — a hot reload (141.5), or a first load after a fix — is a new attempt
and logs again, which is correct and is how a developer sees it clear.

### Cheap variant, not taken

Tinting the checks differently per cause — magenta for a missing asset, red for a
failed compile — is about one line and keeps the at-a-glance distinction. Not
taken, because the owner asked for one pattern and the log already answers "which".
Recorded so it is a decision someone can revisit rather than an option nobody saw.

## Answered 2026-08-05 — the shader language, and how parameters are declared

The owner asked: *"question on custom shader parameter, will it be in the HSLS?
how will it be defined where to place them and refference them?"*

### The language is HLSL, and that is already settled

Not reopened here, but restated because it is the premise everything below rests
on. **D2** makes OpenGL the only fallback on Windows, and Diligent's portable
path is **HLSL** — compiled through glslang for Vulkan, converted for GL by its
HLSL2GLSL converter. GLSL written directly does not reach both backends through
one pipeline. Whatever is chosen becomes the language every custom material is
written in **forever**, so it is worth the sentence: it is HLSL, and 141.1 must
also record which HLSL subset is safe on the GL path, because the converter has
documented limitations.

### How parameters are declared: reflect the buffer, annotate the source

Two mechanisms exist and **neither is sufficient alone**, which is the whole
answer:

- **Reflect the constant buffer** — `IShader::GetResourceDesc` and the shader
  resource variables give names, types and offsets. This is *authoritative* and
  cannot drift from the shader, because it is derived from the compiled shader.
  What it cannot know is ranges, defaults, tooltips, or whether a `float4` is a
  colour or four unrelated numbers.
- **An annotation block in the shader source** for exactly those. Godot's
  `uniform float x : hint_range(0, 1) = 0.5;` is the model to copy.

**Reflection alone** gives an inspector full of unlabelled, unbounded floats.
**Declaration alone** drifts from the shader the first time somebody edits one
and not the other. So: layout from reflection, presentation from annotation, and
a parameter present in the annotation but absent from the compiled buffer is a
**warning**, not a silent omission.

### Where the values are placed on the GPU

`PBR_Renderer` already reserves a configurable custom-data region —
`GetPBRPrimitiveAttribsSize(Flags, CustomDataSize = sizeof(float4))`, with
`PSO_FLAG_ENABLE_CUSTOM_DATA_OUTPUT` — so there is a per-draw channel to shader
parameters **without inventing a buffer**. `Material::ShaderAttribs::CustomData`
is a second, per-material `float4` of the same kind. Check whether those cover
the common case before adding a third constant buffer; a custom shader with four
floats of parameters should not cost a buffer of its own.

## D24 must be revisited as part of 141.0, not left to erode

**D24 says materials map onto `PBRMaterialShaderAttribs`.** That decision was
taken when `RenderPBR.psh` *was* the material shader. If 141.0 chooses to own the
surface stage, the premise changes: the shader consuming the material becomes
ours, and the parameter set is then bounded by what our surface function reads
rather than by what Diligent's struct happens to carry.

**The decision may well survive** — reusing their struct layout keeps their
lighting functions callable with no translation, which is a good reason to keep
it. What must not happen is the decision quietly ceasing to be true while the
log still asserts it. **141.0 amends D24 explicitly, either way.**

The same applies to the omission recorded in `11-material-format.md`:
`SpecularFactor` is absent because it is read only under
`PBR_WORKFLOW_SPECULAR_GLOSSINESS` (`RenderPBR.psh:159`). If the surface stage
becomes ours, that justification is no longer automatic and should be restated
rather than inherited.

## Decided 2026-08-05, with the owner — the Godot model, recorded as D27

A game shader includes **one engine header** and fills in a function; the engine
owns the `main`. It never includes a DiligentFX header.

The rejected alternative was letting game shaders include `PBR_Shading.fxh`
directly, which is cheaper and would have made DiligentFX's internals part of
this engine's public contract — so every upstream rename would silently break
every shipped game shader. That is D26's trap from the other side: D26 stopped us
being unable to *change* DiligentFX, and this stops us being unable to *update*
it.

**The accepted cost is named in D27 rather than left to be discovered:** a
developer who wants something the contract does not expose waits for us to
expose it. The mitigation is a generous `HpSurfaceInput`, which is why 141.6 is
a deliberate decision and not a list that grows by accident.

## Where this stands (2026-08-05)

**Done and verified on hardware** — RTX 2080, Vulkan, zero skipped tests:

- **141.0** — the decision, recorded as **D26**.
- **141.6** — `HpMaterial.fxh`, the contract, recorded as **D27**. `HP_UNSHADED`
  is declared, so 141.15 needs no contract change.
- **The shader plumbing** — shaders embedded in the binary, a compound source
  factory, and `compileEngineShader`.
- **`SurfacePipeline`** — a pipeline built from the engine's own pixel shader
  and accepted by the device, which is the claim D26 rests on.
- **The shading path** — the surface stage calling DiligentFX's public
  `GetSurfaceReflectanceMR` / `ApplyPunctualLight` / `ResolveLighting`, with the
  `HP_UNSHADED` branch compiling beside it.

**Not done, and the engine's output is unchanged so far.** `SceneRenderer` still
draws through `PBR_Renderer`'s own pipeline; nothing in the render path uses
`SurfacePipeline`. Everything above is proven to build and be accepted — none of
it is proven to *draw the right thing*, and those are different claims.

### The next step, and why it is shaped this way

**Switch `SceneRenderer` over, together with a pixel assertion that the new path
matches the old one.** Not one then the other: "it renders" and "it renders the
same thing" are different claims, and only the second is worth making about a
renderer. The gpu bucket runs here, so this can be a real pixel comparison rather
than a submission count — which is exactly the guard T0134 could not write and
141.11 exists to provide.

Only after that do 141.7 (parallax) and 141.8 (triplanar) mean anything, because
until the engine draws through this shader they would be changing a code path
nothing executes.

### Remaining, in dependency order

| | | Blocked on |
|---|---|---|
| **141.10 close** | swap `SceneRenderer` over | — |
| **141.11** | textured-render pixel guard | the swap |
| **141.12** | draw the missing-material checkerboard | the swap |
| **141.7 / 141.8** | parallax + height, triplanar | the swap |
| **141.13** | VFS-backed shader source | — |
| **141.1 / 141.2** | custom shader asset, parameter reflection | 141.13 |
| **141.15** | `HP_UNSHADED` as a PSO permutation | 141.1 |
| **141.3** | `RenderStateCache` / `BytecodeCache` | — |
| **141.4** | error shader on compile failure | 141.1 |
| **141.5** | shader hot reload | 141.13 |
| **141.14** | generated shader-contract docs, gated in CI | 141.6 settling |
| **141.9** | tessellation | deferred: *when a silhouette must change* |

## Notes / findings

### D26's mechanism is proven on hardware, 2026-08-05 — first increment of 141.10

**The load-bearing assumption held**, and it was worth checking before building a
renderer on it: a shader in `engine/shaders/` compiles on a real device and
`#include`s a DiligentFX **public** header, with neither side modified.

Measured on an **RTX 2080 under Vulkan**, not asserted — and the skip path was
checked too, because a gpu test that quietly skips also passes. Zero skips, and
the negative case logs `shader 'ThisShaderDoesNotExist.psh' did not compile`.

**GPU tests run on this machine**, which changes what "verified" can mean for the
rest of this ticket: 141.10 and 141.11 can assert *pixels* rather than "it
compiled". That matters more here than anywhere else in the engine — a shader
that compiles and draws the wrong thing is exactly the failure T0134 spent a
ticket on.

What landed:

- `cmake/hp_embed_shaders.cmake` — engine shaders are **compiled into the
  binary**, the same way DiligentFX embeds its own. So D13 stays true by there
  being no read at all, `dist` has nothing extra to install, and a missing shader
  is a build error rather than a black screen. **Diligent's own generator is not
  reused**: it shells out to a Python `file2string` and calls
  `find_package(Python3 REQUIRED)`, which the engine build does not currently
  need and the offline-configure CI job would have to grow. CMake does the same
  job with a **raw string literal** and no escaping.
- `hp::createEngineShaderFactory` — a compound factory, **ours first then
  DiligentFX's**, so a name collision resolves to ours deliberately and not by
  accident after an upgrade.
- `hp::compileEngineShader` — a validation pass, not the render path. It is also
  the seed of **141.3's warm-up**: the same walk over the embedded set is what
  fills a `RenderStateCache` before the first frame instead of hitching on it.

Two things deliberately *not* done, so nobody mistakes this for the shader:

- **`HpSurface.psh` does nothing yet.** It returns the missing-material magenta
  and includes `PBR_Structures.fxh` and nothing more. Grown deliberately: the
  plumbing and the shading fail in completely different ways, and they are far
  easier to tell apart when they land in separate commits.
- **`PBR_Shading.fxh` is not included yet.** It needs the macro set
  `PBR_Renderer::DefineMacros` produces, which arrives with the PSO work in
  141.10 proper.

**No test includes a Diligent RHI header**, and this one does not either. D21
exports only the math subset to consumers, and widening that for a test's
convenience would erode a boundary the engine keeps on purpose — so the device
pointer passes straight through `compileEngineShader` without being dereferenced
on the test's side. That is why the API is shaped as it is rather than handing
back an `IShader*`.

### 141.10's mechanism works end to end, 2026-08-05 — a real pipeline from our shader

**The device accepts a pipeline built from the engine's own pixel shader.**
That is a strictly stronger claim than the compile check the previous increment
made, and it is the one D26 actually rests on: a shader can compile in isolation
and still be refused once it has to agree with a vertex layout, a resource
signature and a render-pass format — which is exactly the seam between our pixel
shader and DiligentFX's vertex shader.

`SurfacePipeline` subclasses `PBR_Renderer` to reach the plumbing that is
`protected` and would be miserable to reimplement: `DefineMacros` (**without
which `PBR_Shading.fxh` does not compile at all**), the generated VS input/output
and PS output structs, `m_ResourceSignatures`, and the frame/primitive/material
buffers. `CreatePSO` is *private*, so this is not an override — the subclass
reuses the machinery and builds its own pipelines beside it, and deliberately
does **not** use `GetPsoCacheAccessor`, whose cache builds pipelines from *their*
shaders.

**The vertex shader is still DiligentFX's, deliberately.** The surface stage is a
pixel-shader concept — parallax, triplanar and blending are all per fragment — and
`RenderPBR.vsh` already produces exactly the `VSOutput` our pixel shader
consumes. **141.7's vertex displacement is the only thing that should change
that**, and the file name is a named constant so it is a one-line change.

**A test was retired rather than weakened.** The standalone-compile case proved
our source reached the compiler; `HpSurface.psh` is no longer standalone, since
it consumes the per-pipeline generated structs, so compiling it alone now fails
*correctly*. Keeping it alive by pointing it at a different shader would have
been testing scaffolding. The pipeline case exercises the same includes and then
requires the device to accept the result.

**The shading landed in the increment after this one.** `HpSurface.psh` now
reads the material attribs, runs the surface stage, and calls DiligentFX's public
`GetSurfaceReflectanceMR` / `ApplyPunctualLight` / `ResolveLighting`. Both the
shaded and `HP_UNSHADED` paths compile and the device accepts the pipeline.

One include is worth defending: **`RenderPBR_Structures.fxh` is private and this
shader takes it anyway.** `PBRFrameAttribs` and `PBRPrimitiveAttribs` are the
*layouts of the constant buffers `PBR_Renderer` fills*, and this engine already
uses those buffers — `SceneRenderer.cpp` includes the same file on the C++ side
to write them. It is the coupling we already had, not a new one, and D26 accepted
it in as many words: keeping their struct layouts is what keeps their lighting
callable with no translation. **Layout versus behaviour is the distinction that
matters** — a layout change breaks the C++ that writes the buffer and the shader
that reads it in the same build, loudly. Depending on a private *function* would
be the thing to avoid, and this shader does not.

**`NdotV` is clamped away from zero**, which is not defensive padding: a normal
facing exactly edge-on makes the BRDF divide by it, and the result is NaN pixels
reaching the target as black or white speckle depending on the backend.

**Still not switched on.** `SceneRenderer` continues to draw through
`PBR_Renderer`'s own pipeline; nothing in the render path uses `SurfacePipeline`
yet, so the engine's output is unchanged. That swap plus a pixel assertion that
it matches what the old path drew is what closes 141.10 — see "Where this
stands".

### Build wiring worth knowing

`Diligent-GraphicsTools` is now linked: it supplies
`CreateMemoryShaderSourceFactory` and `CreateCompoundShaderSourceFactory`, and it
is also where `RenderStateCache` lives, which **141.3** will want. DiligentFX
exports its root PUBLIC, which reaches `PBR/interface` but **not**
`Utilities/interface` where `DiligentFXShaderSourceStreamFactory` lives, so that
one directory is named explicitly — a move upstream becomes a build error here
rather than a silently wrong include.

## Inherited notes, moved from T0060 rather than re-derived

**`RenderStateCache.hpp` and `BytecodeCache.h` already exist in
`Graphics/GraphicsTools`** and solve shader compile hitching and startup cost.
Use them rather than building a cache — this is a significant piece of work
Diligent has already done, and rebuilding it is the waste `CLAUDE.md`'s
"do not reinvent wheels" rule names directly.

**Shader parameter reflection is separate from C++ reflection (T0053).** Getting
a custom shader's uniforms into the inspector means reflecting the *shader*.
Check what `IShader::GetResourceDesc` and the shader resource variables expose
before writing a parser.

**Custom shaders must reach engine intermediates, not just material parameters.**
T0093 (vision-based visibility) needs a per-pixel visibility factor *inside* the
material shader to dim, hide or dither. If shading is a sealed pipeline that
consumes lights and emits pixels, that capability gets bolted on as a
post-process hack later. Design the interface with documented inputs —
visibility, screen position, depth, world position — from the start. **This is
the requirement most likely to be forgotten and most expensive to retrofit.**

**Variants are the thing that grows without limit.** Every optional feature
doubles the permutation count. Decide early whether variants are enumerated
ahead of time or compiled on demand, and write the decision down.

## Notes / findings
