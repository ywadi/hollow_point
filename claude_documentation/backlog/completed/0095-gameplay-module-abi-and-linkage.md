# T0095 — Gameplay module ABI: engine linkage, one engine state, entt across the boundary

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 2 — Engine skeleton |
| **Created** | 2026-08-03 |
| **Blocks** | T0048, T0062 |
| **Refs** | T0013, T0055, T0056, T0076, T0094 |

## Why

T0013 makes the engine a **static** library. T0048 makes gameplay a **shared**
library loaded by the editor and the runtime. Nobody has decided how the
gameplay module links against the engine, and the default answer — statically
link the engine into the module too — is silently catastrophic: **two copies of
every engine global**, one in the executable and one in the game DLL. The
logger, the autoload registry, the asset pool's statics, and entt's type-index
counters all exist twice, and each side sees only its own.

The entt case is the sharpest. Verified against the vendored copy
(`third_party/entt/src/entt/core/type_info.hpp`): type identity is *partly*
name-hash based (stable across modules on one compiler) but `type_info::index()`
comes from `internal::type_index::next()` — a **static sequential counter**,
marked `ENTT_API` precisely because it must be a single instance across
boundaries. With the engine linked into both sides, the exe's registry and the
module's component registrations can disagree about type indices. The failure
is not a crash at the boundary; it is components quietly landing in the wrong
pools, which presents as impossible bugs in gameplay code.

The platform asymmetry makes this a real design decision rather than a flag:

- **Linux:** a `dlopen`'d module can resolve engine symbols from the executable
  if the exe is linked `--export-dynamic`. Cheap, conventional.
- **Windows:** PE has no such mechanism. A DLL imports from a *named* module —
  so either the engine becomes `engine.dll` that both the exe and the game
  module import, or the exe itself exports engine symbols and the module links
  against the exe's import library (possible under MinGW, unusual, fragile).

If this is discovered *after* T0048/T0062 are built, the fix is relinking the
world — exactly the "hacking the engine" outcome this backlog exists to avoid.

## Done when

- [x] The linkage model is decided and recorded in the decision log: engine
      static vs shared, per configuration (dev with hot reload vs shipped
      static, per T0048.7), and how the module resolves engine symbols on
      **both** platforms — **D12**
- [x] Exactly one instance of every engine global exists at runtime — proven by
      a test (e.g. exe and module both log the address of the same engine
      static and they match) — `tests/integration/module_boundary_test.cpp`,
      run by `zig build test` on both targets
- [x] entt type identity holds across the boundary: a component type used from
      the module is visible and iterable from engine code and vice versa, on
      both targets — tested both directions, both targets
- [x] `dynamic_cast`/`typeid` on types crossing the boundary either works
      (typeinfo symbols unified) or is banned by convention (T0055) — decided,
      not left to chance. **It works, on both targets, measured** — so it is
      allowed rather than banned, conditional on boundary types being marked
      default-visibility
- [ ] Dev and shipped configurations build from the same source with no
      `#ifdef` spread through gameplay code — **moved to T0105**, needs a build
      system and T0104 to exist
- [x] The rules are written into the conventions doc (T0055) — see its module boundary section

## Subtasks

- [x] 95.1 Decide: engine as shared library in dev builds (one copy of state,
      conventional on both platforms) vs exe-exports (Linux-easy,
      Windows-fragile). Prototype the Windows side under the zig/MinGW
      toolchain **before** committing — this project's history (G2, G3, G4) is
      that Windows linking surprises are real
- [x] 95.2 Configure entt for cross-boundary use — **re-scoped and resolved as
      "deliberately do not set them"**. Setting the macros makes indices
      disagree rather than unifying them; `type_hash` was confirmed identical
      across modules on both targets. The reasoning is in the prototype results
      below and the resulting rule is in the conventions doc
- [ ] 95.3 Boundary test: two component types (one engine-defined, one
      module-defined), created on one side, queried on the other, surviving a
      hot reload — **still not fully done**: both component types are tested
      both directions on both targets, and two independently loaded generations
      are proven to agree, but a *genuine unload* is impossible under this
      toolchain today (see the dlclose finding). Reload is proven only in the
      copy-before-load shape T0048 will actually use
- [x] 95.4 Symbol visibility policy for shared types — confirmed by
      measurement and written into the conventions doc: boundary types marked
      default-visibility, `-fvisibility=hidden` everywhere else, which is what
      the fixtures build with and what makes RTTI work across the boundary
- [x] 95.5 Exception and allocation rules at the boundary, folded into T0055:
      our code does not throw, Diligent's exceptions are converted at the call
      site and never reach a module boundary, and memory is released by the
      side that allocated it because each library carries its own statically
      linked libc++
- [ ] 95.6 Re-verify T0048's mechanics against the chosen model — **moved to
      T0105**, blocked on T0048 existing
- [ ] 95.7 Check `dist`/export staging carries the engine shared library
      correctly on both platforms — **moved to T0105**, blocked on T0013
      producing an engine shared library to stage

## Prototype results (2026-08-03) — 95.1/95.2/95.3 fact-finding

A standalone prototype under the pinned zig toolchain: `hpengine` (shared),
`gamemod` (shared, links the engine), and a driver that `dlopen`/`LoadLibrary`s
the module and compares both sides. Built for **both** targets, run natively on
Linux and under wine for Windows.

### The engine-as-shared-library model works on both platforms

```
Linux    engine global has one address     PASS  exe=0x7bdd1e03e680 module=0x7bdd1e03e680
Windows  engine global has one address     PASS  exe=00006FFFFAF5F9E0 module=00006FFFFAF5F9E0
```

Mutating the global through the engine and reading it back through the module
gives the mutated value on both. **Windows did not surprise us**, which is
worth stating given G2/G3/G4: `zig c++ -target x86_64-windows-gnu -shared`
produced `hpengine.dll`, and linking `gamemod.dll` and `driver.exe` against it
by naming the DLL directly worked with no import library, no `.def` file and no
export list. The "exe-exports" alternative in 95.1 does not need to be
prototyped — the conventional option is available on both platforms, so the
fragile one can simply be dropped.

### The ticket's sharpest claim is wrong, and the fix it proposes backfires

This ticket says `type_index`'s sequential counter "must be a single instance
across boundaries" or "components quietly land in the wrong pools". Against the
vendored entt 3.16.0 that is not how identity works:

- `basic_registry` keys **every** pool on `type_hash<Type>::value()`, the
  name-based hash — `registry.hpp:248,280,455,466,1022`, all defaulting to
  `type_hash<Type>::value()`. The sequential index is never a pool key.
- `type_info::operator==` compares **`hash()` only** (`type_info.hpp:188`).
  `seq` is carried in the struct and ignored by equality, so the debug asserts
  at `registry.hpp:258,288` (`it->second->info() == type_id<Type>()`) do not
  fire on an index mismatch either.

Measured, both targets: `type_hash` for the same type agrees across the
boundary (`48949654` on both sides, both platforms), and a component emplaced
by the engine is visible to a `view<>` in the module.

Worse, **setting `ENTT_API_EXPORT`/`ENTT_API_IMPORT` as 95.2 proposes makes the
indices disagree** rather than unifying them:

```
no ENTT_API      engine=0  module=0   (and module's own type = 1)
ENTT_API set     engine=0  module=1   (and module's own type = 2)
```

The macros do make `internal::type_index::next()` a single counter, but
`type_index<T>::value()` memoises into a function-local static that is still
**per-module**. One shared counter handing out numbers to two memoisation sites
gives the same type two different indices — the opposite of the intent. Without
the macros each module runs its own counter and the first type registered in
each gets `0`.

**Caveat on that last row, stated because the test cannot distinguish it:** the
`no ENTT_API` pass is a *coincidence*, not a proof of sharing — each module's
counter independently starts at zero, so the first type on each side gets `0`
either way. The prototype cannot tell "one shared counter" from "two counters
that happen to agree", and no conclusion should be drawn from it beyond "index
values are not a cross-boundary identity". `type_hash` is the identity, on the
evidence above.

### What this changes

- **95.2 as written should not be done.** Do not define `ENTT_API_EXPORT` /
  `ENTT_API_IMPORT`. Leaving `ENTT_API` empty is both the default and the
  better behaviour here. The subtask becomes: prove `type_hash` stability and
  write down *why* the macros are deliberately not set, so the next person
  reading entt's docs does not "fix" it.
- **A convention for T0055 falls out of this:** `entt::type_index` /
  `type_info::index()` must never be used as a persistent identifier, a
  serialised value, or any cross-module key. It is a per-module runtime number.
  This binds T0053 (reflection registry) and T0074 (gameplay tags), both of
  which will be tempted by a dense integer id.
- The genuine hazard the ticket identified — **one instance of engine state** —
  is real and is solved by the engine being a shared library, which is now
  demonstrated on both platforms rather than assumed.

### The decision is recorded; the ticket is not finished

D12 (engine linkage, lockstep, build id) and D14 (no scripting, name-keyed
behaviours) are in the decision log, and the amendments they imply have been
made to T0013, T0023, T0032, T0035, T0043, T0048, T0053 and T0062. T0104 was
created to own the build-id guard that makes D12 safe, and T0103 to own the
virtual filesystem.

**The single most valuable thing left is turning the prototype into a real
test.** It currently lives in a scratchpad directory and proves the boundary
holds on both targets; nothing in `zig build test` would notice if a future
change broke it. Two Done-when conditions above are blocked on exactly that, and
it is also the natural home for the hot-reload half of 95.3.

### The prototype is now a test — and it found a defect the prototype could not

`tests/integration/module_boundary_test.cpp` plus two fixture shared libraries
(`tests/fixtures/abi_engine.cpp`, `abi_module.cpp`) now run in `zig build test`
on both targets: 6 cases, 31 assertions, Linux natively and Windows under wine.
The fixtures are a miniature of the real layout — a *shared* engine that both
the test executable and a loadable module link.

**`dlclose` segfaults the process at exit, and it is a toolchain property, not
an engine one.** Reduced to a shared library containing one `static
std::string` and no engine code whatsoever:

```
before dlopen
after dlopen: 0x1ddee300 (ok)
before dlclose
after dlclose: 0
returning from main
Segmentation fault (core dumped)     <- after main returned
```

Zig links libc++ statically into every shared library, so each module carries
its own copy. A static object needing destruction registers with
`__cxa_atexit`; `dlclose` unmaps the code without retiring the registration,
and the process dies at exit against unmapped memory — arbitrarily far from the
unload, with nothing pointing at the cause. `dlopen` *without* `dlclose` is
clean, which is exactly why the earlier prototype missed it: it never unloaded.

`RTLD_NODELETE` avoids it by keeping the mapping alive, and the test loader uses
it. **That means the suite does not prove a module can be unloaded — it proves
two independently loaded generations agree.** The reload case was rewritten to
match what T0048 actually does (copy the module to a fresh path, load the copy,
leave the previous image resident) rather than to claim an unload that does not
work.

**This is T0048's problem now and it is not solved.** Options not yet
evaluated: linking libc++ dynamically so one copy is shared, forbidding statics
requiring destruction in modules as a convention (T0055), or accepting
NODELETE and relying on copy-before-load permanently. Whichever is chosen, a
long-running editor that reloads many times accumulates module images.

Also found, and cheap to get wrong: MinGW keeps the `lib` prefix on shared
libraries, so the module is `libhp_abi_module.dll` on Windows. Hardcoding
`hp_abi_module.dll` compiled cleanly and failed every Windows test at run time;
the name now comes from CMake via `$<TARGET_FILE_NAME:>`.

### Still open in this ticket

**95.3 is only half done.** Two component types created on one side and queried
from the other is proven, on both targets. The rest of that subtask — that it
still holds *across a hot reload* — was not tested: the prototype loads the
module once and never unloads it. That half matters more than it looks, because
reload is when a module's memoised `type_index` statics are re-initialised
against a counter that has already advanced, and it is the case T0048 depends
on. It needs the dlclose/FreeLibrary cycle before it can be ticked.

95.4 (visibility policy) is untested: the prototype used
`-fvisibility=hidden` on Linux with explicit default-visibility exports and it
worked, but vtables/typeinfo for types crossing the boundary were not
exercised. 95.5 (exceptions/allocation at the boundary), 95.6 (re-verifying
T0048's copy-before-load against this model) and 95.7 (`dist` staging the
engine shared library) are untouched. No decision has been recorded in the
decision log yet — that is the first Done-when and needs sign-off on "engine is
a shared library", not just evidence that it can be.

## Notes / findings

- **Why this is Phase 2:** T0013 (the split) and T0048 (the module) are both
  Phase 2, and this decision changes what both of them build. Doing T0013 with
  "engine = static lib" as an unexamined default is the trap.
- entt arrives via DiligentFX at Diligent's pinned ref (D7) — the
  `ENTT_API` configuration has to be injected as compile definitions from our
  root `CMakeLists.txt`, never by patching the fetched copy. Confirm the
  definitions reach every target that includes an entt header, including the
  gameplay module, or the setup is worse than none.
- The same one-instance argument applies to anything with static state that
  both sides touch: the reflection registry (T0053), the autoload registry
  (T0076), the log sinks (T0054). Designing those as instance objects handed
  across the boundary (context pointer), rather than as globals reached by
  symbol, sidesteps most of the problem and is worth adopting as the
  convention regardless of the linkage choice.
- T0056's "use Diligent math types in gameplay" is compatible with all of
  this — header-only value types have no linkage. The hazard is *stateful*
  engine code and *RHI interface pointers* (T0094's concern), not PODs.
- If the engine becomes a shared library, symbol export lists / visibility
  macros (`HP_API`) need deciding at T0013 time — retrofitting export macros
  onto hundreds of headers is exactly the kind of mechanical churn worth
  avoiding by deciding now.


## Closing note

**Closed on what it existed to do: decide the linkage model and prove it.** D12
is recorded, the boundary is proven by a suite that runs on both targets in
`zig build test`, and the conventions that fall out are written into T0055. That
is what T0013, T0048 and T0062 were waiting for, and they are no longer blocked.

**Four items are moved to [T0105](../inprogress/0105-module-linkage-loose-ends.md)
rather than ticked**, and the distinction matters: none was overlooked, and each
needs code that does not exist yet.

| Item | Waits on |
|---|---|
| Genuine module unload (95.3's second half) | T0048 |
| Dev vs shipped configuration | T0104, and a build system |
| Re-verify T0048's reload mechanics (95.6) | T0048 |
| `dist` staging of the engine shared library (95.7) | T0013 |

Leaving this ticket open for them would have shown a blocker on the board that
was blocking nothing, while the tickets that could actually resolve them queued
behind it.

### What this ticket proved, and where the evidence lives

- **The engine-as-shared-library model works on both targets.** One address for
  an engine global seen from the executable and from a loaded module; Windows
  needed no import library, no `.def` and no export list. The exe-exports
  alternative was dropped rather than prototyped as a result.
- **The ticket's own sharpest claim was wrong.** entt keys component pools on
  `type_hash` (name-based) and `type_info::operator==` compares `hash()` only,
  so a `type_index` mismatch is inert — and setting `ENTT_API_EXPORT`/`IMPORT`
  as 95.2 proposed makes indices disagree rather than unifying them.
- **RTTI across the boundary works** on both targets, so it is allowed rather
  than banned — conditional on boundary types being default-visibility.
- **`dlclose` segfaults the process at exit**, reduced to a shared library
  containing one `static std::string`. Found only because the prototype became
  a test that unloads; the prototype never did.

Living evidence: `tests/integration/module_boundary_test.cpp` and
`tests/fixtures/abi_*` — 7 cases, 38 assertions, green on Linux natively and on
Windows under wine, in CI on both hosts.
