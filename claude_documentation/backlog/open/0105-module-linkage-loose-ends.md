# T0105 — Module linkage: the parts that need something built first

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Order** | 155 |
| **Created** | 2026-08-03 |
| **Refs** | [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D12, [../completed/0095-gameplay-module-abi-and-linkage.md](../completed/0095-gameplay-module-abi-and-linkage.md), T0013, T0048, T0104 |

## Why

T0095 decided and proved the engine/gameplay linkage model (D12): the engine is
a shared library, rich C++ crosses the boundary, engine state exists once per
process, and the boundary is guarded by a test suite that runs on both targets.
That decision is what T0013, T0048 and T0062 were waiting for, and it is done.

Four items in that ticket could not be finished, and not because they were
overlooked — **each one needs code that does not exist yet.** Keeping T0095 open
for them would have shown a blocker on the board that was blocking nothing,
while the tickets that could actually resolve them sat behind it.

This ticket holds them until their prerequisites land.

## Done when

- [ ] A module can be genuinely unloaded and reloaded, or the project has
      decided it never will be and the consequences are written down
- [ ] Dev and shipped configurations build from the same source with no
      `#ifdef` spread through gameplay code
- [ ] T0048's reload mechanics are re-verified against the shared-engine model:
      the *game* module is copied-before-load and swapped; the *engine* library
      is loaded once and never reloaded
- [ ] `dist` staging carries the engine shared library correctly on both
      platforms, and an exported build runs from a directory it was not built in

## Subtasks

- [ ] 105.1 **The unload problem** (was 95.3's second half). `dlclose` of a
      library holding any static needing destruction segfaults the process at
      exit under this toolchain — reduced during T0095 to a library containing
      one `static std::string` and no engine code at all. Modules are currently
      loaded `RTLD_NODELETE` and stay resident. Options, none yet evaluated:
      link libc++ dynamically so one copy is shared; forbid statics requiring
      destruction in modules as a convention; or accept NODELETE permanently and
      rely on copy-before-load. Needs T0048 to exist to be worth solving
- [ ] 105.2 Dev vs shipped configuration. Entangled with T0104: the profiling
      flag alone changes the engine's symbol surface, so "same source, different
      configuration" and "refuse a mismatched module" are the same problem seen
      from two sides
- [ ] 105.3 Re-verify T0048's mechanics (was 95.6). Blocked on T0048
- [ ] 105.4 `dist`/export staging of the engine shared library (was 95.7).
      Blocked on T0013 producing one; note `cmake/dist.cmake` already handles
      shared libraries for the Windows target, and T0043 records a RUNPATH
      problem that this will meet
- [ ] 105.5 Extend `tests/integration/module_boundary_test.cpp` as each of the
      above becomes possible. The suite exists and is the right home; it
      currently proves two generations agree, not that one can be retired

## Notes / findings

**Read T0095's prototype results before starting any of this.** They record
what was measured rather than assumed, including two things that are easy to get
backwards: `entt::type_index` is a per-module number and setting
`ENTT_API_EXPORT`/`IMPORT` makes it *worse*, and RTTI across the boundary works
on both targets provided boundary types are default-visibility.

**105.1 is the one with teeth.** The others are staging and configuration work
that will be obvious once their prerequisites exist. Whether a module can truly
be unloaded decides more than hot reload: Tracy retains pointers to
source-location strings in module memory (see T0029's amendment), so profiling a
module that later unloads is the same problem wearing a different hat. A
long-running editor that reloads many times accumulates module images under the
current NODELETE approach, and nobody has measured how much that costs.

**Do not let this ticket become a dumping ground.** It exists because four
specific items had unmet prerequisites. New linkage questions belong in their own
tickets; this one closes when these four do.
