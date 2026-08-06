# T0151 — Shader variants bounded: precompiled modules, link-time specialisation, and the escape hatch

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 445 |
| **Created** | 2026-08-06 |
| **Refs** | [../inprogress/0141-custom-shader-materials.md](../inprogress/0141-custom-shader-materials.md) — its Done-when requires "variant growth bounded by a decision that is written down", and this ticket is where that decision's mechanisms live; 141.3 (the cache) and [../inprogress/0142-slang-shader-language.md](../inprogress/0142-slang-shader-language.md) 142.6 (the measured 2–4x) / 142.7 (cooking) are the near-term mitigations this composes with; [0143-extended-material-features.md](0143-extended-material-features.md) — 143.8's permutation count is the pressure gauge; **D28**, **D30** ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)) |

## Why

**"Variants are the thing that grows without limit"** — T0141's inherited
note, and its Done-when demands a written bound that still does not exist.
The pressure is measured, not feared: slang's cold compile is **2–4x**
glslang's on identical permutations (142.6), T0143 adds six feature bits that
multiply the PSO space (143.8), and T0145/T0146 add lighting- and
vertex-stage axes on top. The cache (141.3) and cooking (142.7) amortise the
cost per variant; **nothing yet bounds the number of variants**, and nothing
owned the question. This ticket owns it.

Three mechanisms were probed on the pinned `slangc 2026.14.1` (2026-08-06;
probe sources in the scratchpad session, results restated here so they
survive):

**1. Precompiled modules are real and fast.** A 600-function synthetic
library precompiled to a binary `.slang-module` (0.69s, once) cut the
*consumer's* compile from **0.71s to 0.33s** — the front-end cost of imported
code is paid at precompile time, not per shader. The engine's own module set
(the surface shader's includes) is the real candidate; the synthetic number
only proves the mechanism.

**2. Link-time constants specialise completely.** A module declaring
`extern static const int kLightingMode;` and a three-line variant module
providing `export static const int kLightingMode = 2;` produced SPIR-V with
**zero conditional branches** — the knob folds exactly as a preprocessor
define would, but the library module is compiled **once** and each variant is
a trivial module plus codegen, not a full re-front-end. That is permutation
without the 2–4x, for every axis the *engine* owns.

**3. Dynamic dispatch exists, works on SPIR-V, and has a real prerequisite.**
An interface-typed value from a buffer with `-conformance Red:IMaterial=0
-conformance Checker:IMaterial=1` compiles to an `OpSwitch` over type IDs —
one pipeline, N materials, the permutation count's hard ceiling. Two measured
costs: an existential **may not hold opaque members** (`error[E33080]` on a
`Texture2D` field), so textured materials need `DescriptorHandle<T>` bindless
handles — which compile, and demand `RuntimeDescriptorArray` descriptor
indexing the engine has not adopted — and the dispatch is a per-wave branch
whose register pressure is the *maximum* over registered materials.

## The honest scope limits, stated up front

- **DiligentFX's own permutation axes stay preprocessor.** Their headers are
  `#if ENABLE_SHEEN` all the way down, and a module carries its own
  preprocessor state — link-time constants cannot reach into their `.fxh`.
  The audit in 151.1 is which of the **engine's** axes (the ~119 macros
  `DefineMacros` emits are mostly theirs) can move; the answer may be "few,
  today", and that is a finding, not a failure.
- **The engine's compile path cannot load modules today.** `SlangCompiler.cpp`
  deliberately uses the deprecated compile-request API (recorded at the
  `createCompileRequest` call, with the reasoning); precompiled modules and
  link-time linking need the session/module API. Adopting it re-verifies what
  the probe-era code settled — that cost is real and belongs in this ticket,
  not discovered inside it.

## Done when

- [ ] **The variant-growth decision exists in writing** — what bounds the
      count (enumerated axes? on-demand with cache? cooked closed set per
      game?), where new axes must be argued, and what T0141's Done-when can
      point at
- [ ] The **axis audit** is done: every current permutation axis listed with
      its owner (theirs/ours), and each engine axis marked
      macro / link-time-constant / runtime-branch, with the migration cost
- [ ] Precompiled modules are measured **on the real shader set**, not the
      synthetic — the number replaces the 0.71→0.33 proxy, and the
      session-API migration it requires is dispositioned (done, or rejected
      with the reasoning)
- [ ] Cooking (142.7) knows this ticket's answer: whether cooked output is
      per-variant SPIR-V, precompiled modules + link at load, or both —
      decided *with* 142.7, referenced both ways
- [ ] The **dynamic-dispatch escape hatch is recorded with a trigger**: the
      measured condition (pipeline count or cold-compile seconds after cache +
      cook) that reopens it, and the named prerequisite (bindless descriptor
      indexing) so the cost is priced before it is wanted
- [ ] Godot's answer is on file for comparison (it is below), so the decision
      is made knowing the industry baseline rather than re-deriving it

## Subtasks

- [ ] 151.1 The axis audit (ours vs theirs, per-axis mechanism)
- [ ] 151.2 The session/module API disposition for `SlangCompiler` — the
      deprecated-API note names a pin bump as the revisit point; this is the
      other legitimate one
- [ ] 151.3 Precompile the engine module set, measure real cold compile
      against 142.6's numbers
- [ ] 151.4 Move one engine axis to a link-time constant end to end, measure,
      and only then decide the rest (one proof before a policy)
- [ ] 151.5 Write the bound, cross-reference T0141/142.7/143.8
- [ ] 151.6 Record the dynamic-dispatch trigger and its bindless prerequisite

## Notes / findings

### Godot's answer to the same problem (4.7.1, surveyed 2026-08-06)

Specialisation constants + **ubershaders** (4.4): a generic variant compiled
ahead of time runs any specialisation immediately while the optimised pipeline
compiles in the background — trading a slower frame for no stutter. Plus a
**shader baker** (4.5) that ships precompiled intermediates in the export.
The mapping here: the ubershader's role is played by runtime-branch axes (the
debug views already made that call deliberately), the baker's by 142.7's
cooking, and the background-specialise trick is available to 141.3's cache if
hitching is ever measured. Vulkan specialisation constants themselves are a
fourth mechanism this ticket's audit should at least name — Diligent exposes
them, and they sit *between* link-time constants (free, needs relink) and
runtime branches (costs registers, free to switch).

### Why this is not folded into 141.3 or 142.7

Those amortise the cost of each variant; this bounds how many exist and moves
axes to cheaper mechanisms. They compose — the cache stays right whatever this
decides — and they are owned by in-flight tickets whose scope should not grow
mid-session. The two-way references are the contract between them.
