# T0060 — Material assets and custom shader materials

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 450 |
| **Created** | 2026-08-03 |
| **Refs** | [0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md) — **must reconcile this ticket's material model with `PBR_Renderer`'s material attribs, or diverge deliberately and say so.** T0028 adopted DiligentFX's PBR renderer; read T0134.1 before designing materials |

## Why

Diligent's `PBR_Renderer` shades glTF materials, but there is no material
**asset** — nothing GUID-addressable, editable in the inspector, or reusable
across meshes. Without one, the only way to change how something looks is to
re-export from the DCC tool.

Following Godot's model: a standard PBR material for the common case, **plus the
ability to attach a custom shader** for anything else.

## Done when

- [ ] Material is an asset with a GUID, serialized (T0020), inspector-editable
- [ ] Standard materials drive Diligent's `PBR_Renderer`
- [ ] **Custom shader materials** — attach a shader, expose its parameters
- [ ] Custom shader parameters appear in the inspector automatically
- [ ] Materials are assignable per mesh, overriding what the model imported
- [ ] Shader compilation is cached, not repeated every launch
- [ ] A shader that fails to compile falls back visibly, never crashes
- [ ] Shader hot reload in the editor
- [ ] **Custom shaders receive engine intermediates** — visibility (T0093),
      screen position, depth — not just a finished colour

## Subtasks

- [ ] 60.1 Material asset: shader reference plus a parameter block
- [ ] 60.2 Standard PBR material mapping onto `PBR_Renderer`
- [ ] 60.3 Custom shader material with a declared parameter interface
- [ ] 60.4 Reflect shader parameters for the inspector — see notes
- [ ] 60.5 PSO management via Diligent's `RenderStateCache` and `BytecodeCache`
- [ ] 60.6 Material assignment on the mesh component, overriding import defaults
- [ ] 60.7 Error material — unmissable magenta — on compile failure
- [ ] 60.8 Shader hot reload
- [ ] 60.9 Sort by material/PSO in the render queue (T0045)

## Notes / findings

### Inherited from T0134 / D24 (2026-08-05) — materials map onto `PBRMaterialShaderAttribs`

**Decided, not left open**: `PBR_Renderer`'s material attribs are the engine's
material vocabulary, and a material asset maps onto them rather than defining a
parallel structure. Read [../completed/0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md)
before designing the asset — the survey is done, and redoing it is the waste this
ticket's Refs exist to prevent.

What that gives you for free: `BaseColorFactor`, `EmissiveFactor`, `NormalScale`,
`SpecularFactor`, `Workflow`, `AlphaMode`, `AlphaMaskCutoff`, `MetallicFactor`,
plus 17 texture slots addressed by `TEXTURE_ATTRIB_ID_*`.

Three obligations this ticket picks up:

- **`GetMaterialPSOFlags` is currently inlined and constant-folded** in
  `SceneRenderer.cpp`, because with every optional feature off it collapses to
  `USE_COLOR_MAP | USE_NORMAL_MAP | USE_PHYS_DESC_MAP`. It carries a
  `static_assert` guard. **The moment this ticket enables `EnableEmissive` or any
  extended-material setting, that must go back to consulting the material** or
  materials will silently render with the wrong feature set.
- **Extended materials are off by design** — clearcoat, sheen, anisotropy,
  iridescence, transmission, volume. Each widens the PSO permutation space and
  the material attribs buffer *whether or not a material uses it*, so turning one
  on is this ticket's decision to argue, per D24.
- **Texture colour-space conversion is unresolved and inherited from T0028.**
  `GetPBRTextureSRV` is not public, so textures bind through the model's own
  views with no conversion. Untextured materials are unaffected, which is exactly
  why no test has caught it. Sits next to T0097's sRGB work.

**The regression test T0134 could not write belongs here.** T0134 fixed an
unwritten `PBRFrameAttribs::Renderer` whose observable symptom — a garbage
`MipBias` fed into every texture sample — needs a *textured* material to see. A
textured mesh rendered with its pixels asserted is the guard, and there is no
texture path in a test yet.

**Custom shaders must be able to reach engine intermediates, not just material
parameters.** T0093 (vision-based visibility) needs a per-pixel visibility factor
inside the material shader to dim, hide or dither. If shading is a sealed pipeline
that consumes lights and emits pixels, that capability has to be bolted on as a
post-process hack later. Design the custom-shader interface to expose documented
inputs — visibility, screen position, depth, world position — from the start.


**`RenderStateCache.hpp` and `BytecodeCache.h` already exist in
`Graphics/GraphicsTools`** and solve shader compile hitching and startup cost. Use
them rather than building a cache — this is a significant piece of work Diligent
has already done.

**Shader parameter reflection is separate from C++ reflection (T0053).** Getting
a custom shader's uniforms into the inspector means reflecting the *shader*.
Diligent's shader resource querying can provide this — check what
`IShader::GetResourceDesc` and the shader resource variables expose before
writing a parser.

**Variants are the thing that grows without limit.** Every optional feature
doubles the permutation count. Decide early whether variants are enumerated
explicitly or generated on demand and cached, and prefer the smallest scheme that
works — this is where material systems become unmaintainable.

The error material matters more than it sounds: a shader failure that renders
black is indistinguishable from an unlit object, and costs hours.

### Architecture review (2026-08-03) — two gaps in this ticket

**1. The custom-shader *language* is an undecided decision hiding in 60.3.**
Custom materials must run on both Vulkan and OpenGL (D2 — GL is the only
fallback on Windows). Diligent's portable path is **HLSL** (compiled via
glslang for Vulkan, converted for GL via its HLSL2GLSL converter, with
documented limitations); GLSL written directly does not portably reach both
backends through the same pipeline. Whatever is chosen becomes the language
every custom material is written in, forever — decide it explicitly at the
start of this ticket and record it, including which HLSL feature subset is
safe on the GL path.

**2. Skinning is a missing variant axis.** The variant discussion covers
optional features but not the one variant the engine is guaranteed to need:
skinned vs static vertex input (T0041/T0049). The standard PBR path gets this
from `PBR_Renderer` (verified: joints buffer support exists); custom-shader
materials need the skinned variant defined here, or skinned characters will be
limited to standard materials by accident rather than by decision.

Also note T0096 (HDR/tonemapping) now owns where material output lands —
custom shaders write linear HDR and must not tonemap themselves.


### Architecture amendment (2026-08-03) — particles need a material path this ticket does not describe

This ticket never mentions particles, blending, additive, or unlit — it is
implicitly about opaque, lit surfaces. VFX need a material path with different
requirements, and deciding now whether they share the material *system* or get
their own is cheaper than retrofitting either:

- **Blend mode is material state** — additive, alpha, and probably
  premultiplied alpha (T0106.4). Opaque materials have no such concept.
- **Usually unlit.** Fire and magic are emissive and must not be shaded by scene
  lights; smoke arguably should be. T0106.7 owns the fork, but "can a material
  opt out of lighting entirely" is a question about *this* system's shape.
- **Soft particles** need the material to sample scene depth, which is a
  resource an opaque material never binds (T0046).
- **Per-particle input.** A particle material is fed colour, opacity and
  flipbook frame *per instance* from the simulation buffer, not from uniform
  material parameters. That is a different data path from a mesh material.

The likely answer is that VFX materials are a distinct material *domain* sharing
the asset and shader-authoring machinery rather than a separate system — the
same way engines distinguish surface, decal and post-process domains. Decals
(T0108) are a third such domain and land in the same conversation, so it is
worth having once rather than three times.
