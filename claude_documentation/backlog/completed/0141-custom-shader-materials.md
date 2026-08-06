# T0141 — The surface stage: the standard material shader, and custom ones

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 455 |
| **Created** | 2026-08-05 |
| **Blocked by** | T0060.1 + T0060.6 only — the material *asset* and per-surface assignment. **Not** the rest of T0060, which was re-cut into this ticket on 2026-08-05 |
| **Blocks** | **T0086** — shadow sampling must be written against *our* pixel shader, not `RenderPBR.psh` (141.0 is decided; 141.10 is what T0086 now waits on). Height mapping, parallax occlusion, triplanar and vertex displacement — 141.7/141.8 — which no material parameter can express |
| **Refs** | [../completed/0060-material-system.md](../completed/0060-material-system.md) (split from it), [../completed/0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md), T0093, T0053, T0094, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D24, [../open/0145-lighting-stage-own-the-light-loop.md](../open/0145-lighting-stage-own-the-light-loop.md) — the lighting rung above this ticket's surface stage (D30), [../open/0146-vertex-stage-hook.md](../open/0146-vertex-stage-hook.md) — owns the `kVertexShader` move this ticket's 141.7 note anticipates, [../open/0147-engine-intermediates-for-shaders.md](../open/0147-engine-intermediates-for-shaders.md) — delivers the *sampled* intermediates this ticket's Done-when promises, [../open/0151-shader-variants-and-compile-cost.md](../open/0151-shader-variants-and-compile-cost.md) — where the "variant growth bounded by a written decision" Done-when gets its decision; [../inprogress/0152-winding-convention.md](../inprogress/0152-winding-convention.md) — **corrects 141.12's winding diagnosis** (D33): the inversion was the test assets' winding contradicting their normals, not the engine's chain; the cull-`FRONT` workaround reverts there |

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

- [~] **Custom shader materials** — attach a shader to a material, declare its
      parameter interface. *Attaching is done and proven (142.15: `Material::shader`,
      a `.slang` module overriding one method renders exactly). **Declaring a
      parameter interface is not**: a custom module has no parameters of its own
      yet, and the mechanism is undecided — descoped to **T0032.8** with the
      decision intact.*
- [x] Shader compilation is cached, not repeated every launch (141.3, 2026-08-06 — persistent SPIR-V cache, measured 11.9s → 2.3s on the heaviest gpu case)
- [x] A shader that fails to compile renders the **same magenta checkerboard** a
      missing material does, **and** logs the compiler's error — never a crash,
      never a silently wrong surface (141.4, 2026-08-06 — asserted with a log
      capture: one error across three frames)
- [~] **Custom shaders receive engine intermediates** — visibility (T0093),
      screen position, depth, world position — not just a finished colour.
      *Delivered in halves: `ScreenPos`, `WorldPos`, `CameraPos`, `ViewDir` and
      the rest of `HpSurfaceInput` are in the contract and generated into
      `docs/shaders/`. The **sampled** half — scene depth, scene colour, T0093's
      visibility, game-fed textures — is **T0147**, which exists because no
      subtask here delivered it.*
- [x] **The standard material renders through the surface stage** — absorbed
      from 60.2, and the reason this ticket exists before T0086 (141.10,
      2026-08-05 — `SceneRenderer` draws through `SurfacePipeline`, and a red
      quad under a white light is (211, 144, 144) through both shaders: the
      same bytes, not "close")
- [x] **Parallax occlusion mapping works on a standard material** — the owner's
      "screen displacement", and the concrete proof the ceiling is lifted
      (141.7, 2026-08-06 — differential pixel test on an oblique rock quad)
- [x] A **textured** mesh renders with its pixels asserted — absorbed from 60.11,
      the regression guard T0134 could not write (141.11, 2026-08-06 — rock
      centre (108, 105, 95) variation 14.13 over 21,781 unique colours, plus a
      per-channel debug view that immediately found three bugs)
- [x] **What is *not* delivered is written down with a trigger**, not left vague
      — "Tessellation is deferred, with a named trigger", and the trigger was
      **sharpened rather than inherited** when 141.9 moved to T0146.7: it is now
      *when a silhouette must change at a density the mesh does not carry*,
      because T0146 itself delivers silhouette change at the density a mesh has

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

- [→] 141.1 **Moved to T0142.15.** A custom shader material is a `.slang`
      module implementing `IHpMaterial` (D28); the asset shape follows from the
      language, so designing it against HLSL would be work done twice.
      *(Numbered 2026-08-06 — this line previously said only "moved to T0142"
      with no subtask on the receiving end, which is the one-way-reference
      failure the backlog rules exist to prevent.)*
- [→] 141.2 **Moved to T0142.9, and on to T0032.8** *(2026-08-06 — T0142
      descoped its editor half to the ticket that builds an editor; the
      undecided mechanism question went with it as text, not as a pointer)*.
      Slang reflects parameters from *source*,
      so the inspector works before a shader compiles — and Diligent already
      reflects constant-buffer contents at runtime
      (`LoadConstantBufferReflection`, one bool, currently false). Both beat
      annotating and parsing, which was this subtask's plan
- [x] 141.3 PSO management via `RenderStateCache` and `BytecodeCache` (was 60.5).
      **T0142 measured the argument for this** (2026-08-06): slang's cold
      compile is 2–4x slower than glslang's on identical permutations — the
      debug-channel gpu test went 3.2s → 7.9s — so the cache is what turns
      that from a startup cost into a one-time miss. Consider pulling forward.
      **Done 2026-08-06 as a persistent SPIR-V cache on `IBytecodeCache`**;
      `IRenderStateCache` was evaluated and deliberately not adopted — with
      IBL off the base constructor compiles nothing through it, and our
      `build()` talks to the device directly, so the slot would cache nothing
      today (reasoning at the construction site; revisit trigger: T0087's
      IBL, or PSO creation showing up in a measurement). Measured on the gpu
      suite: the channel-inspection case 11.88s uncached → 2.25s warm, lit
      surface 3.24s → 0.67s — full numbers in the notes
- [x] 141.4 Error shader on compile failure: the shared checkerboard, plus a
      console error naming the shader and the compiler's message (was 60.7).
      **Done 2026-08-06**: a custom module that fails to compile renders
      141.12's fallback through the same `ensureFallbackBinding` path — one
      pattern, three causes. The compiler's message is logged by the pipeline
      on the attempt (the null is cached, so once per permutation), and one
      renderer line names the module, once per module — measured: across
      three rendered frames, exactly one compiler error and one substitution
      line, and the draw path stays silent. Never a crash, never invisible:
      the frame is loud magenta
- [→] 141.5 **Moved to T0142.8, and on to T0032.7** *(2026-08-06, same
      descope)* — Slang's runtime API is built for it
- [x] 141.6 **`HpMaterial.fxh` — the contract a game shader compiles
      against** (D27). `HpSurfaceInput` is a *promise*: adding to it later is
      easy, removing from it breaks shipped games, so the list is decided
      deliberately. Must cover at least UVs, world position, normal, view
      direction, depth, and **T0093's per-pixel visibility**, which is the one
      most likely to be forgotten and most expensive to retrofit.
      **Delivered as HLSL, and superseded in form by T0142.2** (D28): the field
      list, the "adding is free, removing breaks shipped games" promise and the
      "nothing is exposed before the system behind it exists" rule all carry over
      unchanged — only the language does not.
      **The file is `engine/shaders/HpMaterial.slang` since T0142.13**
      (2026-08-06), and the rename came with a correction worth knowing here:
      the free `HpSurface` function this subtask declared **cannot be defined by
      a game** and has not been since 142.15 — the engine's own body is in scope
      when the module is included. D27's function-style hook lives on as
      `IHpMaterial.surface()`; the declaration is gone with the claim
- [→] 141.15 **Moved to T0142.16** *(numbered 2026-08-06; was an orphan)* —
      under D28 this is an interface method with a
      default implementation, not a PSO permutation bit and not a macro, so the
      design below is superseded even though the requirement is not.
      **Original:** **Unshaded as a game-facing option** — `#define HP_UNSHADED 1`,
      Godot's `render_mode unshaded`. Requested by the owner 2026-08-05 and
      declared in `HpMaterial.fxh` already, so the contract does not have to
      change to add it. **Compile-time, never a runtime branch**: an `if` still
      pays for the lighting code in registers and compile time, which is the
      entire thing unshaded exists to avoid. Costs one PSO permutation bit, and
      that is why it is decided now — variant count is what grows without limit
      here. Distinct from `Material::unlit`, which says the same for a
      *standard* material as data; both end in the same place
- [x] 141.14 **Generate the shader contract's reference from `HpMaterial.slang`,
      and gate it in CI** — the same mechanism `tools/gen_api_docs.py` and the
      "API reference is up to date" job give `engine/include/hp/`. Scoped to
      **public shader headers only**, mirroring the include/src split the C++
      side already has: `HpSurface.psh` is no more public than `engine/src/*.cpp`.
      **Not before 141.6 settles** — a generator written against a contract that
      is still moving is work done twice.
      **Done 2026-08-06, once 142.13 had settled the contract file.**
      `tools/gen_shader_docs.py` → `docs/shaders/`, wired into `zig build docs`
      and gated by the same CI job, which now checks `docs/` rather than
      `docs/api` and uses `git status --porcelain` so a *new* page counts as
      drift. The boundary is **declared in the source and mechanically
      enforced** — every `.slang` carries `// hp-shader-doc: public|private` or
      the generator refuses to run — because there is no include/src split in
      `engine/shaders/` to infer it from. Two findings in the notes: the
      contract genuinely spans two files, and the generator's first output was
      confidently wrong in exactly the way `gen_api_docs.py`'s stale-param
      check exists to catch
- [→] 141.13 **Moved to T0142.14** *(numbered 2026-08-06; was an orphan)* —
      the resolution order below still holds
      (engine, then game, then DiligentFX; engine and DiligentFX names reserved),
      but it is enforced through `ISlangFileSystem` rather than Diligent's
      factory, because Slang compiles first. **Original:** **A VFS-backed shader source**, so a game's shader is content like
      any other (D13). Resolution order is **engine, then game, then DiligentFX**
      — a project must not be able to shadow `HpMaterial.slang` and redefine the
      contract — which makes engine and DiligentFX header names reserved
- [x] 141.7 **Height mapping and parallax occlusion** — needs the surface stage;
      `PBR_Renderer` has no path for it and `GetPSMainSource` cannot reach before
      texture sampling. Raised from T0060 on 2026-08-05.
      **Done 2026-08-06**: `Material::heightTexture`/`heightScale`, the
      engine's own `g_HeightMap` signature slot (added through
      `CreateCustomSignature`, the designed extension point), a POM march in
      the surface stage behind an `HP_USE_HEIGHT_MAP` permutation, and — the
      stress-test answer — **`IHpMaterial` grew `surfaceCoordinates`**, the
      before-sampling hook D26 was decided for, which the interface turned out
      not to have. Measured: zero-scale vs no-map frames **bit-identical**
      (diff 0.0); a 0.08 scale moves the frame by 15.69 mean abs per-channel
      on an oblique rock quad, and the PPM looks carved. See notes
- [x] 141.8 **Triplanar projection** — same reason: a surface-stage technique,
      not a material parameter. Raised from T0060 on 2026-08-05.
      **Done 2026-08-06**: `Material::triplanar`/`triplanarScale`, an
      `HP_TRIPLANAR` permutation, world-space projection with a sharpened
      three-plane blend in the standard material's defaults. **Proven on a
      mesh with no UVs at all** — the same material sampled the normal way is
      flat (variation 0.0, the getters return the factor); triplanar covers
      it in rock (variation 11.38). The one place the surface stage samples
      textures itself, argued per D26's per-function rule: DiligentFX's
      getters are UV-set-bound and cannot express it. Normal map deliberately
      ignored in triplanar mode; see notes
- [x] 141.0 **Decide C1/C2/C3 and record it** — **C2, decided 2026-08-05 by the
      owner**: *"i dont want to modify dilligent"*. Recorded as **D26**, which
      amends D24. Diligent's source is never modified; we own the vertex and
      pixel shader mains and PSO creation, subclass `PBR_Renderer` for the
      plumbing, and `#include` its public `PBR_Shading.fxh` for the lighting
- [x] 141.10 **The standard material shader**, written against the surface stage
      (was 60.2). Includes the two obligations T0060 inherited from T0134:
      un-inline `GetMaterialPSOFlags` in `SceneRenderer.cpp` the moment any
      extended-material setting is enabled, and decide `EnableEmissive`/`EnableAO`
      deliberately rather than by drift
- [x] 141.11 **The textured-render regression test** T0134 could not write (was
      60.11) — a textured mesh with its pixels asserted, which is what catches an
      unwritten `PBRFrameAttribs::Renderer` and its garbage `MipBias`
- [x] 141.12 **Render the missing-material fallback** — T0060.10 defines the
      convention and the three-state table; this draws it, and must reach the
      same code path as 141.4's failed-compile pattern.
      **Done 2026-08-06, and it turned out to be the whole three-state table,
      not just the third row**: drawing the fallback needs exactly the
      machinery that renders any `hp::Material` (convert to `GLTF::Material`,
      bind textures by GUID, key the PSO), so `Assigned` materials reach the
      pixels with it — which item 6's `shader` field presupposes anyway.
      Asserted on hardware: unlit magenta checks with **no light in the
      scene**, warning once per GUID across three frames; an assigned unlit
      green material renders exactly (0, 255, 0). Found and fixed on the way:
      single-sided culling was inverted engine-wide (see notes) and the
      placeholder/pool textures were 2D where the shader wants 2D arrays

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

### 141.10 closed 2026-08-05 — the engine draws through its own shader, pixel-identical

**`SceneRenderer` now renders through `SurfacePipeline`.** The regression guard is
`lit_surface_test.cpp`, which already existed and already documented its measured
value: a red quad under a white light was **(211, 144, 144)** through
DiligentFX's shader, and it is **(211, 144, 144)** through ours. Not "close" —
the same bytes. That is the claim worth making about a renderer, and it is why
the swap and the comparison landed together rather than one then the other.

Two bugs found, both of which a submission count would have called success. The
first version of this rendered pure clear colour with `stats.submitted == 1`, no
validation errors and a pipeline that built cleanly.

**1. The PSO key drives pipeline state, not just shader macros.**
`PBR_Renderer::CreatePSO` reads `Key.GetCullMode()` into the rasterizer
(`PBR_Renderer.cpp:2151`) and the alpha mode into the blend state. The first
version consulted neither, so every draw got `GraphicsPipelineDesc`'s default of
`CULL_MODE_BACK` — and the **double-sided** quad was back-face culled. Both are
now taken from the key.

**2. The two-sided normal flip, which cost the longest.** A double-sided surface
seen from behind has a geometric normal pointing *away* from the viewer, so every
`N·L` is negative, clamps to zero, and the surface renders **pure black** with
the light present, the loop running and the material read correctly.
`GetPerturbNormalInfo` does this on DiligentFX's path, which is why the old
renderer lit the quad and ours did not. Fixed with `SV_IsFrontFace`.

**How they were found is the reusable part.** Guessing was tried and got nowhere;
what worked was three narrowing diagnostics that each split the space in half —
a constant colour (does the fragment shader run at all: no), depth and cull
disabled (which state is rejecting: cull), and forcing `HP_UNSHADED` (is the
material read right: yes, so it is the lighting). The last one output the light
count and `N·L` **as colours**, which said light count 1 and `N·L` 0 in a single
frame. A shader has no log; the target is the only instrument, and using it
deliberately beats reading the code again.

**`HP_UNSHADED` is verified working** as a side effect of that third diagnostic:
forcing it produced exactly `(255, 0, 0)`, the authored base colour with no
lighting applied. 141.15 is the PSO permutation for it, not the shader path.

## Where this stands (2026-08-05)

**Done and verified on hardware** — RTX 2080, Vulkan, zero skipped tests. **The
engine now draws every mesh through its own pixel shader**, pixel-identical to
what it drew before:

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

**The surface stage is live**, so 141.7 (parallax) and 141.8 (triplanar) now have
a code path to act in — which they did not before the swap, and which is why they
were sequenced after it.

### The surface stage samples textures — closed 2026-08-06, with pictures

The owner supplied two ambientCG sets (rock, metal), downscaled and ORM-packed
into `test_assets/derived/` by `tools/pack_test_textures.py`. Before this work a
textured quad came back **flat grey, exactly one unique colour in the frame,
variation 0.00**. It now comes back as rock:

| set | centre RGB | variation | unique colours |
|---|---|---|---|
| rock (dielectric) | (108, 105, 95) | **14.13** | 21781 |
| metal | (20, 19, 19) | **8.92** | 1013 |

`zig build test -Dtest=gpu` — **21/21 on both targets**, zero skipped. Full suite
**302 fast + 89 integration**, both targets, green.

#### The cause was not the one recorded the day before, and the correction matters

The previous note here blamed `ensureBindings` skipping unfilled slots. That was
**a** bug and it is fixed, but it was not the one holding this up. The real one
was a field nobody set:

**`PBR_Renderer::CreateInfo::TextureAttribIndices` defaults to all `-1`, and
`SceneRenderer` never assigned it.** The array maps each renderer texture slot to
an index in the glTF material's attribute table, and **-1 means "this renderer
does not use that texture"** to every one of its three readers. So all three did
nothing, in a release build, without logging:

* `DefineMacros` never emitted `BaseColorTextureAttribId` and friends, so a
  shader could not name its own texture attributes;
* `ensureBindings` bound nothing at all, because every slot looked disabled;
* `WritePBRMaterialShaderAttribs` wrote **no** per-texture attributes, so UV
  selectors, slices and transforms never reached the GPU.

`GLTF_PBR_Renderer` fills the array in a private wrapper struct around its own
`CreateInfo`, so a renderer built on `PBR_Renderer` directly — which is what D26
chose — has to do it itself. Nothing warns.

**It was found by a shader failing to compile**, not by an image looking wrong:
`'BaseColorTextureAttribId' : unknown variable`. Had the shader been written to
tolerate a missing macro, this would have shipped as a texture path that silently
sampled the wrong thing. Worth remembering the next time a shader constant looks
like scaffolding.

#### The `CreateInfo` is now built in one place, because two is how it got in

`SceneRenderer::create` and `buildEngineSurfacePipeline` each configured their own
`PBR_Renderer::CreateInfo` by hand. They drifted, and the field they both omitted
was this one — so the pipeline test proved a pipeline that was not the shipping
one. `SurfacePipeline::configure(CreateInfo&)` is now the single definition and
both call it. `InputLayout` stays the caller's job: `CreateInfo` holds a *pointer*
to the layout elements, so a layout built inside the function would dangle.

#### What the shader now does, and what stayed public

`HpSurface.psh` declares `g_BaseColorMap`, `g_PhysicalDescriptorMap` and
`g_NormalMap` as `Texture2DArray` with their per-slot samplers — the names
`PBR_Renderer::CreateSignature` registers, which are not ours to choose. The
sampling is **ours**, written against public headers only:

* `HpMaterialTextureUv` — UV-set selection and the material's UV transform,
  split out as its own function because it is the one place a coordinate is
  decided, which is exactly what 141.7's parallax displaces;
* `HpSampleMaterialTexture` — wrap-mode clamping and `SampleBias` with
  `g_Frame.Renderer.MipBias`;
* base colour through `TO_LINEAR`, roughness from green and metallic from blue,
  and normal mapping through the **public** `GetPerturbNormalInfo` /
  `PerturbNormal`.

Only `PBRMaterialTextureAttribs` and its `Unpack*` helpers were needed from
DiligentFX, and both are in **public** `PBR_Structures.fxh`. `PBR_Textures.fxh`
stayed uncluded, as D26 intends.

#### Three things worth knowing that cost time

**The always-on permutation is what makes the default bindings load-bearing.**
`SceneRenderer` compiles every material with colour, normal and phys-desc enabled
— one permutation per material rather than one per texture combination — so an
untextured material still runs a shader declaring all three. Unbound texture
arrays sample **zero**, which is black, not white. Both halves are needed: the
C++ binds a default view, and the shader returns the factor untouched when the
material's UV selector is `-1`.

**`GetPerturbNormalInfo` applies the two-sided flip itself.** Passing it the
already-flipped `surfaceIn.Normal` flips twice and hands back the away-facing
normal on exactly the back faces the flip exists to fix. The geometric normal is
now kept unflipped alongside. The bug this produces — correct from the front,
inverted from behind — reads as a normal-map handedness problem and is not one.

**The sRGB gap T0134 recorded is closed, in the shader rather than the view.**
The glTF loader creates `RGBA8_TYPELESS` textures whose default shader view is
`RGBA8_UNORM` — linear — so an sRGB-authored albedo read through it is too
bright. The free fix is an sRGB *view*, which is what `GetPBRTextureSRV` makes,
and that function is **file-static in DiligentFX's .cpp**: not merely private but
invisible outside the translation unit. So `TexColorConversionMode` is set to
`SRGB_TO_LINEAR` and `TO_LINEAR` runs per colour sample. T0097 can move it into
the view later without the shader noticing.

#### Not verified, and honest about it

**The metal set renders very dark** — centre (20, 19, 19). That is consistent
with correct behaviour rather than evidence of it: a metal has no diffuse
response, and with `EnableIBL = false` there is no environment for it to reflect,
so a single directional light leaves it nearly black with bright specks. **It has
not been proven that the metallic channel is wired correctly** — only that the
frame varies and is not the clear colour. **T0087's environment lighting is what
makes that testable**, and this ticket should be referenced from there.

`HpMaterial.slang` claims `Time` needs "a frame-wide clock field, which
`PBRFrameAttribs` has no room for yet". That is **wrong**:
`PBRRendererShaderParameters` already carries a `Time` field. Nothing depends on
the claim, but the table should be corrected when 141.6's contract is next
touched.

### 2026-08-06, second session — the sampling is theirs, and three bugs the debug views found

**`HpSurface.psh` now includes `PBR_Textures.fxh` and calls their getters.** The
owner asked why we were not including their implementation and building on it,
and the answer was that the option had never been evaluated — this ticket had
considered *shadowing* their header and rejected it, and never considered simply
*including* it. 228 code lines to **165**, output still byte-identical to
`RenderPBR.psh`. Recorded as the D26 revision.

The hook survives: every getter takes `VSOutput` **by value**, so 141.7 and 141.8
displace a copy's UVs and pass that in.

**Slang was evaluated and rejected** — its `extension` mechanism applies only to
struct types, and DiligentFX is free functions calling free functions by static
name, so there is no seam to attach to. Full reasoning in the decision log,
including what was *not* verified.

#### `SurfaceDebugView` — and the three things it caught immediately

A per-view debug channel on `Camera`, reflected, written to
`PBRRendererShaderParameters::DebugView`, implemented in `HpSurface.psh` as a
**runtime** branch (unlike `HP_UNSHADED`, which is compile-time: a debug view is
an editor control that flips between frames, and making it a PSO permutation
would mean a pipeline rebuild per dropdown change and nine times the variants).

Within minutes of working it found:

1. **Occlusion was never read.** `EnableAO` was false, so `USE_AO_MAP` was 0,
   `GetOcclusion` was never compiled in, and `Occlusion` sat at the material
   factor of 1.0 — while a real AO map sat loaded, packed and *bound*. Flat white
   channel, zero variation. Now on, variation **6.37**, guarded by a test.
2. **The test light was head-on.** An identity-transform light points down +Z,
   coincident with the view axis and the quad normal, so `N·L` was 1 everywhere —
   the worst possible angle for judging a normal map, and the reason the frame
   looked evenly veiled and "washed out". It read as a sampler bug for an
   afternoon. Now raked ~40°.
3. **Then the rake pointed the wrong way.** `ResolvedLight::direction` is the
   direction light *travels* (`PBR_Shading.fxh` does `L = -lightDir`), so the
   first fix gave `direction.y = +0.644` — lighting from *below*. The owner spotted
   it from the shadow direction. The test now **asserts** `direction.y < 0`.

**And the quad was 2.3x too big for the frame.** Half-size 4 at distance 3 with a
60° vertical FOV shows the middle 43%, magnified — so every by-eye comparison
against the source texture was comparing different regions, and the `variation >
8.0` threshold was calibrated against magnified detail. The quad is now sized from
the camera's own FOV, and that threshold has been replaced.

#### Verified per channel, on hardware

| channel | result |
|---|---|
| BaseColor | the albedo, correct scale |
| MeshNormal vs ShadingNormal | **7e-11** vs **12.17** — the normal map provably reaches the lighting |
| Roughness | varies (8.96) — texture, not factor |
| Metallic | rock **0**, metal **249** — pins metalness to the blue channel |
| Occlusion | varies (6.37) after the fix |
| Texcoord0 | clean corner-to-corner gradient |

`zig build test -Dtest=all` — **302 fast + 89 integration + 22 gpu, both targets,
zero failures.**

#### Not done, and stated plainly

- **The debug output is sRGB-encoded**, because the target is `RGBA8_UNORM_SRGB`.
  A channel value of 0.5 reads as ~188, not 128. Fine for looking at, wrong for
  reading exact values off.
- **The extended features are still off** — clearcoat, sheen, anisotropy,
  iridescence, transmission, volume. The owner has asked for full parity ("all
  what they have and the ability to add to it"), which **amends D24** and needs
  its own ticket. Their getters are now already included and callable, so the
  remaining work is `CreateInfo` flags, the texture slots, the
  `SurfaceShadingInfo` sub-structs, and — the piece that makes "modify for custom
  too" real — **growing `HpSurfaceOutput`** so a game shader can write them.
- **`LoadConstantBufferReflection` is off.** Diligent already reflects constant
  buffer *contents* — name, type, offset, array size, nested members — via
  `IShader::GetConstantBufferDesc()`. That is what **141.2** needs, and it is one
  bool away. The ticket's current plan to annotate and parse source should be
  reconsidered against it.
- **Metal is still unproven in a shaded frame** and its threshold is now 3.0, not
  8.0. Metal without an environment is near-black whatever the metallic channel
  says; T0087 owns that check and carries the note.

### Remaining, in dependency order

*(Corrected 2026-08-06: the earlier version of this table still listed
141.1/141.2/141.5/141.13 as remaining here after they moved to T0142, with
141.13 blocking work that had moved with it.)*

| | | Blocked on |
|---|---|---|
| **141.14** | generated shader-contract docs, gated in CI | ~~141.6 settling~~ — **done 2026-08-06** |
| ~~**141.9**~~ | tessellation | **left this ticket 2026-08-06 → T0146.7**, trigger sharpened — see Descoped |

**Nothing remains.** This ticket closed 2026-08-06.

Moved to T0142 and tracked there: 141.1 → **142.15**, 141.13 → **142.14**,
141.15 → **142.16** — all three closed. Two went one hop further when T0142
descoped its editor half on 2026-08-06: 141.2 → 142.9 → **T0032.8**, and
141.5 → 142.8 → **T0032.7**. Follow the chain rather than the first arrow.

## Closed 2026-08-06 — the ceiling the owner named is lifted, and the evidence is pixels

**The question this ticket opened with was** *"if an editor wants to use screen
displacement for texture, thats a custom shader or not even possible on the
engine it self?"*, and the honest answer on 2026-08-05 was **not possible at
all**. It is now three things, each proven on hardware rather than argued:

| The claim | The evidence |
|---|---|
| The engine owns the surface stage (**D26**) | `SceneRenderer` draws every mesh through `SurfacePipeline`; a red quad under a white light is **(211, 144, 144)** through DiligentFX's shader and **(211, 144, 144)** through ours — the same bytes, not "close" (141.10) |
| Screen displacement works | Zero-scale vs no-height-map frames **bit-identical** (mean abs diff 0.0); `heightScale = 0.08` moves the frame by **15.69** mean abs per channel on a 40° oblique rock quad, unlit, so every differing pixel is a displaced texel (141.7) |
| A game can write its own | A three-line `.slang` module overriding `baseColor` renders exactly (255, 0, 0) unshaded and exactly (0, 0, 0) shaded with no light — the un-overridden lighting default still governing is the partial-override claim, measured (142.15) |
| Nothing fails silently | A missing material, an unloadable one and a module that will not compile all render **one** magenta checkerboard, with the compiler's error logged **once** per module across three frames and the draw path silent (141.4, 141.12) |
| The contract is documented and cannot go stale | `docs/shaders/` is generated from `HpMaterial.slang` and `IHpMaterial`, regenerated by every build and gated by CI (141.14) |

**Final verification, this tree, 2026-08-06:**

```
zig build all                  EXIT: 0, no ^FAILED:|error:
zig build test -Dtest=all      fast 310/214,690 + integration 89/515, both targets
zig build test -Dtest=gpu      26 cases / 562 assertions, both targets
zig build docs                 green; docs/api and docs/shaders both current
+- test (linux-x86_64, gpu) natively success
+- test (windows-x86_64, gpu) as a real Windows process via WSL interop success
```

The Windows target reaches an **NVIDIA GeForce RTX 4070 Laptop GPU**; the Linux
target on this host reaches `llvmpipe` only, because there is no NVIDIA Vulkan
ICD here. So every pixel claim above is proven on both a real driver and a
software one — stated because it decides what a Linux-target number is evidence
of.

**What is honestly not finished** is in the two `[~]` Done-whens and the
Descoped table below, and none of it is hidden: a custom module has no
parameters of its own yet (T0032.8, and the mechanism is undecided), there is
no hot reload (T0032.7), the *sampled* engine intermediates do not exist
(T0147), the variant bound is unwritten (T0151), and tessellation is deferred
with a sharpened trigger (T0146.7).

## Descoped 2026-08-06 — what left this ticket, and where it went

**These are no longer this ticket's checklist**, which is the point: leaving a
box unticked forever is what "moved" is supposed to prevent. Each left because
it needs something that does not exist, and each is written onto the receiving
ticket in full rather than as a link.

| Was | Went to | Because |
|---|---|---|
| 141.9 tessellation / displacement | **T0146 (146.7)** | Hull and domain stages are **PSO construction**, and T0146 is where the pre-rasteriser stages become the engine's. Deferred there with a **sharpened** trigger — see below |
| Custom shader parameters in the inspector (Done-when; was 141.2 → 142.9) | **T0032 (32.8)** | There is no inspector. The undecided mechanism moved as text, not a pointer |
| Shader hot reload in the editor (Done-when; was 141.5 → 142.8) | **T0032 (32.7)** | There is no editor. The engine half already works — a changed module is picked up whenever a pipeline builds |
| The *sampled* engine intermediates (half of a Done-when) | **T0147** | Scene depth, scene colour, T0093's visibility and game-fed textures need systems that do not exist. The interpolated half shipped here |
| "Variant growth bounded by a written decision" (Done-when) | **T0151** | The decision's mechanisms — precompiled modules, link-time specialisation, the dynamic-dispatch escape hatch — are all T0151's, probed on the pinned slangc. 141.3 and 142.7 bound the *cost* of a variant; nothing here bounds the *count* |

**Why 141.9 went to T0146 and not to T0155 (terrain), argued rather than
defaulted.** Terrain is the obvious consumer — 155.2 is the LOD-scheme
decision, and GPU tessellation is a candidate answer. But what tessellation
needs is hull and domain **stages in the PSO**, and `PBR_Renderer::CreatePSO`
sets `pVS` and `pPS` and nothing else. T0146 is the ticket that stops the
vertex shader being Diligent's, so it is the one whose code grows a stage.
Homing a general capability on terrain would have shaped it like terrain, and
the second consumer would have found it in the wrong place. T0155's 155.2
carries the cross-reference instead, which is where the want appears first.

**And the trigger was wrong by the time it moved.** This ticket deferred
tessellation *"when a silhouette must change"* — and **T0146 delivers exactly
that**, at the mesh's existing vertex density; its own Done-when says
"silhouettes included, the thing 141.7's parallax explicitly cannot do". So the
trigger as written would have read as "do it now" to whoever picked T0146 up.
It is restated there as **when a silhouette must change at a density the mesh
does not carry**, which is the real distinction: vertex displacement moves
geometry, tessellation makes it. T0156.6's silhouette-POM evaluation moves the
trigger in one direction or the other, and T0156 says so.

## Notes / findings

### 141.14 landed 2026-08-06 — the shader contract is generated, and drawing its boundary found two things

**What landed.** `tools/gen_shader_docs.py` → `docs/shaders/`
(`index.md`, `HpMaterial.md`, `IHpMaterial.md`): 4 declarations, 23 members,
all documented. It runs inside `zig build docs`, on every route to a build,
and CI's *API reference is up to date* job now regenerates and diffs `docs/`
rather than `docs/api`. Measured: 4 declaration(s), 23 member(s) across 2
page(s); `zig build docs` converges to `cached`; `rm -rf docs/shaders && zig
build docs` restores it (the generated markdown is declared as an input, the
lesson T0123 paid for on the C++ side).

**Finding 1 — the include/src split this subtask told itself to mirror does
not exist in `engine/shaders/`, and pretending it does would have been the
wrong boundary.** The subtask said "`HpSurface.psh` is no more public than
`engine/src/*.cpp`", which is true of the *file* and false of one declaration
inside it: **`interface IHpMaterial` is the thing a game actually implements**
(D28) and it lives in the private file, because its default implementations
*are* the standard material and they call DiligentFX's getters and the
engine's own resources. It cannot move into `HpMaterial.slang` without making
the contract file depend on resources declared in the implementation — which
is a worse boundary, not a tidier one.

So publicness is **declared in the source** rather than inferred from a path:

| marker | meaning |
|---|---|
| `// hp-shader-doc: public` | the whole file is contract |
| `// hp-shader-doc: private` | the whole file is implementation |
| `// hp-shader-doc: export` | (in a private file) the next declaration is contract, and only that one |

**A `.slang` file carrying neither file marker is a hard error**, the same
shape as `hp_embed_shaders.cmake` refusing a stray `.psh` — verified in both
directions: the clean tree generates, a `Stray.slang` with no marker fails the
`docs` step naming the file and D27, and removing it goes green again. A new
file is private until somebody decides otherwise, which is the safe direction.

**Finding 2 — the first generated output was confidently wrong, and it is the
exact failure `gen_api_docs.py`'s stale-param check exists to prevent.** Every
one of `IHpMaterial`'s seven methods rendered as *"No default — every material
must implement this"*, directly above prose beginning *"Default: ..."*. The
cause is that this project writes the opening brace on its own line, and the
detector tested `"{" in <the signature text>`. A reader would have concluded
that a three-line material is impossible. Fixed by looking for the terminator
*after* the signature — `{` means a default, `;` means a requirement — and the
distinction is load-bearing, since "adding a method with a default is free" is
the whole of D28's promise.

**A stale paragraph was found and corrected rather than published.** The block
above `IHpMaterial` still said the interface was *"not yet a game-deliverable
authoring surface ... until that lands, the only conformance in the build is
the engine's own `HpStandardMaterial`"*. 142.15 landed; a game's module has
been a conformance since. Because the generator reproduces the source's own
prose, that sentence would have become the shader reference's answer to "can I
ship a material?" — which is precisely the re-introduction of stale guidance
the amended D27 warns about. `HpSurface.slang` now says what is true, with the
correction noted in place.

**One hole closed in the *existing* gate on the way**: the job checked
`git diff --quiet -- docs/api`, and `git diff` does not see an untracked file.
A generator that can add a *page* — marking a second shader file public does
exactly that — could therefore have left a new page uncommitted and green. It
is `git status --porcelain -- docs` now.

**Not done, and stated plainly:** the parser is hand-rolled (Slang has no
libclang) and covers `struct`, `interface` and `#define` at file scope. It
fails loudly on anything it cannot parse inside a public region rather than
omitting it silently, which is the mitigation, not a substitute for the
limitation. There is deliberately **no baseline**: the contract is small and
fully documented today, so a missing comment is an error from the first commit
— if that ever becomes untenable, add the ratchet deliberately.

### 141.8 landed 2026-08-06 — triplanar, and the second data point agrees with the first

**Proven on the case nothing else can fake**: a quad with positions and
normals only. The UV-mapped control renders dead flat (variation 0.0 — the
getters have no coordinates and return the factor); the same mesh, textures
and pool with `triplanar = true` renders tiling rock at variation **11.38**,
four tiles across the frame at `triplanarScale = 0.5` on an 8 m quad. Unlit,
so the detail is sampled texture and nothing else. 19 gpu cases green on both
targets.

**The interface answer this data point adds**: triplanar did *not* need a new
interface method — it lives inside the standard material's channel defaults
under `HP_TRIPLANAR`, because it changes *where a texel comes from* per
channel rather than the coordinates every channel shares. One technique
(parallax) wanted the shared hook `surfaceCoordinates`; the other wanted the
per-channel defaults that already existed. That is the shape D28 promised —
the interface is neither too small (after 141.7's addition) nor growing a
method per technique.

**The per-function D26 argument, spent**: this is the one place the surface
stage samples material textures itself. DiligentFX's `SampleTexture` reads
`SelectUV(VSOut, …)` behind `#if USE_TEXCOORD0 || USE_TEXCOORD1`, so a
technique whose whole point is needing no texture coordinates cannot reach
through their getters. The triplanar samples mirror the getters' composition
line for line — `TO_LINEAR` on colour/emissive, vertex-colour multiply,
factor last — so a triplanar material differs from a UV-mapped one only in
where texels come from. `triplanarScale` rides in `CustomData.y` beside the
parallax depth.

**Deliberately not done, written down**: the normal map is ignored in
triplanar mode (per-plane tangent reorientation — a whiteout blend with
swizzles — arrives when something needs it; a silently wrong basis would be
worse than a flat one, and `Material::triplanar`'s doc says so). Parallax and
triplanar are mutually exclusive, triplanar wins — there are no UVs to
displace. World-anchoring (moving the mesh slides it through the pattern) is
triplanar's nature, documented on the field rather than fought.

### 141.7 landed 2026-08-06 — parallax occlusion, and the interface really was short

**The sequence's bet paid off**: writing the first real technique against
`IHpMaterial` found the contract missing its most important hook. The six
methods could change what a channel *returns* but nothing could displace the
coordinates every getter samples **before** sampling — which is the exact
capability D26 was decided to create. The interface now has
`surfaceCoordinates(VSOutput, HpSurfaceInput) -> VSOutput`, defaulted to
parallax-when-height-mapped, run first in `evaluateSurface`, with the
contract's `UV0/UV1` refreshed from its result so a game's `HpSurface` sees
the displaced coordinates. Adding it pre-141.1 was free; after games author
against the interface it would have been a versioning event. (Triplanar,
141.8, overrides this same method — one hook, two techniques.)

**The mechanics.** The height map is the **engine's own signature slot** —
`SurfacePipeline::CreateCustomSignature` appends `g_HeightMap` plus an
immutable wrap sampler to the one shared signature; squatting in a disabled
DiligentFX slot (clearcoat, sheen…) was rejected because T0143 will enable
all of them. The override only dispatches because the constructor now passes
`InitSignature = false` and calls `CreateSignature()` itself — the base
constructor would have called it through the base vtable, silently. The march
is steep-parallax with occlusion interpolation, 8–32 layers by view angle,
`SampleGrad` with pre-march gradients (required, not a refinement: implicit
derivatives are undefined in the divergent loop), tangent frame from screen
derivatives so **no vertex tangents are needed**, and the normal is oriented
toward the viewer explicitly because of the winding finding — the flipped
contract normal points away from the camera on front-facing surfaces here,
and `viewTS.z` would go negative and disable the march everywhere.
`heightScale` rides in `PBRMaterialShaderAttribs.CustomData.x`, the
per-material channel DiligentFX reserves — no new buffer. Permutation bit
`kPsoFlagHeightMap` (user-defined, macro `HP_USE_HEIGHT_MAP`), stripped for
meshes without UV0.

**Measured (gpu suite, both targets green, 18 cases):** no-height vs
zero-scale frames **bit-identical** (mean abs diff 0.0 — the march moves
nothing when told to move nothing, across two different pipelines); no-height
vs `heightScale = 0.08` differs by **15.69** mean abs per channel on a 40°
oblique rock quad, and `test-frames/parallax_on.ppm` visibly bulges where
`rock_height.png` says rock. The comparison is **unlit** so every differing
pixel is a displaced texel, not a lighting response.

**Known limitations, written rather than discovered:** the height lookup reads
raw UV0, so a material whose uv0 channel carries a non-identity transform
misaligns relief against its colour maps (revisit if authored); `loadTexture`
still decodes every image as sRGB (T0097 owns per-asset colour space), so the
height curve is warped-but-monotonic — displacement direction and test both
unaffected; silhouettes stay flat, which is POM's nature and 141.9's trigger.

### 141.3 landed 2026-08-06 — the SPIR-V cache, and why the RenderStateCache half was declined

**What landed.** `compileSlangToSpirv` now fronts a **persistent
`IBytecodeCache`** (Diligent's, from GraphicsTools — not a cache of our own):
key = content hash of the shader and every transitive include resolved through
the same compound factory the compiler uses, plus the macros — computed by
their `XXH128State`, so editing any header, including a per-pipeline generated
struct, changes the key and there is no staleness rule to remember. Two inputs
their hash cannot see ride in as synthetic macros: the prepended
`HLSLDefinitions.fxh` (content-hashed with a hand-rolled FNV-1a, stable across
libc++ versions) and a hand-bumped schema constant standing for
compile-request state (target, matrix layout). The file lives beside the
executable, named with the pinned slang version so a pin bump is a cold cache
rather than stale hits, written through on every new entry via temp+rename,
and `HP_SPIRV_CACHE=0` disables the whole thing for measurement or suspicion.
Failures are never persisted — a fixed shader must not stay broken.

**Measured, Linux gpu suite, RTX 4070 Laptop (seconds per doctest case):**

| case | cache disabled | first run (filling) | warm process |
|---|---|---|---|
| each surface channel | 11.88 | 2.58 | 2.25 |
| lit surface | 3.24 | 1.40 | 0.67 |
| world+HUD composite | 2.97 | 0.61 | 0.53 |
| assigned material | 1.05 | 0.66 | 0.15 |
| missing material | 0.56 | 0.52 | 0.13 |

Even the *filling* run wins big, because the suite builds fresh
`SurfacePipeline`s per `SceneView` and identical permutations start hitting
within the process. Cache size after the whole suite: 179 KB; the editor's own
run writes 26 KB beside `hp_editor`. Both targets green (fast 302, integration
89, gpu 17/505 x2), docs green; wine writes its cache beside the `.exe`.

**`IRenderStateCache` was evaluated and declined, with the trigger written
down.** The constructor slot exists, but with `EnableIBL` off `PBR_Renderer`'s
constructor compiles zero shaders through its cached device, and our `build()`
creates shaders and PSOs against the device directly — so constructing one
today would cache nothing and add the Archiver subsystem for it. The measured
cost was slang's frontend, which the bytecode cache erases. Revisit when
T0087 turns IBL on (the BRDF precompute runs through that slot) or if PSO
creation from cached SPIR-V ever appears in a profile. T0151 composes with
this from the other side — it bounds how many variants exist; this amortises
each one — and records that the cache stays right whatever it decides.

**Not verified:** the cache under a *read-only* executable directory degrades
to warnings-and-recompile by construction, but no test installs the tree
read-only to prove it; and concurrent processes sharing one cache file
last-writer-wins on the whole file — acceptable for a dev cache, noted rather
than solved.

### 141.12 landed 2026-08-06 — material assets reach the pixels, and the first single-sided draw found an engine-wide winding inversion

**What landed.** `SceneRenderer` now resolves every surface's slot through
`resolveMaterialSlot` and draws the three-state table: `Imported` unchanged,
`Assigned` renders the material asset (converted to `GLTF::Material` via
`MaterialBuilder`, textures bound by GUID from the pool, SRB cached per asset
and rebuilt on pointer change), `Missing` renders `missingMaterial()` with the
checkerboard bound as its base colour. Drawing the fallback required exactly
the machinery that renders any material asset, so the `Assigned` row came with
it rather than after it. The unlit half is the engine's own PSO permutation:
`SurfacePipeline::kPsoFlagUnshaded`, a **user-defined** flag that `build()`
turns into the contract's `HP_UNSHADED` macro — DiligentFX's own
`PSO_FLAG_UNSHADED` was evaluated and is the wrong tool: its `PSOKey`
constructor strips every texture flag and its footer outputs the frame-wide
`UnshadedColor`, so the checkerboard could never be sampled through it.

**Measured, on hardware (RTX 4070 Laptop, Vulkan, both targets):** a missing
material in a scene with **no light at all** renders (127, 0, 127) centre mean
with 1442 loud-magenta and 1342 near-black centre pixels — the checks, undimmed,
which is the unlit proof; the warning fires **once** across three frames. An
assigned unlit green material renders exactly **(0, 255, 0)**; the imported
control in the same scene is exactly (0, 0, 0). Suites: 302 fast + 89
integration + 17 gpu (505 assertions), both targets, zero failures; docs green.

**The winding finding, and it is engine-wide.** The first single-sided draw
this engine ever made came back invisible: **a glTF front face reaches the
rasteriser as a hardware back face** (glTF's CCW winding x the left-handed
view x Vulkan's viewport flip), so `CULL_MODE_BACK` culls exactly the faces a
single-sided material means to keep. Probed three ways: flipping the cull enum
made the quad appear, an `SV_IsFrontFace` colour probe returned *false* on
fragments facing the camera, and setting `FrontCounterClockwise = true`
inverted the flip for every existing surface — the lit suite went black,
because its lights sit behind the quads and every measured baseline was
calibrated against the two-sided flip. Resolution for now: single-sided
materials cull `FRONT` (one line in `SceneRenderer`, argued in place), and the
convention question — whether hardware facing should be realigned with
geometric facing, which would re-baseline every pixel test and change
two-sided lighting — is **left open here deliberately. T0086 must look at this
before shadow bias is tuned**, and T0086's Refs now say so.

**Corrected 2026-08-06 by T0152/D33, and the correction inverts the
diagnosis.** The trace measured zero winding reversals in the engine's chain
— the LH view is a rigid det-+1 inverse, reverse-Z touches only the
projection's Z column, and Diligent folds its internal Vulkan viewport flip
into `FrontCounterClockwise`'s D3D semantics — so the default `false` is the
glTF-conformant setting. What was inverted is the **test quad**: indices
`{0,1,2, 0,2,3}` over BL,BR,TR,TL vertices wind its front toward **+Z**,
away from the camera, while its authored normals say −Z. `SV_IsFrontFace ==
false` was the correct classification of an asset whose winding contradicts
its normals. The lit suite went black under `FrontCounterClockwise = true`
because its scenes are lit from the far side and depend on the two-sided
flip inverting the authored normal — the flip only fires because the winding
is backwards. `CULL_MODE_FRONT` therefore keeps exactly the faces a
conformant renderer culls; it reverts to `BACK` in T0152.4, in the same
commit that re-winds the assets.

**Also fixed on the way:** `makePlaceholderTexture` and `loadTexture` created
plain 2D textures; every material slot in the shader is `Texture2DArray`
(matching the glTF loader and `PBR_Renderer`'s defaults), and Vulkan refuses
the mismatch — both now create one-slice arrays. Alpha mode now reaches the
pipeline from the drawn material (it was hardwired opaque), the shader gained
`RenderPBR.psh`'s compile-time cutout discard and runtime blend premultiply,
and `PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM` is set per material when a bound UV
channel's transform is not the identity — without it the material's UV
transforms were written and silently ignored.

**Not verified:** `AlphaMode::Mask` and `Blend` land in the pipeline state and
the shader, but no pixel test pins them yet — T0045's sorted transparent queue
is where blend becomes testable honestly. Texture hot reload behind an
unchanged material object is not detected by the binding cache (recorded for
T0058). UV-transform rotation composition mirrors the glTF loader's
(`Scale * Rotation(-r)`) but has no pixel test.

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

### 2026-08-06 — two Done-when items now have owners outside this ticket

- **"Custom shaders receive engine intermediates"** is delivered in two
  halves: `ScreenPos`/`WorldPos` landed with 141.6; the *sampled* half —
  scene depth, scene colour, T0093's visibility, game-fed textures — is
  **T0147**, created because no subtask here delivered it. When T0147 closes,
  this box ticks against its evidence.
- **"Variant growth is bounded by a decision that is written down"** — the
  decision's mechanisms (precompiled modules, link-time specialisation, the
  dynamic-dispatch escape hatch, all probed on the pinned slangc) live in
  **T0151**. This box ticks when T0151's 151.5 writes the bound.

The ladder above the surface stage — per-light and whole-loop overrides — is
**T0145** (D30 amends D24 for it); the vertex stage is **T0146**. Both carry
the sequencing consequences recorded here (T0086, T0041).
