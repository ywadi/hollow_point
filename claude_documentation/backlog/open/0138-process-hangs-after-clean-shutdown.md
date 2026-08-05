# T0138 — The process hangs after a clean shutdown, so closing the window force-quits

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Order** | 178 |
| **Created** | 2026-08-05 |
| **Blocked by** | Nothing |
| **Refs** | [../completed/0048-hot-reloadable-gameplay-module.md](../completed/0048-hot-reloadable-gameplay-module.md), [../completed/0104-build-id-and-module-compatibility.md](../completed/0104-build-id-and-module-compatibility.md), [../completed/0014-application-and-main-loop.md](../completed/0014-application-and-main-loop.md), T0136, T0137 |

## Why

**Closing the editor window requires a force quit.** Reported from normal use,
then reproduced: the engine shuts down correctly and completely, and then the
*process* never exits.

Everything the engine is responsible for succeeds first — the close is seen, the
layers detach, the device is released, the run loop reports `exit 0`:

```
[info ] app: window close requested
[info ] editor: editor shutting down
[info ] render: device released
[info ] app: HollowPoint Editor ran 317 frame(s) in 5.625s, exit 0
[info ] sandbox: sandbox module unloading, generation 1
                        <- last line. Nothing follows, ever.
```

`sandbox module unloading` is logged by the module's own unload hook, so the hang
is **in or immediately after `dlclose`**, once every engine subsystem has already
been torn down.

## Not the renderer, and this is measured rather than assumed

Found while fixing T0137, so the obvious suspicion was the new present path. It
is not:

- The identical terminal line appears in logs captured **before** any renderer
  change in that session, on the rebuilt-but-unmodified tree.
- It reproduces on **both backends** — Vulkan and OpenGL — so it is not the
  Vulkan-specific blit, and not the swap chain.
- It reproduces whether the run ends by window close (8 frames) or by `SIGTERM`
  from `timeout` (317 and 343 frames), so it is not tied to how the exit began.

## Done when

- [ ] Closing the editor window exits the process, with no force quit
- [ ] The same holds for `hp_runtime`
- [ ] Where it blocks is identified and named, not worked around by skipping the
      unload
- [ ] A test covers process exit, so this cannot regress silently
- [ ] If `dlclose` is genuinely unsafe to call at exit, that is a recorded
      decision with its reasoning, not an omission

## Subtasks

- [ ] 138.1 Get a stack for the hung process. **`ptrace_scope` is 1 on this
      machine**, so `gdb -p` on a non-child fails — either launch under gdb, or
      `echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope` for the session
- [ ] 138.2 Determine whether the block is inside `dlclose` itself, a static
      destructor in the module, or an atexit handler registered by a driver
- [ ] 138.3 Check whether skipping the final `dlclose` at process exit avoids it,
      as a diagnostic — **not** as the fix until the cause is known
- [ ] 138.4 Check whether a background thread outlives the run loop: SDL, the
      Vulkan loader, or a driver worker
- [ ] 138.5 Fix, then cover it

## Notes / findings

**The suspicious asymmetry**: a hot reload during a session calls `dlclose` too
(T0048), and that path works — the module reloads and the generation counter
advances. So unloading is not inherently broken; unloading **at process exit**,
after the device and SDL are gone, is. That difference is the first thing to
look at, and it suggests ordering rather than the unload itself.

**Do not confuse this with T0136.** That one is a Windows/WSL file-locking
failure where a *running* editor blocks the test suite from staging its own copy
of the DLL. This is a Linux process that will not exit. They share a subsystem
and nothing else.

**A bounded run hides it.** `exitAfterFrames`, `timeout`, and CI all mask the
symptom, because the process is killed anyway — which is why this survived to be
found by hand. Whatever test closes 138.4 has to assert the process *exits on its
own*, not that it can be killed.
