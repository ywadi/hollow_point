# T0014 — Application class, main loop and entry point

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 2 — Engine skeleton |
| **Order** | 80 |
| **Created** | 2026-08-02 |

## Why

The `Application` is the hub every other subsystem plugs into, and it owns the
frame loop. Because the engine is a *library* it cannot own `main()`, so it
exposes an entry-point header that consuming programs include, plus a
`CreateApplication()` factory each app defines to return its own concrete type.

That indirection is what lets the editor and the runtime be two different
programs over one engine, which is the whole point of T0013.

## Done when

- [x] `engine/include/hp/EntryPoint.hpp` defines `main`, and including it in an
      app is the only thing that app needs to become an executable
- [x] `Application` owns the loop: poll → update → render → present. **"update layers" is update-the-app-hook for now** — the LayerStack is T0017, and the call site is marked
- [~] The editor app runs and exits cleanly — it does, on both targets, with the log pasted below. **It does not open a window**, because there is none until T0015: exit is by frame budget or `requestExit`, not by a close event. T0015 has since landed and closes on the window event
- [x] Frame timing is available to the loop (delta time) — T0057's `Clock`, ticked once per frame so every system sees one delta

## Subtasks

- [x] 14.1 `Application` base class: construct subsystems, own the loop, expose
      `Run()` and a clean shutdown path
- [x] 14.2 Entry-point header with `main`, calling `CreateApplication()`
- [x] 14.3 Declare `CreateApplication()` in the engine, define it in each app
- [x] 14.4 Delta time and a frame counter
- [x] 14.5 `apps/editor/EditorMain.cpp` including the entry point and defining an
      `Editor : Application`
- [x] 14.6 Confirm the same pattern works for `apps/runtime` (Phase 8 needs it)

## Notes / findings

Keep `Application` thin. In Laura's account this class became a dumping ground
and had to be refactored around a LayerStack afterwards. We are building the
LayerStack in the same phase (T0017), so `Application` should own *only* the
window, the layer stack and the loop from the start — everything else is a layer.

Shutdown order matters and is easy to get wrong later: layers must detach before
the render device goes away, or GPU resources outlive their device.


## Findings

**`Application` is thin, and staying that way is the point.** It owns the config,
one `Clock` and the loop. Nothing else. The note above about the reference
implementation becoming a dumping ground is the failure being avoided, and the
defence is structural: the loop's `update` and `render` blocks are where T0017's
LayerStack plugs in, so the pressure to add a feature *here* has somewhere else
to go.

**The entry point is a header, not a `main` in the library.** A `main` inside
`libhp_engine` would be pulled into every consumer -- the gameplay module and
every test binary -- none of which want one. Including `<hp/EntryPoint.hpp>` in
one translation unit is the whole contract, and 14.6 is satisfied by the runtime
using the identical pattern.

**`requestExit` records intent rather than breaking out.** The loop finishes the
frame it is in, so a hook can ask to stop without the ground disappearing
underneath it -- a test asserts render still runs for the frame in which update
asked to exit. Ripping out mid-frame is how half-updated state reaches teardown.

**A sink installed in `onStartup()` misses the engine's own startup lines**,
which was a real defect found by running it: `starting HollowPoint Editor` was
logged before the console sink existed and simply vanished. Both apps now
install the sink in `createApplication()` before constructing. Logging that only
begins once the thing being logged is already running is the least useful kind.

**`exitAfterFrames` is scaffolding and is labelled as such.** There is no window
to close (T0015), so without it the loop never ends. It also makes the loop
testable deterministically -- a test that ran until a timer expired would be
flaky. When T0015 lands, the close event becomes the exit condition and this
stays only as a test affordance.

## Evidence

```
$ ./build/linux-x86_64-release/apps/editor/hp_editor
[info ] app: starting HollowPoint Editor
[info ] editor: engine 0.0.1-skeleton, 1 instance(s), 1 consumer(s)
[info ] editor: editor shutting down
[info ] app: HollowPoint Editor ran 3 frame(s) in 0.000s, exit 0

$ wine build/windows-x86_64-release/apps/runtime/hp_runtime.exe
[info ] app: starting HollowPoint Runtime
...
[info ] app: HollowPoint Runtime ran 3 frame(s) in 0.000s, exit 0
```

`tests/integration/application_test.cpp`, 6 cases: frame count, hook ordering
(startup → update → render → shutdown), delta availability and sanity,
`requestExit` setting the exit code, `requestExit` finishing the current frame,
and an app with no frame budget stopping itself.

```
$ zig build test -Dtest=all
[doctest] test cases: 20 | 20 passed | 0 failed | 0 skipped
[doctest] assertions: 80 | 80 passed | 0 failed |     (x2 -- both targets)
```

## Not done

**No window.** The Done-when asking for one is left unticked rather than
reworded: it belongs to T0015, which is the very next ticket, and the loop is
already shaped to receive it -- the `poll` block is where SDL's event pump goes.
