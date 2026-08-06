# T0151 — Shader variants bounded: precompiled modules, link-time specialisation, and the escape hatch

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 445 |
| **Created** | 2026-08-06 |
| **Refs** | [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) — its Done-when requires "variant growth bounded by a decision that is written down", and this ticket is where that decision's mechanisms live; 141.3 (the cache) and [../completed/0142-slang-shader-language.md](../completed/0142-slang-shader-language.md) 142.6 (the measured 2–4x) / 142.7 (cooking) are the near-term mitigations this composes with; [0143-extended-material-features.md](0143-extended-material-features.md) — 143.8's permutation count is the pressure gauge; [0153-surface-detiling.md](0153-surface-detiling.md) — **153.8 registers three de-tiling tiers as a further axis**, and it is the standing proof that the axis list is open: any bound written here must accommodate axes that do not exist yet; **D28**, **D30**, **D34** ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)) — **D34 bounds this ticket's output**: whatever mechanism it picks, a shipped game receives per-variant SPIR-V and links no compiler; [../completed/0160-material-declared-parameters.md](../completed/0160-material-declared-parameters.md) — **registers a non-axis** (160.7): declared parameters add no permutation bit, see the note below |

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
- [x] Cooking (142.7) knows this ticket's answer: whether cooked output is
      per-variant SPIR-V, precompiled modules + link at load, or both —
      decided *with* 142.7, referenced both ways.
      **Answered 2026-08-06 as D34, jointly, while 142.7 landed: cooked output
      is per-variant SPIR-V.** Precompiled modules are permitted as *inputs to
      the cook* and forbidden as *shipped artefacts* — see the note below,
      which is the constraint this ticket now works inside.
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
- [ ] 151.5 Write the bound, cross-reference T0141/142.7/143.8. **T0141 closed
      2026-08-06 having descoped that Done-when to this ticket** — nothing there
      bounds the variant *count*, and nobody rereads a closed ticket, so the bound
      is this ticket's alone to write
- [ ] 151.6 Record the dynamic-dispatch trigger and its bindless prerequisite

## Notes / findings

### Registered by T0160.7 (2026-08-06): declared material parameters add NO permutation axis

Module identity already keys the pipeline (`cacheKey` mixes the module path;
the SPIR-V cache hashes its content), and a parameter *value* is data written
into a constant buffer — changing it rebuilds nothing and invalidates no cook.
That is the entire point of T0160's design, and it hands this ticket a rule
worth stating wherever the axis audit lands: **prefer a runtime parameter over
a permutation bit** whenever the branch it feeds is cheap enough to leave in
the code. A bit costs a variant per combination forever; a parameter costs a
uniform read. The axis audit should treat any proposed new bit as guilty until
it proves a parameter cannot express it.

### From 142.7 / D34 (2026-08-06) — the cooked-output shape is decided, and it constrains 151.2 and 151.3

**Cooked output is per-variant SPIR-V**, in the same store and under the same
content-hash key as T0141.3's developer cache. The joint decision is D34; what
matters to *this* ticket is the constraint it imposes, which is one sentence:

> Precompiled modules and link-time constants are **inputs to the cook, never
> artefacts of it.**

The reason is not aesthetic. Linking at load needs the slang runtime **in the
shipped game** — 34 MB of compiler and a load-time link step on the player's
machine — which is the thing T0142's Done-when forbids and the thing cooking
exists to remove. So 151.3 may measure precompiled modules and adopt them to
make the *cook* faster; 151.4 may move an engine axis to a link-time constant
and cook the resulting variants; neither may make a player's machine do
anything but `memcpy`.

**151.2's disposition gained no new pressure from cooking, and that is worth
recording rather than assuming.** 142.7 reused `compileSlangToSpirv` untouched
— the deprecated compile-request API is still what runs, because cooking is the
existing compile driven early rather than a different compile. The session/module
migration remains owned here, argued on precompiled modules alone.

**One measurement this ticket can now make that it could not before.** With
cooking in place, "how many variants does a project actually have" is a
countable number rather than an estimate: the archive is one entry per variant.
The gpu suite's own cook came out at 438 KB (Linux) and 1.33 MB (the Windows
target, more entries) after the whole bucket — a real, if small, first data
point for 151.1's audit.

**And the axis list is open, which the bound must survive.** D34 fixes the
*form* of cooked output and says nothing about how many entries it has. T0143's
six feature bits (143.8) and T0153's three de-tiling tiers (153.8) both arrive
later and both multiply the space, so 151.5's written bound has to be a rule new
axes are argued against — not an enumeration of today's, which would be stale
before it was committed.

### From 142.16 (2026-08-06) — interface-method overrides do not fold at the SPIR-V level

Measured while landing the unshaded option: a module overriding
`IHpMaterial.unshaded()` to a constant `true` emits SPIR-V within a dozen
bytes of the fully shaded pipeline (19336 vs 19348), at optimization level
none **and** default — slang specialises to a direct call and a real branch,
and does not inline-and-eliminate. The driver almost certainly folds it, but
that is expectation. **Link-time constants (this ticket's mechanism) are what
makes eliminations provable at the artifact level**, and 142.16's test
captures both byte counts every run, so whichever mechanism lands here will
show up as a diverging pair there.

### Owner decision 2026-08-06 — runtime style switching is a requirement, and it lands here

T0149's styles are **per project, with the game able to change them at
runtime**. That sentence is cheap to say and expensive to honour, and this is
the ticket that pays for it: a style that changes the shading model changes
pipelines, and a pipeline built on demand is a visible stall.

**Both comparison engines had to build machinery for exactly this**, which is
the strongest available evidence that it is not avoidable by being careful:
Godot added ubershaders and pipeline precompilation in 4.4 and a shader baker in
4.5; Unreal has PSO precaching and bundled PSO caches, and "shader compilation
stutter" is its most notorious complaint regardless. **Neither engine lets you
switch a shading model cheaply at runtime.** So every style's pipelines must be
cooked or cached ahead — this ticket, plus T0141.3's cache and T0142.7's
cooking.

### The dynamic-dispatch door should be reopened against this use case, not closed

D30 records Slang's existential dynamic dispatch as **rejected for now** —
verified working to SPIR-V (`-conformance`, an `OpSwitch` over type IDs, one
pipeline for N materials), rejected because opaque members are forbidden in
dyn-conforming structs and the fix (`DescriptorHandle<T>` bindless) needs
descriptor indexing the engine has not adopted.

**Runtime style switching is precisely the use case that would justify paying
that price.** One pipeline with a type-ID switch makes a style change nearly
free — which is the thing Godot and Unreal both built elaborate caching to
*fake*. The trade is a runtime dispatch cost per material against an entire
class of stall disappearing.

This is not a decision to take here, but it should be **measured before this
ticket settles on precompilation as the only answer**, because the two are
alternatives and only one of them has been costed.

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
