# T0142 — Slang as HollowPoint's shader language

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Large |
| **Phase** | 4 — Render layer |
| **Order** | 415 |
| **Created** | 2026-08-06 |
| **Refs** | **D28** (the decision), D26, D27, **D29**/[T0144](../completed/0144-remove-opengl-backend.md) — removed the OpenGL backend and with it the single-source constraint, unblocking 142.2 and closing 142.13's GL half; [T0141](../completed/0141-custom-shader-materials.md) — this supersedes how 141.1, 141.2, 141.5, 141.13 and 141.15 are built; [T0143](../completed/0143-extended-material-features.md) — the extended lineup is authored in Slang once this lands; T0087, T0096, T0086 — each adds shading their own shader work must reach; [../open/0151-shader-variants-and-compile-cost.md](../open/0151-shader-variants-and-compile-cost.md) — owns the session/module-API disposition, precompiled modules against 142.6's 2–4x, and must decide cooked-output shape *with* 142.7; [../open/0150-compute-pipelines.md](../open/0150-compute-pipelines.md) — grows `SlangCompiler`'s stage mapping beyond the two-value ternary |

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

- [x] A game can write a `.slang` material, override part of the engine's
      standard material, and see it render — without the engine changing.
      *(142.15, 2026-08-06: a three-line module overriding `baseColor` renders
      exactly (255, 0, 0) unshaded, and exactly (0, 0, 0) shaded with no light
      — the un-overridden lighting default still governing is the partial-
      override claim, measured.)*
- [x] The engine's own shaders are `.slang`, and no hand-written HLSL remains in
      `engine/shaders/`. *(142.13, 2026-08-06: the directory holds
      `HpMaterial.slang` and `HpSurface.slang` and nothing else, and the rule is
      **mechanical** — `hp_embed_shaders.cmake` embeds only `.slang` and refuses
      to run at all if a `.psh`/`.vsh`/`.fxh` appears, verified in both
      directions. Two files outside the directory are deliberate exceptions,
      argued in the notes: DiligentCore's `HLSLDefinitions.fxh`, which is read
      not authored, and `Render.cpp`'s 12-line fullscreen blit.)*
- [x] `slangc` is pinned in `.harness/`, offline after bootstrap, on both hosts.
      *(2026-08-06. The Linux host was proven when 142.1 landed. The **Windows
      host** was proven by CI and nobody went back to look: run 31082064445,
      job "Tests (Windows host, native)" on `windows-latest`, was the first
      after 142.1 changed `bootstrap.ps1` and therefore a cold harness cache —*
      `Cache not found for input keys: harness-windows-858663e0...`*, then*
      `==> installing slang 2026.14.1 (linux-x86_64)` *and*
      `==> installing slang 2026.14.1 (windows-x86_64)`*, then*
      `slang 2026.14.1 (linux-x86_64 + windows-x86_64)` *in the toolchain
      summary, then* `zig build test -Dtest=all -Dtarget=windows` *green at
      302 fast / 214,660 assertions and 89 integration / 515. **Offline after
      bootstrap** is the offline-configure job plus the fact that nothing in
      the build fetches slang. **Not proven, and it is a different claim**: no
      shader has been compiled through `slangc` **on** a Windows host, because
      a GitHub Windows runner has no GPU and the compile happens at
      pipeline-build time. That is 142.11's territory and it is answered there
      by the Windows **target**, not by the Windows host.)*
- [~] A shipped game links no Slang and reads cooked output only. *The
      **link** half is fact: `readelf -d libhp_engine.so` lists only libm,
      libc, ld-linux, libpthread and libdl — no slang, and `ldd hp_editor`
      agrees, because the runtime is `dlopen`ed. The **reads cooked output
      only** half is proven as a capability (142.7: a frame rendered from a
      cooked archive with zero compiles, byte-identical to the compiled one),
      not yet as a `dist` layout — `dist` still stages the runtime by glob,
      **twice** on Linux (43.7 MB in `bin/` and again in `lib/`) and once on
      Windows (31.5 MB). Deciding what a shipping layout contains is **T0128**,
      and the reference is recorded there.*

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
- [~] 142.6 **Choose the interchange format and measure it.** Slang → HLSL → DXC
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
      ***Closed `[~]` 2026-08-06 — the decision is made; the editor number is
      not obtainable on this host, and that was measured rather than
      assumed.*** The **format** question is answered: Slang → HLSL is
      disqualified outright, so there is no second candidate to stopwatch
      against and "measure both" is moot rather than skipped. The **cost**
      question is answered by the 2–4x above and mitigated twice since —
      141.3's persistent cache (11.88s → 2.25s on the heaviest gpu case) and
      142.7's cooking (a frame rendered from a cooked archive with **zero**
      compiles). **The editor first-frame number was attempted and
      abandoned:** the editor has no bounded-frame mode and no timestamped
      first-present marker, so the only available proxy was frames rendered in
      a fixed 15-second window — and here that is noise. Warm 174 frames,
      cache disabled 171, cold-filling 144, warm again **295**: two identical
      warm runs differ by 1.7x, more than the effect being measured, because
      the Linux target renders through `llvmpipe` (this host has no NVIDIA
      Vulkan ICD). A number off that would have been worse than none. **What
      it would take**, written down rather than left implicit: a logged
      time-to-first-present, and either a host where the Linux target reaches
      real hardware or the same measurement on the Windows target, which
      does.*
- [x] 142.7 **Cook shaders as compiled assets.** Slang → cooked output at cook
      time, keyed on content hash like everything else. **But `Cook.hpp`'s
      invariant does not hold**: it promises anything cooked can be re-cooked from
      source, and an exported game has neither `slangc` nor `.slang`. A missing
      cooked shader is **fatal, not recoverable** — the cook layer must say so
      rather than inherit the wrong contract.
      *Done 2026-08-06 as **D34**, and the decision was the work. Cooked output
      is **the same artefact as 141.3's cache** — one producer, one content-hash
      key, one store (`engine/src/ShaderCook.cpp`) — differing only in where it
      lives (VFS content, `shaders/cooked/*.hpsv`, merged across mounts) and in
      what a miss means (fatal; and the dev cache is then not consulted at all,
      because a stale one standing in for content that failed to cook is the
      bug this exists to prevent). Jointly with T0151: **per-variant SPIR-V**,
      precompiled modules permitted as cook inputs and forbidden as shipped
      artefacts, because linking at load would put the compiler back in the
      shipped game. Proved on hardware: the same scene renders the
      byte-identical frame from a cooked archive with **zero compiles**, and a
      module edited after the cook is one loud unrecoverable line plus
      T0141.12's fallback — never a silent substitution. Evidence in notes.*
- [x] 142.11 **Windows and D3D12.** The D28 probe was Linux and Vulkan only.
      DXIL output, and the `SLANG_ParameterGroup_*` naming behaviour on the D3D12
      backend, are both unverified.
      *2026-08-06, first pass: the Windows half is verified — the Windows
      suite loads `slang-compiler.dll`, compiles, and its pixels match its own
      baseline digit-for-digit. And a finding that reshapes the D3D12 half:
      **this toolchain has no D3D12 backend at all** — MinGW gates it out on
      ATL (recorded in D25), so DXIL/D3D12 verification is moot until the
      toolchain decision reopens, and should be closed against D25 rather than
      left implying work.*
      ***Closed 2026-08-06.* Both halves resolve, and one of them resolves by
      correcting this ticket rather than by new work.**
      **D3D12/DXIL is moot, not pending.** D25 records that DiligentCore's own
      probes report `HAS_ATL=FALSE, D3D11_SUPPORTED=FALSE, D3D12_SUPPORTED=FALSE,
      MINGW_BUILD=TRUE`, and D29 then made Vulkan the only backend outright.
      There is no D3D12 backend to emit DXIL for and no signature to name
      `SLANG_ParameterGroup_*` into. Verifying it would require MSVC, a real
      Windows SDK, and giving up cross-compiling Windows from Linux (D1/D3) —
      which is a *toolchain* decision the decision log already took, not a
      slang question. **If D25 ever reopens, this is the work that comes back**,
      and D25's Refs now say so.
      **The Windows run is not owed, and this ticket said otherwise because
      `CLAUDE.md` did.** "Still owed: a native Windows host run" rested on the
      belief that the Windows suite runs under wine here. It does not. Measured:
      `+- test (windows-x86_64, gpu) as a real Windows process via WSL interop
      success 55s`, against `NVIDIA GeForce RTX 4070 Laptop GPU`, loading
      `slang-compiler.dll` through the real Windows loader — 26 cases, 562
      assertions. `build.zig` calls wine "a degraded path, not a normal one"
      and prints which runner it chose precisely so this is read rather than
      assumed (T0125); two sessions assumed. Corrected in `CLAUDE.md` (commit
      3da6656). **The distinct claim that remains is the Windows *host* build**,
      which is T0142's `slangc is pinned` Done-when, and CI proves that
      separately — see there.*
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
- [x] 142.14 **VFS-backed shader source through `ISlangFileSystem`** (was
      T0141.13, numbered 2026-08-06 — it had been "moved to T0142" with no
      number to land on). A game's `.slang` is content (D13) and must reach the
      compiler through the VFS. **Resolution order: engine → game → DiligentFX**,
      engine and DiligentFX names reserved, extending the same compound factory
      `FactoryFileSystem` already bridges — one resolution order, both
      compilers. **Hard blocker for 142.15.**
      *Done 2026-08-06: `VfsShaderSourceFactory` in `ShaderSources.cpp`, second
      in the compound chain, safe with no mounts. **The reservation had to be
      enforced, not just ordered, and the test is what proved it**: Diligent's
      include-relative candidate (`shaders/HpMaterial.fxh`) resolves from the
      mount before the bare name reaches the engine's copy, so a poison file
      beside a game shader redefined the contract on the first run. The VFS
      source now refuses any path whose basename matches an embedded engine
      shader, warning once per path. gpu test: a mounted `.psh` including
      `HpMaterial.fxh` compiles against the engine's copy with the poison
      mounted beside it, and stops resolving when the mount goes. DiligentFX
      names remain a documented-only reservation — their factory cannot be
      enumerated.*
- [x] 142.15 **The `.slang` material asset, and a `shader` field on
      `Material`** (was T0141.1, numbered 2026-08-06 — same orphan). A material
      must be able to name a shader before anything can cook (142.7), reload
      (142.8) or reflect (142.9) one; T0060 deliberately did not foreclose the
      field. The module implements `IHpMaterial` (142.2) and arrives through
      142.14. A material whose shader is missing renders T0141.12's
      checkerboard; one whose shader will not compile is T0141.4's case.
      *Done 2026-08-06. `ShaderAsset` (`.slang`, `AssetKind::Shader`,
      device-free — identity plus path plus load-time source, with the
      compiler re-reading the path through the VFS at pipeline-build time, so
      an edited module is picked up by the next build); `Material::shader`
      (Guid, reflected, so serialization came free); the module rides into
      `HpSurface.slang` through a per-pipeline generated one-line include
      (`HpMaterialModule.generated`) rather than `#include MACRO`, which is
      what lets the content hasher recurse into the module's own text and
      keeps the SPIR-V cache honest when a game edits its shader. The
      authoring shape is `struct HpMaterial : IHpMaterial` with `override`
      mandatory; the pipeline cache keys on the module path beside the PSO
      key. Missing-shader renders the fallback (gpu-tested; on a UV-less mesh
      it degrades to flat magenta, the documented form). **What this is not
      yet**: no parameters of its own (142.9's ParameterBlock work), no hot
      reload (142.8 — though a changed file is picked up whenever a new
      pipeline builds), and the gameplay-authoring doc gains its section when
      142.13 retires the HLSL contract file rather than before.*
- [x] 142.16 **Unshaded as a game-facing option** (was T0141.15, numbered
      2026-08-06 — same orphan). Under D28 an interface method with a default —
      not a PSO permutation bit and not a macro, though `Material::unlit`'s
      data path keeps the permutation T0141.12 built. Must stay compile-time in
      effect: the default folds away under static specialisation, and that is
      to be verified rather than hoped.
      *Done 2026-08-06, with the verification coming back **negative on the
      cost half and exact on the semantics**. `IHpMaterial.unshaded()`
      defaults false; a module overriding it renders its base colour exactly —
      (255, 255, 0) in a lightless scene where the same module without the
      override is exactly (0, 0, 0). But the fold claim failed measurement:
      slang emits the specialised call and a real branch, and the overriding
      module's SPIR-V is 19336 bytes against the shaded 19348 — at
      optimization level none **and** default, identical deltas. The lighting
      is expected to fold in the driver, unproven; **T0151's link-time
      constants are the provable mechanism and the hand-off is recorded on
      both tickets.** The test captures both byte counts every run (a
      per-run probe token defeats the content cache) so a slang upgrade that
      starts folding shows up as a diverging pair.*
- [x] 142.13 **Retire the HLSL path.** When the engine's shaders are Slang, the
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
      *Closed 2026-08-06. `HpMaterial.fxh` → `HpMaterial.slang`, and the
      conversion turned out to be **documentation** rather than syntax: the file
      was already compiled only by slang, so what was wrong with it was what it
      claimed. It told a developer to write a free `HpSurface` that a game has
      not been able to define since 142.15 — measured,
      `error[E30201]: function 'HpSurface' already has a body`. **`hp_embed_shaders.cmake`
      survives**, and D34 is why, not a preference: the cooked-shader key is a
      content hash over the resolved source, so a shipped game reads the shader
      text before it finds the bytecode. The rule is now enforced by that script
      rather than remembered. And the last second compiler reachable from engine
      code is gone: `compileEngineShader` compiled through Diligent's HLSL front
      end and now goes through slang — which is what caught the include-resolution
      divergence in the notes.*

## Closed 2026-08-06 — Slang is the shader language, and the pixels never noticed the change

**D28's bet was that a compiler swap could be invisible.** It was, to six
significant figures, twice: once when the shader moved to Slang (142.3) and
again when it moved behind an `interface` (142.2). The ten frame dumps the
textured-surface tests write are **byte-identical** across both changes, and
every printed guard value is digit-identical on both targets — base colour
(116, 108, 69) var 16.382, shading normal 12.165, occlusion 6.3734, rock
(85, 80, 57) 14.5219, metal (12, 12, 11) 6.8905, lit quad (211, 144, 144). A
compiler swap that reproduces floats to six digits is executing equivalent
arithmetic, not similar arithmetic.

| The claim | The evidence |
|---|---|
| A game authors a `.slang` material and it renders, without the engine changing | 142.15 — a three-line module overriding `baseColor` renders exactly (255, 0, 0); the methods it does *not* override still govern, which is the partial-override claim |
| The engine's own shaders are `.slang`, mechanically | 142.13 — `engine/shaders/` holds two `.slang` files and `hp_embed_shaders.cmake` **refuses to run** if a `.psh`/`.vsh`/`.fxh` appears, verified in both directions |
| `slangc` is pinned on both hosts | CI run 31082064445, Windows host, cold harness cache: `==> installing slang 2026.14.1` for both packages, then 302 fast + 89 integration green with `-Dtarget=windows` |
| A shipped game reads cooked output | 142.7/**D34** — the same scene renders the byte-identical frame from a cooked archive with **zero** compiles; a variant that missed the cook is one loud unrecoverable line plus the fallback, never a silent substitution |
| The engine links no Slang | `readelf -d libhp_engine.so` lists libm, libc, ld-linux, libpthread, libdl — the runtime is `dlopen`ed |

**Final verification, this tree, 2026-08-06:** `zig build all` exit 0 with no
`^FAILED:|error:`; fast 310/214,690 and integration 89/515 on both targets; gpu
26 cases / 562 assertions on both targets; `zig build docs` green. The Windows
target runs **as a real Windows process via WSL interop** on an RTX 4070 — see
the correction note below, because this ticket said "under wine" throughout and
was wrong about it.

**What is honestly not finished**, and each is a `[~]` or a Descoped row rather
than a quiet omission: `dist` still stages the slang runtime by glob, twice on
Linux (T0128 owns the layout); the editor first-frame number could not be
measured on this host and 142.6 says exactly why; and the whole editor half —
hot reload, the inspector, and the reflection-mechanism decision — went to
T0032 with its open question intact.

## Descoped 2026-08-06 — the editor half went to T0032, with its open question

**These are no longer this ticket's checklist.** All three need editor
scaffolding that does not exist — there is no `EditorLayer`, no `IEditorPanel`,
no dockspace — and T0032 is the ticket that builds it. Following `CLAUDE.md`'s
rule and the T0095 → T0105 and T0054/T0056 → T0025 precedents: close on what
was achieved, move the remainder to the ticket that unblocks it, rather than
holding a ticket open near the top of the queue for work nobody can start.

| Was | Went to | Because |
|---|---|---|
| 142.8 hot reload in the editor | **T0032 (32.7)** | There is no editor to reload *in*. The engine half is already there — a changed module is picked up whenever a new pipeline builds (142.15) — so what is missing is the panel, the watch and the rebuild trigger |
| 142.9 material inspector from Slang reflection | **T0032 (32.8)** | There is no inspector. **And it carries a live undecided question**, which is why it moved as text rather than as a pointer — see below |
| 142.10 one inspector over two reflection systems | **T0032 (32.9)** | Same: it is a statement about how a panel presents values, and there is no panel |

The Done-when *"the editor builds a material inspector from Slang reflection,
with no device and no successful compile required"* left with them, because it
is 142.9 restated and would otherwise sit unticked forever on a closed ticket.

**The question that had to survive the move, stated in full so it is not
re-derived badly.** 142.9's plan was Slang's source reflection, and **Slang's
reflection is not the automatic answer**:

- **Diligent already reflects constant-buffer contents** — name, type, offset,
  array size, nested members — through `IShader::GetConstantBufferDesc()`. It
  is **one bool away**: `LoadConstantBufferReflection`, currently `false`. That
  is the runtime-side answer and it may be enough on its own.
- **Slang's reflection buys two things Diligent's cannot**: it works with **no
  device and no successful compile**, so a panel can render while the developer
  is still typing; and **user-defined attributes survive into it** —
  `[Range(0.0, 1.0)] float roughness;` appears in `-reflection-json` as
  `userAttribs: [{name: "Range", arguments: [0.0, 1.0]}]`, which is Godot's
  `hint_range` shape carried by the compiler with no annotation parser
  (measured on the pinned slangc; see the notes). `ParameterBlock<T>` also
  works on SPIR-V, each block landing in its own descriptor set.
- **Decide deliberately rather than adopting either by default**, and note that
  141.2's original plan — annotate the source and write a parser — is beaten by
  *both* of them.

T0032 carries all of this in its own words, and its Refs point back here.

## What this changes in T0141

| T0141 subtask | Effect |
|---|---|
| 141.1 custom shader asset | **Superseded** by **142.15** — the asset is a `.slang` module |
| 141.2 parameter reflection | **Superseded** by 142.9, and reconsidered against Diligent's existing reflection |
| 141.5 hot reload | **Superseded** by 142.8 |
| 141.13 VFS shader source | **Reshaped** into **142.14** — resolution order still engine → game → DiligentFX, but through `ISlangFileSystem` |
| 141.15 `HP_UNSHADED` | **Reshaped** into **142.16** — an interface method with a default, not a macro |
| 141.6 `HpMaterial.fxh` | **Becomes** 142.2, and the file becomes `HpMaterial.slang` in 142.13. The field list and its rules survive; the language changes, and so does the authoring shape it documents |
| 141.7 / 141.8 parallax, triplanar | **Unaffected** — still a displaced `VSOutput` copy handed to their getters |
| 141.10 / 141.11 | **Done**, and 141.11 is the acceptance test for 142.3 |

## Notes / findings

### Corrected 2026-08-06 — "under wine" is wrong everywhere it appears in this ticket

Every claim below that says the Windows suite ran **under wine** is wrong, and
the error is one of reading rather than of measurement — the pixel values, the
compile counts and the byte comparisons all stand. What was misread is *what
executed them*.

`build.zig`'s `runnerFor` prefers **WSL interop** on a Linux host with a
Windows target: `binfmt_misc` hands the `.exe` to the real Windows loader, so
it runs as a genuine Windows process against the real driver (T0004). Wine is
the fallback for a real Linux box, and the code calls it *"a degraded path, not
a normal one"* (T0125). The build prints which one it picked. Measured here:

```
+- test (linux-x86_64, gpu) natively success 12s
+- test (windows-x86_64, gpu) as a real Windows process via WSL interop success 55s
```

and across that run, 58 device lines say `NVIDIA GeForce RTX 4070 Laptop GPU`
against 57 saying `llvmpipe (LLVM 20.1.2, 256 bits)` — **the Windows target on
real hardware, the Linux target on software Vulkan.** So this ticket's evidence
is *stronger* than it claimed, not weaker: `slang-compiler.dll` is being loaded
by Windows itself, and the SPIR-V it produces is being consumed by a real
NVIDIA driver.

The Linux target gets llvmpipe because this host's Vulkan ICD list is
`asahi, gfxstream, intel_hasvk, intel, lvp, nouveau, radeon, virtio` — `lvp` is
lavapipe and there is no NVIDIA ICD, though `libcuda.so`/`libd3d12.so` are
present. Environment configuration, not an engine defect; recorded because it
decides what a Linux-target gpu number is evidence *of*.

`CLAUDE.md` was the source of the error and is fixed (commit 3da6656). CI is
the one place the word still belongs: a GitHub ubuntu runner really is a plain
Linux box and really does use wine.

### For 142.9, measured on the pinned slangc: parameters and hints need no parser

- **`ParameterBlock<T>` works on SPIR-V**: each block lands in its own
  descriptor set (probe: two blocks → sets 0 and 1, members auto-assigned).
  A custom material's parameters as a ParameterBlock is the typed alternative
  to hand-rolled constant buffers when 142.9 gets there.
- **User-defined attributes survive into reflection**: `[Range(0.0, 1.0)]
  float roughness;` (declared via `[__AttributeUsage(_AttributeTargets.Var)]`)
  appears in `-reflection-json` as `userAttribs: [{name: "Range", arguments:
  [0.0, 1.0]}]` — Godot's `hint_range` shape, carried by the compiler, no
  annotation parsing. Layout and hints from one reflection pass, no device.
- Reflection JSON also lists **both entry points of a two-stage module** from
  one compile — relevant to T0146's single-module plan.

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

### 142.7 landed 2026-08-06 — cooking, and the invariant it had to refuse

**The decision is D34; this is what it cost and what it proved.**

**Shape.** `hp/ShaderCook.hpp` (public: the container, the policy, `cookShaders`
and `loadCookedShaders`), `engine/src/ShaderCook.cpp` (the one store),
`engine/src/ShaderStore.hpp` (the seam `SlangCompiler.cpp` reaches it through).
141.3's `SpirvCache` **moved** into that store rather than being duplicated
beside it — `SlangCompiler.cpp` now describes a variant (`ShaderVariantKey`:
file, stage, macros, prelude text, factory) and the store owns hashing, because
a hash computed in two places is a hash computed differently. Lookup order:
cooked archives → developer cache → compile; in cooked-only mode it stops after
the first, loudly.

**Container.** `HPSHADER` magic, format version, the pinned slang version as a
compiler id, length-prefixed payload. It reuses `Cook.hpp`'s *byte primitives*
and none of its semantics: `CookedShaderStatus` shares no value with
`CookStatus` and every one of its values is an error to report rather than a
reason to re-cook. Eight fast tests pin round-trip, empty-but-valid,
compiler mismatch, bad magic at header length, truncation, and a future format
version being refused *before* the compiler id is believed.

**Measured on hardware (RTX 4070 Laptop under wine for the Windows target;
llvmpipe for the Linux one on this WSL host — see "not verified" below):**

| claim | evidence |
|---|---|
| a cook run compiles | `cook run: 1 compile(s)` — the module carries a per-run GUID token so it cannot be served from any earlier cache |
| the archive is real | `cooked archive: 438249 bytes` (Linux), 1326301 (Windows target — more entries, same suite) |
| cooked renders identically | `cooked run: (255, 0, 0), 0 compile(s), 0 missing`, and the full readback compares **equal** to the compiled run's, not merely close |
| a missed variant is loud | `uncooked variant: 0 compile(s), 1 unrecoverable line(s), magenta 4096` — 4096/4096 centre pixels are the fallback |

Full suites after: gpu 26 cases / 560 assertions on **both** targets; fast
310 / 214,690; integration 89 / 515; `zig build docs` green.

**Two things that surprised, and are now written down:**

- **The key being a content hash means the shader *source* still ships.**
  Computing the lookup key requires resolving `HpSurface.slang` and every
  include, so a cooked build reads all of it and only then finds the bytecode.
  That settles 142.13's open question in the opposite direction to the guess:
  **cooking does not replace `hp_embed_shaders.cmake`.** The alternative — a
  cheaper key such as a PSO-key digest — is a second key mechanism and loses
  "edit any header, key changes, no staleness rule to remember".
- **`HP_SPIRV_CACHE=0` makes cooking impossible**, because the store is where a
  compiled variant is kept. `cookShaders` refuses with that reason named rather
  than writing a plausible empty archive.

**Not verified.** The Linux gpu run on this WSL host reports
`llvmpipe (LLVM 20.1.2)` for these cases while the Windows-target run under
wine gets the real RTX 4070 — so the cooked-vs-compiled byte equality is proven
on both a software and a hardware Vulkan implementation, but the *Linux*
hardware path specifically was not the one exercised. Also unverified: a cooked
archive loaded from a **pack** rather than a mounted directory (the code path is
`Vfs::read`, which does not distinguish, but no test mounts a ZIP), and two
packs each carrying an archive (the merge is `IBytecodeCache::Load`'s, which
`emplace`s, so first-loaded wins — asserted by reading their source, not by a
test).

### 142.13 landed 2026-08-06 — and the conversion was documentation, not syntax

**What was actually wrong with `HpMaterial.fxh` was what it said.** The file was
already compiled by slang and by nothing else — the GL backend that once needed
it to be HLSL went with D29/T0144 — so "port it to Slang" was a rename plus an
include-guard rename. The substance was that it documented **an authoring shape
that no longer exists**: *"a developer writes one function"*, with
`void HpSurface(in HpSurfaceInput In, inout HpSurfaceOutput Out)` forward
declared so a shader that forgot to define it failed at link.

A game has not been able to define that function since 142.15 put its module
*inside* `HpSurface.slang`. Measured on the pinned `slangc 2026.14.1` rather
than reasoned about:

```
error[E30201]: function 'HpSurface' already has a body
```

D27's function-style hook is not gone, it moved: it is `IHpMaterial.surface()`,
whose default calls the engine's own `HpSurface`. So the declaration went, the
header now documents `struct HpMaterial : IHpMaterial`, and **D27 carries an
amendment paragraph** saying the shape changed and the substance did not.

### `hp_embed_shaders.cmake` survives, and D34 is the reason

142.13 listed it as something cooking might replace. It does not, and the
argument is one sentence: **the cooked-shader key is a content hash over the
resolved source text**, so a shipped game reads `HpSurface.slang`, every header
it includes and the game's own module, and only then finds the bytecode. A
cheaper key — a PSO-key digest, say — would be a *second* key mechanism and
would lose "edit any header, the key changes, no staleness rule to remember".
Engine shader source stays embedded (three files, tens of kilobytes); cooked
SPIR-V ships as content beside it.

What did change is that the rule is now **mechanical**. The script embeds only
`*.slang` and fails outright if a `.psh`/`.vsh`/`.fxh` appears in
`engine/shaders/`, naming the decision. Verified in both directions: a clean
tree embeds 3 shaders; a stray `Stray.psh` produces
`CMake Error ... hand-written HLSL in '.../engine/shaders' ... Port it to
.slang, or change the decision deliberately`. The dependency glob in
`engine/CMakeLists.txt` deliberately stays wider than what is embedded, so
adding one *re-runs* the step and hits the error rather than being invisible.

### The last second compiler, and the bug removing it found

`compileEngineShader` compiled through **Diligent's own HLSL front end**. Only
tests called it, but it was a second compiler reachable from engine code, which
is the hazard 142.13 names. It now compiles through slang and then hands the
SPIR-V to the device — a strictly stronger claim than before, since it proves
the driver accepts the result too.

**And it immediately failed, which is the point of having done it.** A mounted
game shader at `shaders/game_probe.slang` including `"HpMaterial.slang"` did not
compile:

```
error[E15300]: include file not found
  1 | #include "HpMaterial.slang"
    |          ^^^ failed to find include file 'HpMaterial.slang'
```

**Slang resolves an include relative to the including file and stops there.**
Diligent's front end tries the relative candidate *and then the bare name*. So
`shaders/HpMaterial.slang` was refused by the VFS (reserved basename, 142.14)
and missed the engine's embedded set (keyed on bare names), and the contract
header could not be included from any subdirectory at all. 142.4's claim that
"the two compilers cannot disagree about what a name means" was **false**, and
nothing had exercised the case because the only slang consumers were at the
virtual root.

Fixed in `readSource`: relative first, then the bare name, for includes only.
**Safe because the reservation is enforced rather than ordered** — a bare-name
retry can only ever reach the engine's copy of a reserved name, never a mounted
one. The gpu probe deliberately stays in a subdirectory, because a root-level
one would pass either way.

### Two hand-written HLSL exceptions, argued rather than overlooked

- **`HLSLDefinitions.fxh`** — DiligentCore's, embedded from the pinned submodule
  at build time and *prepended* to every slang compile because the DiligentFX
  headers assume `MATRIX_ELEMENT` and friends without including them. Read,
  never authored; its content hash rides into the variant key as
  `HP_SLANG_PRELUDE` precisely because a prepend is invisible to an include
  walk. It is a parameter to the embed script rather than a file in
  `engine/shaders/`, which is the distinction.
- **`Render.cpp`'s fullscreen blit** — ~12 lines of HLSL compiled by Diligent's
  own front end (T0137). Converting it is possible and was rejected: it is the
  presentation path of last resort, and putting it behind a run-time-`dlopen`ed
  compiler means a black screen can be caused by a missing library. It shares no
  source with the surface path, so it is not the *drift* hazard 142.13 names —
  there is no second copy of anything. Recorded in `hp_embed_shaders.cmake` so
  the next person does not have to rediscover the reasoning.

**Pixels unchanged**, which is the acceptance test: every gpu `MESSAGE` line is
identical before and after the rename (`(195, 195, 195) variation 8.92733`, and
the rest), 26 cases / 562 assertions on both targets.

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
