# T0142 — Slang as HollowPoint's shader language

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Large |
| **Phase** | 4 — Render layer |
| **Order** | 415 |
| **Created** | 2026-08-06 |
| **Refs** | **D28** (the decision), D26, D27, [T0141](../inprogress/0141-custom-shader-materials.md) — this supersedes how 141.1, 141.2, 141.5, 141.13 and 141.15 are built; [T0143](0143-extended-material-features.md) — the extended lineup is authored in Slang once this lands; T0087, T0096, T0086 — each adds shading their own shader work must reach |

## Why

**D27's cost, paid off.** D27 chose Godot's model — the game writes
`HpSurface(in, inout)`, the engine owns the `main` — and named the price: *"a
developer who wants something the contract does not expose has to wait for the
engine to expose it."* Every field is all-or-nothing, the contract must
anticipate every need, and widening it is a ticket each time.

**HLSL cannot express the alternative.** No inheritance, no override, no partial
types. Shader Model 5's dynamic linkage was the only mechanism that ever came
close and it is D3D11-only, dead in DXC/SM6, absent from SPIR-V. Reuse in HLSL is
`#include`, macros, and shadowing a filename.

Slang gives the engine an `interface` whose **default implementations are the
standard material**, which a game material implements and overrides piecewise —
with `override` mandatory, so nothing is replaced by accident, and with generics
specialising statically so it costs nothing at runtime.

**This is measured, not hoped.** See D28 for the full table. The headline: a
Slang shader including DiligentFX's unmodified `PBR_Shading.fxh`, calling
`GetSurfaceReflectanceMR` through an interface method, compiled by `slangc` to
SPIR-V and handed to Diligent as bytecode, rendered **(245, 122, 49)** on an
RTX 2080 — their arithmetic, to the byte.

## Done when

- [ ] A game can write a `.slang` material, override part of the engine's
      standard material, and see it render — without the engine changing.
- [ ] The engine's own shaders are `.slang`, and no hand-written HLSL remains in
      `engine/shaders/`.
- [ ] `slangc` is pinned in `.harness/`, offline after bootstrap, on both hosts.
- [ ] A shipped game links no Slang and reads cooked output only.
- [ ] The editor builds a material inspector from Slang reflection, with no
      device and no successful compile required.

## Subtasks

- [ ] 142.1 **Pin `slangc` in `.harness/`** the way zig, cmake and ninja are —
      version plus SHA256, fetched once by `bootstrap.sh`, zero network during
      `zig build`. Prebuilt archives exist per platform (23–78 MB); the
      `glibc-2.27` Linux variant is the small one. **Build-time only** — this
      must never become a runtime dependency.
- [ ] 142.2 **Define `IHpMaterial`** — the interface whose default
      implementations *are* the standard material. This is **D27's contract
      restated in a language that can express it**, and the same promise applies:
      adding is free, removing breaks every shipped game. `HpMaterial.fxh`'s
      field table and its "nothing is exposed before the system behind it exists"
      rule carry over unchanged.
- [ ] 142.3 **Port `HpSurface.psh` to Slang** as the reference implementation of
      142.2, still calling DiligentFX's getters and lighting. **The pixel output
      must not change** — the existing byte-identical comparison against
      `RenderPBR.psh` is the acceptance test, and it already exists.
- [ ] 142.4 **Feed the generated interface structs to Slang.** `PBR_Renderer`
      emits `VSOutputStruct.generated` and friends into *Diligent's* source
      factory; Slang compiles first, so they must reach `ISlangFileSystem`
      instead. Same strings, different consumer.
- [ ] 142.5 **Pass the permutation macros through.** `DefineMacros` already
      produces exactly the `-D` set Slang needs. Verified working for
      `PBR_Textures.fxh` in the D28 probe.
- [ ] 142.6 **Choose the interchange format and measure it.** Slang → HLSL → DXC
      keeps Diligent's whole pipeline and lets Slang stay ignorant of nothing;
      Slang → SPIR-V skips a step. **Measure both** — cold compile time and first
      frame — rather than picking on taste. The bytecode path is proven to work
      (D28); the HLSL path is not yet tried end to end.
- [ ] 142.7 **Cook shaders as compiled assets.** Slang → cooked output at cook
      time, keyed on content hash like everything else. **But `Cook.hpp`'s
      invariant does not hold**: it promises anything cooked can be re-cooked from
      source, and an exported game has neither `slangc` nor `.slang`. A missing
      cooked shader is **fatal, not recoverable** — the cook layer must say so
      rather than inherit the wrong contract.
- [ ] 142.8 **Hot reload in the editor**, via Slang's runtime API and the same
      content hash. Supersedes 141.5.
- [ ] 142.9 **Material inspector from Slang reflection** — parameters read from
      *source*, so the panel works before the shader compiles and while the
      developer is typing. Supersedes 141.2's plan to annotate and parse.
      **Note Diligent already reflects constant-buffer contents** via
      `IShader::GetConstantBufferDesc()` (`LoadConstantBufferReflection`, one
      bool, currently false) — that is the runtime-side answer and may be enough
      on its own. Decide deliberately rather than adopting Slang's by default.
- [ ] 142.10 **One inspector over two reflection systems.** Component fields come
      from `entt::meta`, material parameters from Slang. **These do not merge** —
      Slang cannot see a `MeshRenderer` and never will. What unifies is the
      *presentation*: the editor should consume one description of "a named,
      typed, editable value" whichever side produced it.
- [ ] 142.11 **Windows and D3D12.** The D28 probe was Linux and Vulkan only.
      DXIL output, and the `SLANG_ParameterGroup_*` naming behaviour on the D3D12
      backend, are both unverified.
- [ ] 142.12 **Delete or promote the probe.** `tests/gpu/slang_spirv_probe_test.cpp`
      and `hp::probePrecompiledSpirvPipeline` are experimental scaffolding from
      D28, gated behind `HP_SLANG_PROBE_DIR`. They become the real integration
      test or they go — they must not sit half-alive.
- [ ] 142.13 **Retire the HLSL path.** When the engine's shaders are Slang, the
      hand-written `.psh`/`.fxh` in `engine/shaders/` go, along with
      `cmake/hp_embed_shaders.cmake` if cooking replaces embedding. **Two paths
      that both work is the outcome to avoid** — it is how the `CreateInfo`
      duplication that hid `TextureAttribIndices` happened.

## What this changes in T0141

| T0141 subtask | Effect |
|---|---|
| 141.1 custom shader asset | **Superseded** — the asset is a `.slang` module |
| 141.2 parameter reflection | **Superseded** by 142.9, and reconsidered against Diligent's existing reflection |
| 141.5 hot reload | **Superseded** by 142.8 |
| 141.13 VFS shader source | **Reshaped** — resolution order still engine → game → DiligentFX, but through `ISlangFileSystem` |
| 141.15 `HP_UNSHADED` | **Reshaped** — an interface method with a default, not a macro |
| 141.6 `HpMaterial.fxh` | **Becomes** 142.2. The field list and its rules survive; the language changes |
| 141.7 / 141.8 parallax, triplanar | **Unaffected** — still a displaced `VSOutput` copy handed to their getters |
| 141.10 / 141.11 | **Done**, and 141.11 is the acceptance test for 142.3 |

## Notes / findings

### Struct inheritance is deprecated — do not build on it

`warning[E30816]: support for inheritance is unstable and will be removed in
future language versions, consider using composition instead`

The first design attempt used `struct MyGameMaterial : HpStandardMaterial` with
`override`, and it failed three ways: the warning above, `override` not matching
any base declaration, and the derived struct not inheriting interface
conformance. **The mechanism is `interface` + default implementations**, and it
is not a workaround — it is the idiom, and it is the one that survives.

### What the D28 probe did not prove

- No *real* engine shader permutation compiled through Slang — only DiligentFX's
  headers plus a synthetic probe.
- No performance measurement of any kind.
- Linux and Vulkan only.
- `RenderPBR.psh` itself never compiled through Slang.
- Resource *binding* through a real `SurfacePipeline` signature untested; the
  probe used its own trivial pipeline.

### The research that got it wrong, and why

A web research pass concluded Slang could not help, because `extension`
declarations apply only to struct types and DiligentFX is free functions. **The
fact was right and the conclusion was wrong** — `extension` is not the mechanism;
interface defaults are. That rejection was written into the decision log before
anyone ran the compiler, and it took ten minutes to disprove once someone did.
Recorded because "anything measurable here, measure it" exists for exactly this.
