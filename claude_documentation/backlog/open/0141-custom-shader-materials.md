# T0141 — Custom shader materials

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 455 |
| **Created** | 2026-08-05 |
| **Blocked by** | T0060 (there is no material asset to attach a shader to yet) |
| **Blocks** | height mapping, parallax occlusion and triplanar — 141.7/141.8, which T0060 cannot deliver because Diligent's material shader has no hook before texture sampling |
| **Refs** | [../inprogress/0060-material-system.md](../inprogress/0060-material-system.md) (split from it), [../completed/0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md), T0093, T0053, T0094, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D24 |

## Why

Split out of T0060 on 2026-08-05, deliberately and with the reason recorded.

T0060 as written was two tickets wearing one number: **a material asset**, which
is a data-model gap and blocks T0045 and T0086, and **a shader system** — custom
shaders, parameter reflection, compile caching, hot reload, an error material,
and a documented interface to engine intermediates. The second is larger than the
first and blocks nothing.

Following Godot's model, which T0060 already cites: a standard PBR material
covers the common case, **plus the ability to attach a custom shader** for
anything else. T0060 delivers the first half. This is the second.

Keeping them together would have meant T0045 and T0086 waiting behind a shader
compiler cache, which is the wrong dependency to accept.

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
- [ ] 141.6 Document the engine intermediates a custom shader may read
- [ ] 141.7 **Height mapping and parallax occlusion** — needs the surface stage;
      `PBR_Renderer` has no path for it and `GetPSMainSource` cannot reach before
      texture sampling. Raised from T0060 on 2026-08-05
- [ ] 141.8 **Triplanar projection** — same reason: a surface-stage technique,
      not a material parameter. Raised from T0060 on 2026-08-05
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
