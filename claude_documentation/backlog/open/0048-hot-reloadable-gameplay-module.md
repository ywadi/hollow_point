# T0048 — Hot-reloadable gameplay module

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Very Complex |
| **Phase** | 2 — Engine skeleton |
| **Created** | 2026-08-03 |

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
