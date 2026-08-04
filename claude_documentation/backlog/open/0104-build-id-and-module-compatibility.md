# T0104 — Build id stamping and module compatibility checks

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 2 — Engine skeleton |
| **Order** | 130 |
| **Created** | 2026-08-03 |
| **Blocks** | T0048 |
| **Refs** | [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D12, T0095, T0105, T0053, T0013 |

## Why

D12 lets rich C++ cross the engine/gameplay boundary — real types, templates,
entt registries — instead of a C ABI with a generated binding layer. That is a
large ergonomic win and it rests on one assumption: **the module was compiled
against exactly this engine.**

When the assumption holds, everything works. When it does not, nothing announces
it. A module built against an engine whose class layout has since changed will
load, resolve its symbols, and then read fields at the wrong offsets. The
symptom is corrupted data or a crash somewhere unrelated, arbitrarily far from
the cause, and the usual debugging instinct — read the gameplay code — leads
away from the actual problem.

This ticket is the guard that makes D12 safe. It is deliberately small and
deliberately not optional.

## Done when

- [ ] The engine carries a build id derived from something that actually changes
      when the ABI changes
- [ ] Every gameplay module is stamped with the id of the engine it was built
      against
- [ ] Loading a module whose id does not match the running engine **fails at
      load**, before any of its code runs, with both ids in the message
- [ ] The failure is a clear diagnostic, not an assert or a crash — it names
      what to rebuild
- [ ] A test loads a deliberately mismatched module and asserts the refusal
- [ ] Works on both targets

## Subtasks

- [ ] 104.1 Decide what the id is derived from (see notes — this is the whole
      design)
- [ ] 104.2 Generate it at configure/build time and compile it into the engine
- [ ] 104.3 Stamp it into every gameplay module, via the same mechanism, so a
      module cannot accidentally be built without one
- [ ] 104.4 Ship a **loader-agnostic check function** and test it here; T0048
      wires it into the real loader (see the 2026-08-04 circularity note)
- [ ] 104.5 Test the refusal path with an intentionally stale module — red-green,
      not by observing a pass
- [ ] 104.6 Make the message actionable: which module, which id it wants, which
      id is running, and what to rebuild

## Notes / findings

**What the id derives from is the entire design decision.** Options, roughly in
increasing order of accuracy and cost:

- **A version string bumped by hand.** Free, and worthless — the failure mode
  is forgetting to bump it, which is exactly the case that needs catching.
- **Git commit hash of the engine.** Cheap and automatic, but wrong in both
  directions: a docs-only commit invalidates every module, and an uncommitted
  local header edit invalidates nothing.
- **A hash of the public engine headers.** Closest to "did the ABI change",
  which is the actual question. Costs a build step that hashes the header set,
  and needs care that the set is complete.
- **Compiler identity and flags folded in as well.** D12 assumes one toolchain,
  and the pinned toolchain (D5) makes that near-certain — but a Debug engine and
  a Release module differ in ways a header hash will not catch.

A defensible first answer is the header hash plus build type plus target triple.
Start there and record why in the ticket rather than the code.

**Do not let this become a version negotiation system.** The check is
equal-or-refuse. Ranges, compatibility windows and "minor versions are
compatible" are all how this grows into something that has to be maintained and
reasoned about, and D12's lockstep means there is exactly one right answer at
any moment. If lockstep is ever abandoned, this ticket's design is the wrong
starting point anyway.

**The editor is a module host too** (see the T0032/T0035 amendments). The check
belongs in the shared loader, not in the runtime's copy of it, or the editor
will happily load a stale module and blame the inspector.


### Amendment (2026-08-03) — the profiling flag is part of the ABI

Enabling profiling changes which symbols the engine exports and can change the
layout of anything embedding a zone object. A gameplay module built with
profiling on, loaded by an engine built with it off, is therefore exactly the
mismatch this ticket exists to refuse — and it is a *likely* one, because
profiling is a build-configuration switch a developer flips casually and
independently of whether they rebuilt everything.

So the build id must fold in the profiling setting alongside build type and
target triple (see 104.1). More generally the rule is: **any flag that changes
the engine's symbol surface or type layout belongs in the id.** Profiling is
the first such flag to appear; it will not be the last, so 104.1 should record
the principle rather than just the flag.

## Findings (2026-08-04) — nothing to adopt, and the loader question resolved

### There is no library, and that is the right answer

Surveyed against the pinned toolchain and the offline build. Nothing is worth
adopting: the hard part is *what to hash*, which is inherently project-specific,
and every engine that solves this rolls its own. Unreal is the closest prior
art and validates the shape exactly — a GUID minted per engine build, stamped
into every module, refused at load by `FModuleManager`. Godot's
`compatibility_minimum` and O3DE are **not** applicable: Godot's versioned C
function-pointer table exists to support a compatibility *range* across engine
versions, which is the opposite of D12's lockstep, and O3DE documents no ABI
guard at all.

So the ticket's own estimate stands: this is roughly 150 lines plus a decision
about the hash input.

### The one free candidate fails today — and it depends on T0105

SDL3 is already a submodule, so `SDL_LoadObject` would cost nothing. **It cannot
be used as things stand.** `third_party/SDL/src/loadso/dlopen/SDL_sysloadso.c:47`
hardcodes:

```c
handle = dlopen(sofile, RTLD_NOW | RTLD_LOCAL);
```

and the public API is `SDL_LoadObject(const char *sofile)` — three functions, no
flags parameter anywhere. It **cannot express `RTLD_NODELETE`**, which
`tests/integration/module_boundary_test.cpp:145` currently passes and depends
on. Adopting it today reintroduces the segfault T0105.1 documents.

That flips if T0105 takes its Remedy A (the `__cxa_finalize` fix, which makes
genuine unload safe) or Remedy B (`-Wl,-z,nodelete`, which moves NODELETE into
the module's own ELF where the caller's flags no longer matter). **So this is a
both-ways dependency with T0105 that did not previously exist**: decide the
unload remedy first, and whether the loader can be SDL's or must stay ours
follows from it.

### The id itself

The proposal in the notes above — public-header hash + build type + target
triple + every flag that changes the symbol surface — is right. Two refinements:

- **The public header set is already cleanly scoped**: `engine/include/hp/` is
  the public root, 11 files, with `Api.hpp` carrying the `HP_EXPORT`/`HP_IMPORT`
  split. Glob it. Include-graph closure is not worth it at this size.
- **Hash something closer to the compiler's view than raw bytes.** The Linux
  kernel's `genksyms` expands declarations to fully-resolved form before
  CRC'ing, precisely so comments and reformatting do not thrash the id while
  genuine layout changes always do. Worth borrowing the idea. Do **not** borrow
  its per-symbol CRC table — that exists to support a compatibility range, which
  this ticket explicitly rejects.

### Linker build ids work on both targets — as a diagnostic, not the gate

Measured, since it was worth knowing whether the toolchain gives this for free:

- `zig cc -target x86_64-linux-gnu.2.28 -Wl,--build-id=sha1` → `NT_GNU_BUILD_ID`
  present
- `zig cc -target x86_64-windows-gnu -Wl,--build-id=fast` → a CodeView RSDS
  entry in a `.buildid` section, GUID + age, with no `.pdb` required

Useful to **log alongside** the compatibility id for crash triage — it is exact
binary identity. Useless **as** the gate: it changes on every relink even when
nothing ABI-relevant moved, so it would refuse modules that are perfectly
compatible.

### A trap for whoever writes the loader

Cross-compiling a DLL that links the engine DLL fails with
`lld-link: error: duplicate symbol: atexit` unless boundary symbols use the
`__declspec(dllexport)`/`dllimport` split — without it lld auto-exports
everything, including MinGW's own `atexit`. `engine/include/hp/Api.hpp` already
gets this right, but a gameplay-module template that omits it produces a
confusing **link** error rather than a runtime one.

### The loader circularity, and how it resolves

T0013 states plainly: *"no app calls `dlopen` on it; the module loader is
T0048."* But this ticket **Blocks** T0048, and its 104.4 says to check the id
"in the module loader" — which therefore does not exist yet and belongs to the
ticket this one gates. That is circular as written.

**Resolution: this ticket ships the id, the stamping, and a loader-agnostic
check function** — something with the shape of
`hp::checkModuleBuildId(path_or_handle) -> Result` — plus its refusal test
driven by the existing boundary-suite loading code, which already does
`dlopen`/`LoadLibraryA` on both targets. T0048 then wires that call into the
real loader it builds, and T0105.3 re-verifies it there. 104.4's wording should
be amended accordingly: the check is *available* and *tested* here, and *wired*
in T0048. Without that split the two tickets deadlock.
