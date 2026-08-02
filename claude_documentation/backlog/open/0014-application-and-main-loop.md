# T0014 — Application class, main loop and entry point

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 2 — Engine skeleton |
| **Created** | 2026-08-02 |

## Why

The `Application` is the hub every other subsystem plugs into, and it owns the
frame loop. Because the engine is a *library* it cannot own `main()`, so it
exposes an entry-point header that consuming programs include, plus a
`CreateApplication()` factory each app defines to return its own concrete type.

That indirection is what lets the editor and the runtime be two different
programs over one engine, which is the whole point of T0013.

## Done when

- [ ] `engine/include/hp/EntryPoint.hpp` defines `main`, and including it in an
      app is the only thing that app needs to become an executable
- [ ] `Application` owns the loop: poll → update layers → render → present
- [ ] The editor app runs, opens a window and exits cleanly on close
- [ ] Frame timing is available to the loop (delta time), since everything
      downstream needs it

## Subtasks

- [ ] 14.1 `Application` base class: construct subsystems, own the loop, expose
      `Run()` and a clean shutdown path
- [ ] 14.2 Entry-point header with `main`, calling `CreateApplication()`
- [ ] 14.3 Declare `CreateApplication()` in the engine, define it in each app
- [ ] 14.4 Delta time and a frame counter
- [ ] 14.5 `apps/editor/EditorMain.cpp` including the entry point and defining an
      `Editor : Application`
- [ ] 14.6 Confirm the same pattern works for `apps/runtime` (Phase 8 needs it)

## Notes / findings

Keep `Application` thin. In Laura's account this class became a dumping ground
and had to be refactored around a LayerStack afterwards. We are building the
LayerStack in the same phase (T0017), so `Application` should own *only* the
window, the layer stack and the loop from the start — everything else is a layer.

Shutdown order matters and is easy to get wrong later: layers must detach before
the render device goes away, or GPU resources outlive their device.
