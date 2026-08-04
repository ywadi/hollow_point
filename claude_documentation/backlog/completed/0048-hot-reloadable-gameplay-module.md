# T0048 — Hot-reloadable gameplay module

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Very Complex |
| **Phase** | 2 — Engine skeleton |
| **Order** | 150 |
| **Created** | 2026-08-03 |
| **Refs** | T0100, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md), T0104 (Blocks this), T0105, [../completed/0127-exceptions-across-the-module-boundary.md](../completed/0127-exceptions-across-the-module-boundary.md) |

## Closed 2026-08-04 — on what it achieved, with 48.7 moved to T0043

**The loader is built, hot reload works, and the central usability guarantee is
now proven** rather than assumed. 48.5 was the last real blocker and T0021
cleared it by providing an engine-owned registry; the test is
`tests/integration/module_host_test.cpp`, "component data outlives a module
reload", and it runs against the real sandbox module on both targets.

It asserts the rule this whole design rests on: a component is put on an entity
in an engine-owned `Scene`, mutated *after* the module is live, the module is
reloaded, and the value, the tag, the transform, the GUID and the parent link are
all unchanged. `totalLoads() == 2` is checked first — without that, every
assertion after it would also pass if the reload had silently no-opped, which is
the failure this suite exists to catch.

**48.7 is moved to T0043**, per CLAUDE.md's rule about tickets blocked on
something far away. Linking the module statically into a shipped runtime needs an
export pipeline to link into, and that is Phase 8. Leaving a Phase 2 ticket open
for months against it helps nobody; T0043 now carries the requirement and the
reasoning.

## Why

Gameplay stays in **C++** — full performance, one language, no binding layer. The
cost C++ normally carries is iteration speed: every tweak means a rebuild and an
editor restart.

A hot-reloadable gameplay module removes that cost while keeping C++. Gameplay
lives in its own shared library that the editor (and runtime) load at startup and
can reload on change, without losing the open scene or restarting.

This is the decision *instead of* embedding a scripting language.

## Done when

- [x] Gameplay builds as a shared library, loaded at runtime by editor and runtime — **corrected 2026-08-04**: this was ticked while only the *test suite* loaded a module. Both apps now do, in the build tree and in an export, on both targets
- [x] Editing gameplay code and rebuilding reloads it live — no editor restart — demonstrated end to end, evidence below
- [x] The open scene, entities and component data survive a reload intact — **done 2026-08-04**, once T0021 provided an engine-owned registry. `tests/integration/module_host_test.cpp`, "component data outlives a module reload"
- [x] Reload works on Linux and Windows
- [x] A reload that fails to compile leaves the previous module running
- [x] Reload time is fast enough to be worth it — measured: **1.76 ms** per swap

## Subtasks

- [x] 48.1 A gameplay module declares its lifecycle through the engine (`HP_GAMEPLAY_MODULE`). **Not a C ABI** — D12 settled that after this line was written; one `extern "C"` symbol exists solely because `dlsym` takes a string
- [x] 48.2 Loader: `dlopen`/`dlsym` on Linux, `LoadLibrary`/`GetProcAddress` on
      Windows, behind one interface — `hp::ModuleHost`
- [x] 48.3 Copy before loading — done on **both** targets, and the Linux reason
      turned out to be different and worse than the Windows one. See below
- [x] 48.4 Watch and reload on change, debounced — a stat() poll at frame phase
      12, 1.5 µs per module per frame. Polling *is* the debounce; see below
- [x] 48.5 Verify state survives: mutate a component, reload, confirm intact — **done 2026-08-04** against the real sandbox module
- [x] 48.6 Handle load failure gracefully, keeping the old module
- [~] 48.7 Ensure the exported *runtime* can link it statically instead — **moved to T0043**, which owns the export pipeline this needs to link into. See the closing note

## Notes / findings


### Frame anatomy — phase 12 — the end-of-frame safe point (T0100, D17)

"Between frames" is **phase 12**, the end-of-frame safe point, shared with T0058
and T0077. The reload must assert the phase-6 queues are drained before acting:
a reload while a queue still holds a module-typed payload is a use-after-free in
a type that no longer exists.

The full order is in [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md); the decision and what it rejected is **D17** in the
decision log. If this ticket needs a phase that does not exist, that is a change
to T0100's document and to D17 — not a new call bolted into `Application::run`.

**The rule that makes this work: all persistent state lives in the ECS, owned by
the engine — never in the gameplay module.** The module contains behaviour, not
data. Globals, statics and singletons inside it are destroyed on reload, so any
state kept there is lost. This is the single design constraint that decides
whether hot reload is reliable or a source of baffling bugs.

Consequences that follow from that rule:
- gameplay components must be plain data, held in the registry
- no `std::function` or vtable pointers into the module stored across a reload —
  they dangle the moment it is unloaded
- systems are functions the engine calls, not objects the module owns

**Debugging across reload** is the known rough edge: breakpoints can detach when
the module unloads. Worth checking early how bad it is in practice, since it
partly determines whether this is pleasant enough to actually use.

For the shipped game, prefer linking the module statically (48.7) — hot reload is
a development affordance and adds startup cost and attack surface otherwise.


### Architecture decision (2026-08-03) — linkage settled, and it is modules plural (D12)

T0095 resolved what this ticket was blocked on. The engine is a **shared**
library; the gameplay module links it and so do the editor and runtime, so
engine state exists once per process. Measured on both targets. Rich C++ crosses
the boundary — no C ABI and no binding layer — because engine and gameplay are
always built together.

Three amendments:

- **A build-id check is mandatory, not a nicety.** T0104 owns it and blocks this
  ticket. Without it a stale module loads, resolves symbols, and reads fields at
  wrong offsets — silent corruption arbitrarily far from the cause. With it, a
  refusal at load naming both ids. The check must run *before* any module entry
  point is called.
- **"The module" is really "a set of modules".** Designing the loader for one
  and generalising later is the expensive order. Even without code-bearing DLC
  (ruled out by D14), the editor and runtime both host modules and a project may
  reasonably split gameplay across more than one.
- **The engine shared library is loaded once and never reloaded.** Only gameplay
  modules are copied-before-load and swapped. 95.6 exists to re-verify this
  ticket's mechanics against that split and is still open.

The entt hazard this ticket inherited from T0095 turned out to be misdescribed —
see that ticket's prototype results. Component identity is name-hash based and
survives the boundary; the sequential `type_index` does not and must never be
persisted or compared across modules.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0105** holds the linkage loose ends this loader inherits, and 105.3
  re-verifies this ticket's mechanics against the shared-engine model. Read
  T0095's prototype results and T0105 before designing 48.2.

  **Corrected 2026-08-04.** This note used to say that 105.1 "forces
  `RTLD_NODELETE`" and that "a loader designed around true unload will fight the
  toolchain". Both were true when written and are false now. 105.1 was
  root-caused — it is [ziglang/zig#17908](https://github.com/ziglang/zig/issues/17908),
  not a C++ constraint — and fixed: `engine/module/ModuleFinalize.cpp` makes the
  linker emit `.fini_array`, and `hp_add_gameplay_module()` compiles it into
  every module so one cannot be built without it. **Genuine unload works, on
  both targets, over 50 cycles**, and there is a red/green regression test.
  Design 48.2 around real unload. Doing otherwise would also make `entt::meta`'s
  mandatory `meta_reset` on unload (T0053) a ritual guarding registrations that
  never actually dangle.

  Still true and still inherited: T0029's Tracy pointers into module memory ride
  on the same unload question, and Remedy A has not been proven against a module
  carrying the engine's own statics — which is what this ticket finally provides.


### Iteration speed: use a PCH, not a JIT (2026-08-04, D19)

Runtime C++ compilation was evaluated as a faster inner loop and rejected — see
**D19** for the reasoning and the measurements.

The finding that matters for this ticket is where the time actually goes.
Measured on `samples/sandbox/src/Sandbox.cpp` with the pinned toolchain:

| | |
|---|---|
| front end only (`-fsyntax-only`) | **1.4 s** |
| `-O0` vs `-O3` difference | ~0.2 s |
| link the module | 0.05 s |
| `dlopen` it | 0.5 ms |

**The front end is the cost.** Codegen, linking and loading together are a small
fraction of it, which is why a JIT — paying the same parse — had nothing to win.

A precompiled header does have something to win: a 19 MB PCH of the engine's
public headers plus entt takes the same TU from **1.4 s to 0.34 s**. That is the
single largest available improvement to this ticket's inner loop, it needs no new
dependency, and it works on both targets. **48.x should adopt it**, and the
gameplay-module CMake function (`cmake/HpGameplayModule.cmake`) is where it
belongs, for the same reason the unload finalizer and build-id stamp live there.

Not yet measured: PCH staleness handling (it must be rebuilt when any engine
public header changes — the same build-graph-input problem T0104 and T0123 both
had to solve), and the Windows-target figures.

### Cross-ticket obligation — T0127 (2026-08-04)

**An exception must not cross the module boundary, and this ticket is the only
one able to enforce it.** Measured rather than assumed: on Linux a `std::`
exception thrown in a module is caught only by `catch (...)`, and even an
engine-owned exception type is invisible to a `std::exception` handler, while
Windows matches by name and works in every case. The conventions now state the
rule and the boundary suite pins the behaviour on both targets — but the
`catch (...)` at every module entry point is **advisory prose with nothing
enforcing it**.

This ticket defines what a module entry point *is*, which is where it can stop
being advisory: a lifecycle whose entry points are declared through the engine,
rather than hand-written `extern "C"` functions, can carry the guard the same way
`hp_add_gameplay_module()` already carries the unload finalizer and the build-id
stamp. Design 48.2 with that in mind. Retrofitting a guard onto entry points
that already exist means touching every module ever written, which is the cost
this note exists to avoid.

## Built (2026-08-04) — `hp::ModuleHost`, and three findings that changed the design

### It works, end to end

A running process, with gameplay code edited and rebuilt underneath it:

```
generation 1: SandboxHealth::current default = 50
-- polling for a rebuild, exactly as frame phase 12 does --
generation 2: SandboxHealth::current default = 77
```

The value is read back through the engine's **shared meta context**, by
resolving the module's own reflected type from the host. So what changed is the
module's compiled code, not a file timestamp and not the loader's opinion of
one. No restart, no relink of the host.

Measured cost:

| | |
|---|---|
| reload swap (copy + `dlopen` + id check + `onLoad` + unload) | **1.76 ms** |
| phase-12 poll, nothing changed | **0.0015 ms** per module per frame |

Against T0048's own front-end measurement of 1.4 s per TU (0.34 s with a PCH),
the swap is free and the compile is the entire cost. That answers Done-when 6
and it also says where any further work belongs: the PCH, not the loader.

### The lifecycle is declared through the engine, which is what enforces T0127

`HP_GAMEPLAY_MODULE(name, onLoad, onUnload)` generates the exported descriptor
and wraps **every** entry point in `catch (...)`. That was T0127's open
obligation — it left the `catch (...)` at module entry points as advisory prose
with nothing enforcing it, and handed the enforcement here on the grounds that
this ticket is the one that defines what an entry point *is*. It now cannot be
forgotten, because there is no other way to declare one.

`tests/fixtures/throwing_module.cpp` throws a `std::` type from `onLoad` — the
case T0127 measured as impossible to match by type on ELF, so `catch (...)` is
the only handler that can run. Without the guard that test does not fail, it
takes the process down.

### Finding 1 — copy-before-load is load-bearing on **Linux**, for a different reason

48.3 framed this as a Windows problem: the OS locks a loaded DLL, so the next
build cannot overwrite it. True, and the Linux half is worse.

Linux does **not** refuse the write. It performs it, into the pages of the live
mapping. Found by writing a test that overwrote the real `libhp_sandbox.so`
while the boundary suite held it mapped `RTLD_NODELETE`: all 55 cases passed,
the summary printed, and the process then died in `.fini_array` out of a
corrupted image.

So on Windows a build without copy-before-load fails loudly; on Linux it
silently corrupts the running module. **The build's output must never be the
file that is mapped**, on either target — which is what the loader now
guarantees, and it is a stronger reason than the one the subtask gave.

### Finding 2 — an unstamped library must be refused *and left mapped*

The loader must not `dlclose` a library it is refusing, unless that library
carries `hp_module_build_id`.

Only `hp_add_gameplay_module()` compiles in the finalizer that makes the linker
emit `.fini_array` (T0105.1). Unmapping a library without one leaves its
`__cxa_atexit` registrations pointing into freed memory and the process dies at
exit, arbitrarily far from the cause. Measured with an engine reference held:

```
libhp_sandbox.so          fini_array=2   dlclose -> exit 0
libhp_throwing_module.so  fini_array=2   dlclose -> exit 0
libhp_abi_module.so       fini_array=0   dlclose -> SIGSEGV     (plain add_library)
```

The stamp is a sound proxy: the helper is the only thing that emits it, and it
emits the finalizer at the same time. So a refused unstamped library is left
mapped deliberately — one leaked mapping until exit, against a crash.

### Finding 3 — Remedy A holds for a real module, which T0105.1 could not claim

T0105.1 fixed the unload segfault and was explicit that it was proven only
against a fixture containing one `static std::string` and no engine code, with
an upstream reporter having seen a residual crash. **That gap is now closed.**
`libhp_sandbox` links the engine and registers reflected types into the shared
`entt::meta` context on every load, deregistering them on every unload:

```
25 host lifetimes, each load+reload+unload, exit 0
10 consecutive reloads inside one host, exit 0        (in the suite)
```

Also measured on the way: **`libhp_engine.so` itself has no `.fini_array`**
(`fini_array=0`). It is never unloaded — the host always links it and D12 says
the engine is loaded once and never reloaded — so this is not a live bug. It
does mean anything that ever `dlopen`s a module *without* holding an engine
reference will unload the engine on `dlclose` and crash. Recorded because
nothing else records it.

### Reload happens at frame phase 12 and nowhere else

`Application` owns a `ModuleHost` and calls `reloadChanged()` at the
end-of-frame safe point, which is why the reload is wired into the engine rather
than left to the editor: a host that polls whenever it likes gets it wrong
exactly once, and the symptom is memory corruption.

Phase 6 is still empty, so there is nothing to assert drained yet. The assertion
goes in immediately before the reload when T0072/T0075 fill it — a queued
module-typed payload outliving its module is a use-after-free in a type that no
longer exists.

### Why polling, and not inotify / ReadDirectoryChanges

A change notification fires on the **first** write, which is reliably a
half-written file — 48.4 asks for a debounce precisely because of this. A
`stat()` comparison of mtime and size is the debounce rather than needing one:
it observes the file as it is at the safe point, and a mid-write file simply
fails to be reloadable this frame and is retried next frame. It costs 1.5 µs per
module per frame, which is not worth optimising away with a mechanism that has a
worse failure mode.

### What is not done

- **48.5, state survival across reload — blocked on T0021.** There is no scene
  and no engine-owned `entt::registry` yet, so there is no component to mutate
  and check. What is proven is the mechanism the rule depends on: module-owned
  reflected types are registered on load and deregistered on unload, and a
  reloaded module re-registers them, with the engine's meta context surviving
  throughout. When T0021 lands, this is a small test, and **T0021 has been given
  the reference** so it is not rediscovered.
- **48.7, static linking for the shipped runtime — not started.** It needs
  T0043's export pipeline to have something to link into. Recorded on T0043.
- **The PCH is not adopted.** D19 measured it at 1.4 s → 0.34 s per TU and this
  ticket says 48.x should take it. It belongs in `HpGameplayModule.cmake` with
  the finalizer and the stamp, and it needs the staleness handling T0104 and
  T0123 both had to solve. Left as its own piece of work rather than bolted on
  here.
- **Debugging across reload was not evaluated.** The notes flag breakpoints
  detaching on unload as the known rough edge and "worth checking early"; it was
  not checked, and it partly determines whether this is pleasant enough to use.

## Correction (2026-08-04) — the first Done-when was overstated, and fixing it found two more things

**"Loaded at runtime by editor and runtime" was ticked when neither app loaded
anything.** The mechanism was real and tested — `Application::modules()`,
`ModuleHost::load()`, reload at phase 12 — but `grep` across `apps/*/src/*.cpp`
returned nothing, so the claim was true of the engine's capability and false of
the binaries that ship. That is the class of claim this project treats as worse
than an open question, and it was mine.

Both apps now load the sample module in `onStartup`, searching the layouts a
binary can find itself in — beside the exe (Windows dist, any co-located
export), `../lib` (Linux dist), `../../samples/sandbox` (the build tree).
Absence is not an error: an app with no gameplay module is legitimate and stays
legitimate. A *refusal* is different and stops the search, because trying the
next candidate would silently run an older copy of the same module — exactly the
confusion the build id exists to prevent.

```
build tree   [info ] module: loaded 'sandbox' from .../apps/runtime/../../samples/sandbox/libhp_sandbox.so
linux export [info ] module: loaded 'sandbox' from .../export-test/linux-x86_64/bin/../lib/libhp_sandbox.so
windows      [info ] module: loaded 'sandbox' from Z:\...\export-test\windows-x86_64\bin\libhp_sandbox.dll
```

Both exports run with the **build tree deleted**, and both unload cleanly at
exit.

### Found 1 — `dist` shipped a working copy

`libhp_sandbox.hot1.so` appeared in `dist/linux-x86_64/lib/`. It is the
copy-before-load working file: the destructor removes it, but a process that is
**killed** never runs the destructor — an editor crash, or a test runner timing
one out, which is precisely how this one survived. `dist` then globbed it like
any other shared object.

Two fixes, because either alone leaves a hole: `cmake/dist.cmake` never stages a
`.hotN.` file, and the loader sweeps stale copies of a module before making a
new one. New harness case, proven red first (`5 pass, 1 fail` without the
exclusion).

### Found 2 — a comment in the loader was confidently wrong

`copyPathFor` claimed the working copy must sit beside the original because the
module needs `$ORIGIN` to resolve `libhp_engine`. **Measured and false**: a
module loads fine from `/tmp`, because the host already has the engine mapped,
so the module's own RUNPATH never enters into it.

Staying beside the original is still right, for a smaller and true reason —
`$ORIGIN` semantics for any *other* dependency a gameplay module might link one
day. The comment now says that instead. A wrong reason recorded in a comment is
worse than no comment: the next person designs around it.

### Also added

`hp::executableDirectory()` (`engine/include/hp/Paths.hpp`). Three places had
already hand-rolled it — two test suites, and both apps would have been a third
and fourth — and every copy has to independently remember that the working
directory is not the answer. It is deliberately a *platform query* and not
content addressing; packs, patches and mounts are T0103's, with different rules.
