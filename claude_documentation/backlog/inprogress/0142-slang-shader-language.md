# T0142 — Slang as HollowPoint's shader language

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Large |
| **Phase** | 4 — Render layer |
| **Order** | 415 |
| **Created** | 2026-08-06 |
| **Refs** | **D28** (the decision), D26, D27, **D29**/[T0144](../completed/0144-remove-opengl-backend.md) — removed the OpenGL backend and with it the single-source constraint, unblocking 142.2 and closing 142.13's GL half; [T0141](../inprogress/0141-custom-shader-materials.md) — this supersedes how 141.1, 141.2, 141.5, 141.13 and 141.15 are built; [T0143](../open/0143-extended-material-features.md) — the extended lineup is authored in Slang once this lands; T0087, T0096, T0086 — each adds shading their own shader work must reach |

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

- [x] 142.1 **Pin `slangc` in `.harness/`** the way zig, cmake and ninja are —
      version plus SHA256, fetched once by `bootstrap.sh`, zero network during
      `zig build`. Prebuilt archives exist per platform (23–78 MB); the
      `glibc-2.27` Linux variant is the small one. **Build-time only** — this
      must never become a runtime dependency.
      *Done 2026-08-06: 2026.14.1 pinned, both packages (linux glibc-2.27 +
      windows) installed by both bootstrap scripts, keyed by the package's
      platform since either host cross-builds both targets. Verified idempotent
      on this host; `bootstrap.ps1` mirrors it but has not run on a Windows
      host. "Build-time only" is honoured one step further than asked: the
      engine does not even link slang — it loads it at run time, dev paths
      only (see notes).*
- [x] 142.2 **Define `IHpMaterial`** — the interface whose default
      implementations *are* the standard material. This is **D27's contract
      restated in a language that can express it**, and the same promise applies:
      adding is free, removing breaks every shipped game. `HpMaterial.fxh`'s
      field table and its "nothing is exposed before the system behind it exists"
      rule carry over unchanged.
      *Done 2026-08-06, the same night T0144 unblocked it. `IHpMaterial` lives
      in `HpSurface.slang`: six methods — `baseColor`, `metallicRoughness`,
      `occlusion`, `emissive`, `shadingNormal`, `surface` — every one with a
      default that is the standard material's former inline body, calling
      DiligentFX's getters unchanged. `surface()`'s default calls D27's
      `HpSurface` function, so the function-style contract stays alive through
      the interface. `main` is one line instantiating `HpStandardMaterial`
      (an empty conformance — the emptiness is the claim) through a generic
      `evaluateSurface<T : IHpMaterial>`, statically specialised. **The scope
      boundary, stated plainly: no game can *deliver* a conforming material
      yet** — the `.slang`-asset path is 141.1/142.7/142.8 — so the interface
      is proven by the engine's own material, not by an external module.*
- [x] 142.3 **Port `HpSurface.psh` to Slang** as the reference implementation of
      142.2, still calling DiligentFX's getters and lighting. **The pixel output
      must not change** — the existing byte-identical comparison against
      `RenderPBR.psh` is the acceptance test, and it already exists.
      *2026-08-06: the shader is `HpSurface.slang`, compiled by slang to
      SPIR-V on Vulkan with the real macros and generated structs, and **every
      measured pixel value matched the pre-Slang baseline to the last printed
      digit on both targets** (evidence in notes). The `[~]` it carried — the
      HLSL-subset constraint that kept it from being 142.2's reference
      implementation — was lifted by T0144 the same night, and the second half
      landed with 142.2: `HpStandardMaterial` conforming to `IHpMaterial` **is**
      the reference implementation now, and the dumped test frames are
      byte-identical to the pre-interface build's.*
- [x] 142.4 **Feed the generated interface structs to Slang.** `PBR_Renderer`
      emits `VSOutputStruct.generated` and friends into *Diligent's* source
      factory; Slang compiles first, so they must reach `ISlangFileSystem`
      instead. Same strings, different consumer.
      *Done 2026-08-06: `FactoryFileSystem` in `SlangCompiler.cpp` bridges the
      same compound factory Diligent uses to `ISlangFileSystem` — one
      resolution order for both compilers. One amendment earned by measurement:
      the slang-path copy of `VSInputStruct.generated` carries injected
      `[[vk::location(n)]]`, because slang ignores semantic indices (see
      notes).*
- [x] 142.5 **Pass the permutation macros through.** `DefineMacros` already
      produces exactly the `-D` set Slang needs. Verified working for
      `PBR_Textures.fxh` in the D28 probe.
      *Done 2026-08-06: forwarded verbatim per compile request; the textured
      permutation (119 macros, `USE_AO_MAP`, `TextureAttribId` constants and
      all) compiles and renders identically.*
- [ ] 142.6 **Choose the interchange format and measure it.** Slang → HLSL → DXC
      keeps Diligent's whole pipeline and lets Slang stay ignorant of nothing;
      Slang → SPIR-V skips a step. **Measure both** — cold compile time and first
      frame — rather than picking on taste. The bytecode path is proven to work
      (D28); the HLSL path is not yet tried end to end.
      *2026-08-06, half-answered by a disqualification rather than a stopwatch:
      Slang → HLSL is **not currently viable at all** — slang's HLSL output
      renames every resource (`cbFrameAttribs_0`), and Diligent binds by name,
      so the signature finds nothing. SPIR-V is adopted for Vulkan on that
      basis.*
      *The stopwatch half, measured the same night by flipping `useSlang` off
      locally and rerunning the Linux gpu suite with doctest durations —
      **slang's cold compile is 2–4x slower than glslang's** on identical
      permutations, same machine, same backend: the single-pipeline test
      0.43s → 0.59s, the lit-surface test 1.99s → 3.07s, the debug-channel
      test (the most permutations) 3.20s → 7.90s. GL cases moved <1%, which
      is the control. Real but bounded — and it is the direct argument for
      pulling T0141.3's `RenderStateCache`/`BytecodeCache` forward and for
      142.7's cooking, both of which make the cold compile a cache miss
      rather than a startup cost. First-frame-in-the-editor was not measured
      separately.*
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
      *2026-08-06: the Windows half is now verified under wine — the Windows
      suite loads `slang-compiler.dll`, compiles, and its pixels match its own
      baseline digit-for-digit. Still owed: a native Windows host run. And a
      finding that reshapes the D3D12 half: **this toolchain has no D3D12
      backend at all** — MinGW gates it out on ATL (recorded in D25), so
      DXIL/D3D12 verification is moot until the toolchain decision reopens, and
      should be closed against D25 rather than left implying work.*
- [x] 142.12 **Delete or promote the probe.** `tests/gpu/slang_spirv_probe_test.cpp`
      and `hp::probePrecompiledSpirvPipeline` are experimental scaffolding from
      D28, gated behind `HP_SLANG_PROBE_DIR`. They become the real integration
      test or they go — they must not sit half-alive.
      *Deleted 2026-08-06, and deletion was the promotion: the question the
      probe asked — does Diligent run SPIR-V from a compiler that is not its
      own — is now answered by the shipping path itself, on every Vulkan gpu
      test, against the real resource signature the probe never had. The
      public `probePrecompiledSpirvPipeline` went with it; `zig build docs`
      regenerated.*
- [ ] 142.13 **Retire the HLSL path.** When the engine's shaders are Slang, the
      hand-written `.psh`/`.fxh` in `engine/shaders/` go, along with
      `cmake/hp_embed_shaders.cmake` if cooking replaces embedding. **Two paths
      that both work is the outcome to avoid** — it is how the `CreateInfo`
      duplication that hid `TextureAttribIndices` happened.
      *2026-08-06: the hand-written `.psh` is gone (it is the `.slang` file
      now), and the drift hazard specifically is gone with it — there is
      **one source**, consumed by two compilers, never two sources.*
      *Later the same day: the GL backend's Diligent-HLSL path retired with
      the backend itself (D29/T0144) — slang is the only compiler the surface
      pipeline uses. `HpMaterial.fxh` remains, pending 142.2, and
      `hp_embed_shaders.cmake` remains pending 142.7's cooking.*

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

### 2026-08-06, overnight session — the mechanism landed, measured as it went

**The engine's material shader now compiles through Slang on Vulkan, at
pipeline-build time, with the real permutation macros and the real generated
structs — and the rendered output is identical to the last printed digit.**
Every gpu pixel test passed unchanged on Linux: lit quad (211, 144, 144), base
colour centre (116, 108, 69) variation 16.3817, shading normal 12.1651,
occlusion 6.3732, rock shaded (85, 80, 57) 14.5218, metal (12, 12, 11)
6.89045 — each value equal to the pre-Slang baseline captured the same night,
digit for digit, 803/803 assertions. A compiler swap that reproduces floats to
six significant digits is executing equivalent arithmetic, not similar
arithmetic.

**How it is shaped.** `engine/src/SlangCompiler.{hpp,cpp}` loads the slang
library at **run time** (dlopen/LoadLibrary, one exported C symbol, then COM
vtables) — no link edge, so nothing a consumer links inherits slang, the MinGW
cross-link never meets an MSVC import library, and a shipped game that reads
cooked shaders never loads it. `SurfacePipeline::build` decides per backend:
Vulkan compiles both stages (their `RenderPBR.vsh` and our surface shader)
through slang to SPIR-V and hands Diligent bytecode; OpenGL keeps Diligent's
HLSL path over the **same embedded bytes**. The library is staged beside every
test and app binary by the build, which is the one location Linux, Windows and
wine all resolve without help.

### Four findings that cost measurement, so nobody pays twice

- **Slang numbers vertex inputs sequentially; the pipeline numbers them by
  semantic.** `GetVSInputStructAndLayout` emits `Tangent : ATTRIB7` with a
  layout expecting location 7; slang put it at location 3. Caught on the CLI
  before any engine code was written, by disassembling the SPIR-V. Fixed by
  injecting `[[vk::location(n)]]` into the *slang-path copy* of the generated
  struct — the Diligent-path copy must not carry it, since neither of their
  compilers accepts the syntax.
- **Slang's HLSL and GLSL outputs rename every resource** (`cbFrameAttribs` →
  `cbFrameAttribs_0`, `g_BaseColorMap` → `g_BaseColorMap_0`), and the GL
  backend binds by name — so **the GL backend cannot consume slang output
  today**, on naming alone, before any converter question. This is why GL
  keeps Diligent's HLSL path over the same source. The known route to closing
  it is SPIRV-Cross with a renaming pass (DiligentCore does the equivalent for
  Vulkan: `SPIRVShaderResources.cpp` prefers slang instance names).
  *(Overtaken by D29/T0144, 2026-08-06: the GL backend was removed, so no
  converter is needed and the GL half of 142.13 closed with it.)*
- **The GL HLSL2GLSL converter inlines `#include` textually, before any
  preprocessing** — an include guard never runs. Including
  `HLSLDefinitions.fxh` from the shader (which slang needs first) defined
  every function in it twice on GL. The fix mirrors Diligent exactly: the
  slang path **prepends** the file, the shader includes nothing, and the same
  bytes serve both compilers. The file itself is embedded from the pinned
  submodule at build time (`hp_embed_shaders` grew an extra-files parameter),
  so it cannot drift.
- **On Windows, `slang.dll` is a forwarder shim; `slang-compiler.dll` is the
  real library** (and on Linux `libslang.so` is a symlink to
  `libslang-compiler.so`). The engine loads the real name directly.

### Where this stands at the end of the 2026-08-06 overnight session

Done and verified on hardware: **142.1, 142.4, 142.5, 142.12**, and 142.3's
rendering half. Final-tree verification: `zig build test -Dtest=all` — 302
fast (214,663 assertions) + 89 integration (515) on **both** targets, zero
failures; `-Dtest=gpu` — 22 cases (23 minus the deleted probe), 803
assertions, both targets, RTX 2080, wine running the Windows suite against
`slang-compiler.dll`; `zig build docs` green; a bounded `hp_editor` run loads
the staged library from beside the executable and renders without errors.

**Next, in rough order of readiness** *(updated 2026-08-06 after T0144 and
142.2 both landed)*: the decision 142.2 was waiting on was made for it — D29
removed the backend that posed it, and `IHpMaterial` is in (see the later
session note below). 142.7 (cooking) and T0141.3 (`RenderStateCache`) are now
the measured answer to the 2–4x cold-compile cost, and cooking is also what a
game-delivered material needs. 142.8–142.10 want editor scaffolding that does
not exist yet (T0032+). 142.11's remainder is one native-Windows-host run
plus closing the D3D12 half against D25.

### 2026-08-06, later the same night — `IHpMaterial` landed, output pinned byte-identical

T0144 removed the GL backend earlier in the evening, and 142.2 followed it.
The mechanism is exactly D28's: `interface IHpMaterial` in `HpSurface.slang`
with six defaulted methods that are the standard material's former inline
bodies; an empty `struct HpStandardMaterial : IHpMaterial` conformance; a
generic `evaluateSurface<TMaterial : IHpMaterial>` that `main` instantiates.
No struct inheritance anywhere (deprecated in slang — see the earlier
finding); `override` remains mandatory for any material that replaces a
default (D28's table measured that).

**The acceptance evidence is byte-level, not statistical**: the ten frame
dumps the textured-surface tests write (`test-frames/*.ppm`) are
**byte-identical** between the last pre-interface build and this one, and
every printed guard value is digit-identical on both targets — base colour
(116, 108, 69) var 16.382, shading normal 12.165, occlusion 6.3734, rock
(85, 80, 57) 14.5219, metal (12, 12, 11) 6.8905, lit quad (211, 144, 144).
The expression changed; the output provably did not. Full suites: fast
302/214,660, integration 89/515, gpu 15/492, both targets, docs clean.

### Smaller things worth knowing

- **The pin lives in three files** — both bootstrap scripts and the root
  `CMakeLists.txt` — and `tests/harness/pins_test.zig` now asserts they agree,
  plus that both scripts install both platform packages. A drift there reads
  as "run bootstrap" on the wrong machine, not as a pin mismatch.
- **CI's harness cache key is `hashFiles('bootstrap.sh')`**, so the first push
  re-downloads the whole harness (~635 MB + the new ~77 MB) and re-caches.
  One-time, by design.
- **`dist` was not re-verified.** The slang libraries are staged beside the
  build-tree binaries; whether `dist`'s glob sweeps them, and what a dist'd
  editor does about them, is T0128's territory and was not tested tonight.
  Until 142.7 cooks shaders, a dist'd build without the library beside the
  executable will fail pipeline creation on Vulkan — loudly, with the log
  naming the library.
- **The compile is serialised behind one mutex** and the global session lives
  for the process. No compile-time numbers were taken (142.6 owes them); the
  gpu suite's wall time did not visibly move.

### The single-source constraint — **lifted by T0144 (2026-08-06)**

*(As originally written: until GL could consume slang output,
`HpSurface.slang` had to stay inside the subset both compilers accept — no
`interface`, no generics, no `override` in this file — and custom shader
materials would have been Vulkan-only until the GL naming problem was
solved.)*

**D29 resolved this by removing the OpenGL backend entirely** (T0144): slang
is the only compiler that ever sees the engine's shaders, so the full language
— `interface`, defaults, `override`, generics — is available in
`HpSurface.slang`. 142.2 is unblocked, and the "Vulkan-only custom materials"
question is moot because Vulkan is the only backend.

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
