# T0161 — The game resource model: a module declares its own resources, by name, at every stage

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 464 |
| **Created** | 2026-08-07 |
| **Blocked by** | nothing |
| **Refs** | **Establishes D35** ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)); [../../documentation/13-shader-capability-matrix.md](../../documentation/13-shader-capability-matrix.md) — flips the `Game-declared textures` cell from **P** to **Y**; [../completed/0160-material-declared-parameters.md](../completed/0160-material-declared-parameters.md) — **this generalises what it shipped and breaks nothing it shipped**; [0146-vertex-stage-hook.md](../completed/0146-vertex-stage-hook.md), [0150-compute-pipelines.md](../open/0150-compute-pipelines.md), [0148-post-process-stack.md](../open/0148-post-process-stack.md) — **all three consume this; it must land first**; [0147-engine-intermediates-for-shaders.md](../completed/0147-engine-intermediates-for-shaders.md) — the *other* side of the two-namespace line; [0143-extended-material-features.md](../open/0143-extended-material-features.md) — untouched, but gains four descriptors back; [0151-shader-variants-and-compile-cost.md](../open/0151-shader-variants-and-compile-cost.md) — adds **no** permutation axis; [0058-asset-lifetime-hot-reload.md](../open/0058-asset-lifetime-hot-reload.md) — widens an already-open invalidation surface; **D13**, **D23**, **D26**, **D27** (amended), **D28**, **D34** |

## Why

**T0160 gave a game author named *parameters* and left it holding four engine-named texture slots.** `HpTexture0`…`HpTexture3` — fixed names, fixed count, declared once in a signature built before any shader module exists. A detail-map shader writes `HpTexture0` and a comment explaining which is which.

That is a limit on the game developer, and the owner's standing requirement is that there are none.

**And it is not a material problem.** The same wall stands in front of every stage a game will author into: a vertex shader wanting a wind field (T0146), a compute shader — which is *nothing but* declared resources (T0150), a post-process effect wanting its own LUT (T0148). Solve it inside the material pipeline and all three rediscover it, which is precisely the failure this line of work exists to stop repeating.

## The design, researched 2026-08-07

**Reflect the module's own author-named declarations off the compile that already happens; build a second, per-module `PipelineResourceSignature` from that reflection at PSO-creation time; bind it beside the engine's base signature.** Identical mechanism for all four stages. Sampling state comes from an engine-declared **sampler palette**, so the one thing SPIR-V cannot carry never needs carrying.

What an author writes — no registration, no attributes, no engine identifiers:

```slang
Texture2DArray detailAlbedo;      // their name, their count
Texture2DArray puddleMask;

cbuffer HpMaterialParams          // unchanged from T0160
{
    [HpRange(0.0, 1.0)] float wetness;
}
```

```yaml
textures:
  - name: detailAlbedo            # was: HpTexture0
    texture: 1570000000000031
```

**There is no circularity.** `SurfacePipeline::build` creates the `IShader` objects and *then* the PSO; the signature is needed only for the second step. Compile → reflect → build a signature naming `detailAlbedo` → create the PSO.

### Two costs that were overstated, corrected by reading the vendored source

- **A signature occupies at most 2 Vulkan descriptor sets, and only with `DYNAMIC` variables.** Everything in the engine's is `MUTABLE`, so the base is **one** set. Base + module = **2**, against a spec floor of 4 — measured on this machine, RTX 2080 reports 32 and llvmpipe 8. The `maxBoundDescriptorSets` objection dissolves.
- **The second `CommitShaderResources` is an API call over work already being done.** `cbPrimitiveAttribs` is `USAGE_DYNAMIC` and mapped per draw, so Diligent already rebinds every descriptor set every draw in a **single** `vkCmdBindDescriptorSets`; the module set is one more handle in an array already submitted. The transition-verify branch is `DILIGENT_DEVELOPMENT`-only.

### The sampler palette, which deletes a problem rather than solving it

The engine pre-declares `HpSamplerLinearWrap`, `HpSamplerLinearClamp`, `HpSamplerPointWrap`, `HpSamplerPointClamp`, `HpSamplerAnisoWrap`, `HpSamplerAnisoClamp` as immutable samplers in the base signature. **An author picks filtering by naming the sampler in code**, so the choice travels inside the SPIR-V and survives cooking with zero metadata. Godot's own design, verified against its forward-clustered source.

## Done when

- [x] A game module **declares its own textures and buffers under its own names**, at arbitrary count, and a `.hpmat` binds them by those names — textures end to end; buffer declarations ride the same helper (`ModuleSignaturePolicy::allowBuffers`, carrying BUFFER_SRV/UAV and TEXTURE_UAV) and a *surface* module declaring one is refused by name until T0147/T0150 provide a data path that could feed it — their Refs now carry the pointer
- [x] The **same mechanism is demonstrably stage-neutral** — the signature-from-reflection helper is written so T0146/T0150/T0148 add only "engine pass signature at index 0"
- [x] **The go/no-go measurement exists** (below), and the decision is recorded against the number rather than the argument
- [x] **`HpTexture0…3` are migrated**, and a shipped v2 `.hpmat` still loads and renders identically
- [x] **Plain glTF materials are byte-identical** and carry *fewer* descriptors than before
- [x] **D35 is written**, so the stage-agnostic rule is a decision rather than a thing we remembered once

## Subtasks

- [x] 161.1 **Measure first — this is the gate.** Wall-clock CPU frame time for a scene of N custom-material draws, base signature vs base + per-module, on this machine and on llvmpipe. The architecture argument says the marginal cost is one handle in an existing bind call; this project's rule is that an argument is not a number. **If it is expensive, stop and report rather than proceeding.**
- [x] 161.2 **The sampler palette** in the base signature and the contract, replacing the hardcoded `Sam_LinearWrap` on module slots
- [x] 161.3 **Generalise reflection**: keep *every* author-named resource rather than matching the four reserved names. Dev-time slang reflection sees resource **dimension**; Diligent's `ShaderResourceDesc` does not — so **check dimension there, loudly, by name**, or a `Texture2D`/`Texture2DArray` mismatch surfaces as a raw Vulkan validation error in cooked builds
- [x] 161.4 **Build the per-module signature** at PSO-creation time, cached by the module's content hash so two materials sharing a module share the signature
- [x] 161.5 **The module SRB**, created lazily on the draw path beside the existing one, with `bindShaderParamSlots` generalised from four fixed slots to a walk over arbitrary names — its white-fallback, checkerboard and GUID-resolution logic transfers verbatim
- [x] 161.6 **Migrate `HpTexture0…3`.** They stay as deprecated-but-working declarations; a v2 `.hpmat` naming them binds through the same generalised walk. **Removal from the shared signature is forced, not optional** — a duplicate across signatures is a loud PSO-creation failure, so the migration cannot silently half-happen
- [x] 161.7 **Test it properly**: an author-named texture end to end (declare, value, render, exact pixels); a v2 `.hpmat` unchanged; the plain-material byte-identical baseline; the descriptor count *dropping*; a dimension mismatch failing by name; the magenta guard on every asserted frame
- [x] 161.8 **Write D35** and flip the matrix's `Game-declared textures` cell to **Y**, with the sampler-state cap recorded as the one deliberate limit

## Not in scope

- **Author-defined sampler *state*** — custom aniso level, border colour, compare function. The palette is the vocabulary in v1. Godot has shipped the identical restriction through every 4.x release, and its demand is concentrated on compare samplers for custom shadow sampling, which is an engine-fed concern here (T0086/T0147). The escape is additive: an `[HpSampler(...)]` attribute read at dev-time reflection and persisted in the cooked entry's header. **Wait for a real request**; it is metadata a cooked build must carry.
- **Bindless / descriptor indexing.** The hardware is willing — `runtimeDescriptorArray = true` on both local devices — and the engine does not request the feature. It buys nothing this design does not, gives materials a worse authoring shape (indices instead of declarations), and solves only textures while compute needs buffers too. It is the right tool for **T0143's** seventeen-slot budget, not for game declarations.
- **`ParameterBlock<T>`.** Investigated and rejected on evidence — see the findings.
- **The other stages' hooks.** T0146, T0150 and T0148 own their own pipelines; this ticket owes them a stage-neutral helper, not their implementations.

## Notes / findings

### 161.1 — the gate passed, measured 2026-08-07

`tests/gpu/module_signature_cost_test.cpp`, raw Diligent against the engine's
device: two pipelines drawing 1000 tiny draws per frame into a 64x64 offscreen
target, each draw mapping a dynamic constant buffer with DISCARD, committing
and drawing — the engine's exact per-draw shape. Variant A: one signature, one
`CommitShaderResources`. Variant B: base + a module signature at binding 1
(texture + `HpMaterialParams` cbuffer), **two** commits **and a second
per-draw dynamic map** (what `writeShaderParams` does), so the delta is the
whole T0161 cost, not the commit alone. Median over 120 frames after 16 warm-up.

| device | base ms/frame | base+module | base rerun | **delta/draw** |
|---|---|---|---|---|
| RTX 2080 | 0.108 | 0.143 | 0.096 | **34 ns** |
| llvmpipe (LLVM 15) | 0.242 | 0.282 | 0.248 | **40 ns** |

The scene-path baseline before the migration (same file, second case: 400
draws of a param-declaring custom material through `SceneView`): median
**1.873 ms** RTX 2080, **0.630 ms** llvmpipe. 34–40 ns per draw against a
~4.7 µs per-draw engine path is under 1%. **Go.** The number agrees with the
argument (one more set handle in a `vkCmdBindDescriptorSets` already recorded
per draw), and the decision is recorded against the number.

**The first run of the harness produced 213 µs/draw and it was the harness.**
The loop never called `FinishFrame`, so a thousand DISCARD maps per frame
accumulated in Diligent's per-frame dynamic heap with nothing recycling it —
timings degraded progressively, the variant measured *second* inherited an
exhausted heap, and base's own mean sat 50x above its median. Both symptoms
now have standing controls in the test: a base **rerun after** the module
variant (order effect shows as first-base vs rerun disagreement), and
mean-vs-median printed side by side. "When a check fails, suspect the check"
— it was the check.

### `ParameterBlock<T>` was investigated seriously and is not load-bearing

Measured on the pinned `slangc 2026.14.1`: a `ParameterBlock<T>` of two textures, a sampler and two uniform fields compiles to SPIR-V with **separate globals named `gGame.flowMap`, `gGame.detailMap`**, plus an implicit std140 buffer, all decorated into one descriptor set.

That single-set mapping is its entire value, and **under Diligent it is void**: `PipelineStateVkImpl` rewrites every set and binding decoration to match *its* signature at PSO creation, matching by name. Slang's assignment is overwritten before the driver sees it. What survives is the names — and they arrive **dotted**, which would leak into every `.hpmat` as `name: gGame.detailMap`. A strictly worse authoring surface for zero binding benefit.

It is the right answer in a Slang-native binding model (Falcor's shader objects). The reflection path here sees *through* it, so accepting it later as authoring sugar stays possible.

### Nobody ships bindless as the authoring surface

It is the default in GPU-driven AAA renderers — id Tech 7, SM6.6's `ResourceDescriptorHeap` — but **every surveyed engine gives authors named declarations and reflects them**, with bindless as an internal batching strategy where present. Godot binds a per-shader material set; Bevy derives `AsBindGroup`; Unity binds compute resources by string name; Unreal reflects C++ parameter structs. WebGPU is only now standardising a bindless proposal, and its `maxBindGroups` default of **4** means two signatures sits comfortably inside the most conservative binding model in the industry.

### The descriptor budget improves

Removing the four slots and the params buffer from the shared signature takes every **plain glTF material** from 10 sampled images / 11 immutable samplers to **6 / 7**, plus the shared palette in the base signature. Custom materials pay exactly what they declare. T0143's projected ~26 images drops by the four it would have inherited.

### Landed 2026-08-07 — what shipped, and the evidence

**The mechanism** (commit after the gate): the compiled pixel shader's Diligent
reflection is walked, every name the engine's signatures own is subtracted, and
what is left — the module's, by construction, since it is exactly the set PSO
creation would fail on — becomes a second `PipelineResourceSignature` at
binding 1. `engine/src/ModuleResourceSignature.{hpp,cpp}` is the stage-neutral
helper: a stage passes its base-signature names and a `ModuleSignaturePolicy`
(`requireTexture2DArray`, `allowBuffers`, `onlyConstantBuffer`), and T0146 /
T0150 / T0148 / T0147 now carry Refs saying so. Signatures are cached on the
hash of the **resource set** — see below for why not the module path.
`HpMaterialParams` moved into the module signature; the module SRB is created
lazily on the draw path and cached per signature on the material binding; the
old slot binder's white/checkerboard/GUID logic transferred to author names.

**Evidence, all on both targets (wine included), RTX 2080:**

- gpu suite 45/45, fast 321 + integration 92 on both targets, `zig build all`
  and `zig build docs` clean.
- Author-named end to end: 6 declared textures (2 more than the old slot
  count), 2 bound by `.hpmat` name, 1 sampled-unbound reading the documented
  white, 3 unused costing nothing, the declared parameter riding the same
  signature — **(255, 255, 137) exact**, magenta count 0, three palette
  samplers doing the sampling.
- Shipped v2 `.hpmat`: the rockcube sample suite (exact-pixel assertions over
  `rock.hpmat` + `rock_pom.slang`) and T0160's `HpTexture0` test pass
  unchanged.
- Plain materials byte-identical: `no-height vs zero-scale: 0` on all three
  parallax baselines.
- Descriptors dropped: base signature counted at creation — **6 sampled
  images, 13 immutable samplers, 4 constant buffers** against T0160's 10/11,
  logged by the renderer and pinned by a test.
- Refusals by name, once each, checkerboard rendered: `Texture2D` dimension
  mismatch (dev-time slang shape, the only place it is visible); an
  author-declared `SamplerState` (palette listed in the line); a surface
  buffer; an array-of-textures; a second constant buffer.
- Scene path after migration: 400 custom draws at 1.86–1.93 ms/frame against
  the 1.87 ms pre-migration baseline on the RTX 2080, and 0.607 ms against
  0.630 pre-migration on llvmpipe — the 34–40 ns/draw the gate measured is
  invisible end to end, on both devices.

### Findings the next stage should know

- **The signature cache key is the resource *set*, not the module's content
  hash the subtask named.** Reflection reads the compiled SPIR-V and slang
  strips unused globals, so two *permutations* of one module can carry
  different used sets (rock_pom's whole body is inside a permutation `#if`).
  A path- or content-keyed signature would either underdeclare for one
  permutation (PSO failure) or force declaration-set reflection the cooked
  path cannot produce. Keying on the set itself makes every PSO's signature
  exactly right and shares harder than the subtask asked — two different
  modules with the same declarations share too. The SRB cache on the material
  binding is keyed per signature pointer for the same reason.
- **`layout.textures` is the used set**, so a texture sampled only inside an
  inactive permutation is absent from that permutation's layout; a `.hpmat`
  naming it is leniently ignored, same as every other absent name. Recorded
  in `ShaderParamLayout`'s doc.
- **The legacy slot samplers are `#define` aliases** of `HpSamplerLinearWrap`
  in the contract (bit-identical state), not four extra immutable samplers —
  which is why the sampler count is 13 and not 17.
- **The palette is declared `VS_PS`**, so T0146's vertex modules sample it
  with no base-signature change.
- **`gen_shader_docs.py` now documents top-level globals** in public regions —
  the palette and the deprecated slots were invisible to it before, and the
  conventions that are not declarations (reserved block name, the
  two-namespace split, the sampler cap) moved into `HpMaterial.slang`'s file
  preamble, which the generator already emits. Nothing new was built; the
  docs-system ticket keeps its scope.
- **A benchmark loop that never presents must call `FinishFrame` itself** or
  Diligent's per-frame dynamic heap grows without bound — the 213 µs/draw
  first reading, kept as a worked example in the 161.1 findings above, with
  the base-rerun control now standing in the test.

### What could not be determined

- **The wall-clock cost of the second commit** — 161.1 exists to produce it, and it is the gate.
- Whether Diligent accepts non-identifier (dotted) resource names in a signature. Moot under this design; relevant only if `ParameterBlock` is ever revisited.
- Signature-creation latency on the first-draw hitch — expected to disappear into the PSO build that already causes one.
- Min-spec descriptor-indexing reality beyond this desk. Irrelevant here, relevant to any future bindless move.
- Hot reload remains **traced, not executed** (T0058), and this widens that surface rather than breaking a working one.
- D28's deviceless-reflection promise stays unmet: parameters and resources are known only after a pipeline is built. This design neither fixes nor worsens it.
