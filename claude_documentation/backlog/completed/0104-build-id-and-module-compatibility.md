# T0104 — Build id stamping and module compatibility checks

| | |
|---|---|
| **Status** | ✅ DONE |
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

- [x] The engine carries a build id derived from something that actually changes
      when the ABI changes
- [x] Every gameplay module is stamped with the id of the engine it was built
      against
- [x] Loading a module whose id does not match the running engine **fails at
      load**, before any of its code runs, with both ids in the message
- [x] The failure is a clear diagnostic, not an assert or a crash — it names
      what to rebuild
- [x] A test loads a deliberately mismatched module and asserts the refusal
- [x] Works on both targets

## Subtasks

- [x] 104.1 Decide what the id is derived from (see notes — this is the whole
      design)
- [x] 104.2 Generate it at configure/build time and compile it into the engine
- [x] 104.3 Stamp it into every gameplay module, via the same mechanism, so a
      module cannot accidentally be built without one
- [x] 104.4 Ship a **loader-agnostic check function** and test it here; T0048
      wires it into the real loader (see the 2026-08-04 circularity note)
- [x] 104.5 Test the refusal path with an intentionally stale module — red-green,
      not by observing a pass
- [x] 104.6 Make the message actionable: which module, which id it wants, which
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

## Done (2026-08-04)

### 104.1 — what the id derives from

SHA256 over the **contents** of every `engine/include/hp/*.hpp`, sorted, plus the
target triple, the build type, and `HP_PROFILING`. Truncated to 16 hex
characters, which is ample for equal-or-refuse and short enough to read in an
error message.

Contents rather than paths or timestamps, so touching a header without changing
it does not invalidate every module in the tree. Sorted, because glob order is
filesystem order and the id must not be.

**The rule, which outlives the list: any flag that changes the engine's symbol
surface or type layout belongs in the id.** Profiling is the first — it alters
exported symbols and the layout of anything embedding a zone object, and it is a
switch flipped casually without rebuilding everything. It will not be the last;
the generator says so at the point where they get added.

### 104.2 — generated at build time, which is the part that matters

`cmake/hp_build_id.cmake`, run by a custom command whose `DEPENDS` are the public
headers, producing `${CMAKE_BINARY_DIR}/generated/hp/BuildId.h`.

**Build time, not configure time, deliberately.** CMake re-runs configure when a
`CMakeLists.txt` changes, *not* when a header does — so a configure-time hash
would keep asserting compatibility across exactly the header edits this ticket
exists to catch. That is the same class of bug T0123 had from the other side,
and the same fix: make the generator a real node in the build graph. The
generated file is written only when its content changes, so an unchanged id does
not trigger a rebuild of everything that includes it.

Verified, rather than assumed:

```
linux:   HP_BUILD_ID "a5cdd34456544b4c"
windows: HP_BUILD_ID "b6213a1755da3043"      <- differs by target, as it must

before edit   "a5cdd34456544b4c"
after editing engine/include/hp/Guid.hpp
              "7fd0f74442881a3c"             <- a header change moves it
after revert  "a5cdd34456544b4c"             <- and it is deterministic
```

The header change was picked up by an ordinary `zig build`, with no reconfigure —
which is the property being claimed.

### 104.3 — stamped by the build, not by the author

`engine/module/ModuleBuildId.cpp` exports `hp_module_build_id()` returning the id
baked at *that module's* compile time, and `hp_add_gameplay_module()` compiles it
into every module alongside T0105.1's unload finalizer. **A module cannot be
built without a stamp**, which is what the subtask asked for. Confirmed in the
real artifact: `nm -D libhp_sandbox.so` shows `T hp_module_build_id`.

Nothing needs to *detect* a stale module. Rebuild the headers and the engine's id
moves while a module compiled earlier still carries the old one; the mismatch is
the detection.

### 104.4 — loader-agnostic, which resolves the T0048 circularity

T0013 states the module loader is T0048's, but this ticket **Blocks** T0048 — so
"check it in the module loader" had nowhere to land. Resolved by shipping the
check independent of *how* a module was loaded:

- `hp::engineBuildId()` — the running engine's id
- `hp::kModuleBuildIdSymbol` — the symbol name, declared once so the engine and
  whatever loads modules cannot disagree about it
- `hp::checkModuleBuildId(const char*)` — equal-or-refuse, taking the value the
  loader resolved
- `hp::describeIncompatibility(...)` — the developer-facing text

The loader resolves one symbol and calls one function. T0048 wires that into the
real loader; T0105.3 re-verifies it there. Deliberately **equal-or-refuse**: no
ranges, no compatibility window, because D12's lockstep means there is exactly
one right answer at any moment.

An unstamped module (`nullptr`) is **refused**, not assumed fine — it is the case
with no evidence either way.

### 104.5 — red/green, not observed

`tests/fixtures/stale_module.cpp` is a well-formed module exporting a
plausible-looking id (`dead0000beef0000`) that belongs to no build of this
engine, built *around* `hp_add_gameplay_module()` because the helper's purpose is
to make staleness impossible. It also records whether its entry point ran, so the
suite can assert the refusal happens **before any module code executes** —
refusing afterwards would be a diagnostic, not a guard.

Proven to fail when the check is broken, rather than trusted because it passes.
Mutating `checkModuleBuildId` to always accept:

```
ERROR: CHECK_FALSE( result.compatible ) is NOT correct!      (stale module)
ERROR: CHECK_FALSE( result.compatible ) is NOT correct!      (unstamped)
ERROR: CHECK( message.find("dead0000beef0000") ... )         (message content)
[doctest] test cases: 38 | 35 passed | 3 failed
```

Restored, and green again.

### 104.6 / both targets

The refusal names the module, both ids, the target/config/profiling triplet, and
what to do — asserted by a test rather than left to reviewer goodwill.

```
Build Summary: 18/18 steps succeeded; 21/21 tests passed
+- test (linux-x86_64, integration)   natively                              success
+- test (windows-x86_64, integration) as a real Windows process via interop success
[doctest] test cases: 38 | 38 passed | 0 failed    (x2, both targets)
```

## What is not verified

**No loader calls this yet**, because none exists — that is T0048, which this
ticket gates. The check is available, exported and tested; it is not yet *wired*.
Until it is, a stale module in a real run is still loaded by nothing, so the
guarantee is "the mechanism works" rather than "the engine is protected".
T0105.3 is the re-verification point.

**The id does not cover the engine's own `.cpp` files, deliberately** — only
public headers, since layout is what breaks a module. An engine rebuilt with a
changed implementation but identical headers keeps its id, which is correct for
ABI purposes and worth knowing if it ever surprises someone debugging.

**`HP_PROFILING` and build type change the id — verified at the generator, not
end to end.** Running `cmake -P cmake/hp_build_id.cmake` directly with each
input:

```
profiling=OFF -> "a5cdd34456544b4c"      config=Release -> "a5cdd34456544b4c"
profiling=ON  -> "25c6a336bed8657f"      config=Debug   -> "c90e70d5ce9e10d8"
```

So the inputs are live rather than decorative. What has **not** been run is a
full engine-plus-module build with `-DHP_PROFILING=ON` actually refusing a
profiling-OFF module, because the build has no profiling configuration yet
(T0029). The mechanism is proven; the end-to-end scenario for that particular
flag is not.
