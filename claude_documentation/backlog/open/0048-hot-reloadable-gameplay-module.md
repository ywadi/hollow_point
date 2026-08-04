# T0048 — Hot-reloadable gameplay module

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Very Complex |
| **Phase** | 2 — Engine skeleton |
| **Order** | 150 |
| **Created** | 2026-08-03 |
| **Refs** | T0100, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md), T0104 (Blocks this), T0105 |

## Why

Gameplay stays in **C++** — full performance, one language, no binding layer. The
cost C++ normally carries is iteration speed: every tweak means a rebuild and an
editor restart.

A hot-reloadable gameplay module removes that cost while keeping C++. Gameplay
lives in its own shared library that the editor (and runtime) load at startup and
can reload on change, without losing the open scene or restarting.

This is the decision *instead of* embedding a scripting language.

## Done when

- [ ] Gameplay builds as a shared library, loaded at runtime by editor and runtime
- [ ] Editing gameplay code and rebuilding reloads it live — no editor restart
- [ ] The open scene, entities and component data survive a reload intact
- [ ] Reload works on Linux and Windows
- [ ] A reload that fails to compile leaves the previous module running
- [ ] Reload time is fast enough to be worth it — measure it

## Subtasks

- [ ] 48.1 `game/` as a shared library with a narrow, stable C ABI entry surface
- [ ] 48.2 Loader: `dlopen`/`dlsym` on Linux, `LoadLibrary`/`GetProcAddress` on
      Windows, behind one interface
- [ ] 48.3 **Copy the library before loading on Windows** — the OS locks a loaded
      DLL and the next build cannot overwrite it
- [ ] 48.4 Watch the module file and reload on change (debounced — build tools
      write in stages and a naive watcher reloads a half-written file)
- [ ] 48.5 Verify state survives: mutate a component, reload, confirm intact
- [ ] 48.6 Handle load failure gracefully, keeping the old module
- [ ] 48.7 Ensure the exported *runtime* can link it statically instead

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

- **T0105** holds the linkage loose ends this loader inherits: 105.1 is the
  dlclose-segfault finding that forces `RTLD_NODELETE` (with T0029's retained
  Tracy string pointers riding on the same unload question), and 105.3
  re-verifies this ticket's mechanics against the shared-engine model. Read
  T0095's prototype results and T0105 before designing 48.2 — a loader
  designed around true unload will fight the toolchain.
