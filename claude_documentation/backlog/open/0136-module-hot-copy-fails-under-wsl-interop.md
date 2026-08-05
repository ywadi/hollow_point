# T0136 — A running app blocks the module tests, and the message does not say so

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 2 — Engine skeleton |
| **Order** | 176 |
| **Blocked by** | Nothing |
| **Created** | 2026-08-05 |
| **Refs** | [../completed/0048-hot-reloadable-gameplay-module.md](../completed/0048-hot-reloadable-gameplay-module.md), [../completed/0104-build-id-and-module-compatibility.md](../completed/0104-build-id-and-module-compatibility.md), [../completed/0125-wsl-interop-detection-reads-proc-wrong.md](../completed/0125-wsl-interop-detection-reads-proc-wrong.md) |

## Why

**Seven integration tests failed at once, and the cause was an editor left
running.** `hp_editor.exe` had been launched on Windows to look at the demo
scene; its module host had staged `libhp_sandbox.hot1.dll` and mapped it.
Windows refuses to overwrite a mapped DLL, so every later `ModuleHost::load`
could not stage its own copy:

```
could not stage a working copy at
\\wsl.localhost\...\samples\sandbox\libhp_sandbox.hot1.dll -- Permission denied
```

The suite went green the moment the editor was closed. **Nothing was wrong with
the engine, the harness, the build id or the module.**

## The trap, which is the point of this ticket

The failure presents as **seven module-loading tests breaking simultaneously**,
immediately after a change that touched public headers — which reads
unmistakably as an ABI or build-id regression. It is neither, and two things
sent the first diagnosis in the wrong direction:

- **The `\\wsl.localhost\` path in the message.** It is just how the path
  renders when WSL interop hands the `.exe` to the real Windows loader (T0125).
  It looks like D18's filesystem-boundary problem and is unrelated to it.
- **"Permission denied" describes the syscall, not the situation.** The file is
  not permission-protected; it is *in use*. The message sends you to look at
  ownership and mounts rather than at `tasklist`.

Two dead ends were ruled out by measurement before the real cause was found:
deleting every `*.hot*.dll` and running twice failed identically, and the same
binary passed 89/89 under `wine` — which pointed at the environment precisely
because wine had not mapped the file.

## Done when

- [ ] Staging failure distinguishes **"in use"** from a genuine permission
      problem, and says which. On Windows that is `ERROR_SHARING_VIOLATION` /
      `ERROR_ACCESS_DENIED` on a mapped image, which is worth naming outright:
      *"the file is loaded by another process — is an editor or game still
      running?"*
- [ ] The same message is checked on Linux, where a mapped `.so` **can** be
      replaced, so the failure mode differs by platform and the diagnostic
      should not pretend otherwise
- [ ] Decided and recorded: whether staging should fall back to a temp directory
      rather than the module's own directory. It would sidestep this entirely,
      at the cost of the copy no longer sitting beside the original — see
      T0048's reasoning for why it is there
- [ ] A line in `03-build-harness.md` under the existing traps, because this
      costs an hour and the fix is `tasklist.exe`

## Notes / findings

**The first useful action when module tests fail together is to check for a
running app**, not to suspect the build id. `tasklist.exe | grep hp_` from WSL
takes a second and would have answered it immediately.

**This gets more likely, not less.** T0033's viewport panel and T0042's runtime
both mean an app is routinely open while tests run, and hot reload (T0048) is
precisely the feature that keeps a module file mapped. The diagnostic is what
makes it a five-second problem instead of an afternoon.
