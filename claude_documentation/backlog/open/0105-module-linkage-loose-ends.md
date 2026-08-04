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

- [ ] 105.1 **The unload problem** — root-caused and fixed 2026-08-04, see below; what remains is *choosing* remedy A or B and proving A against a real module. (was 95.3's second half). `dlclose` of a
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

## 105.1 root-caused and fixed (2026-08-04) — measured, both targets

The unload problem is no longer three unevaluated options. It has a root cause,
two working remedies that are **not interchangeable**, and one option that turns
out not to exist.

### The cause is a zig bug, not a C++ constraint

`zig cc -shared` does not link `crtbeginS.o`/`crtendS.o`, so the produced `.so`
has **no `.fini_array` at all** — yet it imports `__cxa_atexit` and defines
`__dso_handle`. It registers destructors with nothing to retire them, and on
`dlclose` those registrations survive into unmapped memory. Confirmed here: the
identical source built with the host `g++` **has** a populated `.fini_array` and
unloads cleanly. Same machine, same source, only the driver differs.

Upstream: [ziglang/zig#17908](https://github.com/ziglang/zig/issues/17908), open
since Nov 2023 with no PR. The workaround suggested there — borrow the host
GCC's `crtbeginS.o` — **breaks hermeticity (D5), and zig ships no Linux
crtbegin** at all (only BSD copies). So it is not available to us.

### Remedy A — source-only, and it gives *genuine* unload

Four lines in a header every gameplay module includes:

```cpp
extern "C" void* __dso_handle;
extern "C" int __cxa_finalize(void*);
__attribute__((destructor)) static void hp_module_finalize() { __cxa_finalize(&__dso_handle); }
```

Controlled A/B, same module source, one `-D` apart, host **not** passing
`RTLD_NODELETE`:

```
nofix  fini_array=0   N=1 exit=139   N=5 exit=139   N=50 exit=139   (SIGSEGV)
fix    fini_array=1   N=1 exit=0     N=5 exit=0     N=50 exit=0
```

Fully hermetic, no host objects, holds over 50 load/unload cycles.

### Remedy B — link-flag, and it does *not* give unload

`-Wl,-z,nodelete` on the module's link line sets `DF_1_NODELETE` in the ELF, so
the loader refuses to unmap regardless of what the *caller* passed. Measured:
`FLAGS_1: NOW NODELETE`, exit 0 at 50 cycles with a loader that passes only
`RTLD_NOW|RTLD_LOCAL`.

**These are alternatives, not belt-and-braces.** Remedy B works precisely *by
never unloading* — it is today's `RTLD_NODELETE` behaviour moved into the build
where CMake can enforce it centrally, so a module cannot be built without it.
Only **Remedy A** delivers this ticket's first Done-when, "a module can be
genuinely unloaded and reloaded". Applying both yields safety with no unload,
which is a coherent choice but must be a chosen one.

### The defect is Linux-only

Windows was never affected. The same construct through
`LoadLibrary`/`FreeLibrary`, 25 cycles: **exit 0**. The subtask above says "under
this toolchain" without distinguishing targets, which reads as both. It is not.
(Measured under WSL interop; worth re-confirming on the native Windows CI job,
which is what `tests-windows-host` exists for.)

### One option removed

105.1 offers "link libc++ dynamically so one copy is shared". **Zig ships no
shared libc++** — source only, static linking is the only supported path. A
trivial `.so` here is 5.9 MB with 13 locally-defined `_ZNSt*` symbols and zero
undefined ones, i.e. every artifact carries its own C++ runtime. That option is
closed; the choice is between A and B.

### What this leaves open

- **Choosing A or B** — a real decision, not a formality. A enables reload
  without accumulating module images in a long-running editor (the cost nobody
  has measured, per the notes above); B is simpler and cannot be forgotten.
- **Remedy A has not been tried against a module carrying the *engine's*
  statics**, only a synthetic `static std::string`. That is the real case and it
  is the one that matters. An upstream reporter saw a residual crash with a
  variant of this approach, so treat A as strong evidence rather than proof
  until it is exercised against a real module.
- **The A/B belongs in `tests/integration/module_boundary_test.cpp` as a
  regression test** (105.5). Nothing currently catches this fix being silently
  removed, and it is the kind of change a future toolchain bump could undo
  invisibly.
