# Backlog

One file per task, each carrying its own subtasks, rationale and findings.
Filenames are `NNNN-slug.md` and the number never changes or gets reused, so a
task can be referred to as **T0003** anywhere without ambiguity.

**A task's state is the folder it sits in**, not a field inside the file:

| Directory | Meaning |
|---|---|
| `open/` | Not started, or blocked |
| `inprogress/` | Being worked on now |
| `completed/` | Finished **and verified**, with the evidence in the file |

Move a task with `git mv` and update its **Status** line to match. `board/` reads
these folders directly, so the board can never disagree with the repository.

This is the work. For what is already proven to work — and what only appears to
— see [../documentation/05-verification-status.md](../documentation/05-verification-status.md).

## Board

| ID | Task | State | Priority |
|---|---|---|---|
| [T0001](completed/0001-run-windows-exe-under-wine.md) | Run the Windows executable under wine | ✅ DONE | High |
| [T0002](completed/0002-verify-windows-dist-staging.md) | Verify `dist` staging for Windows | ✅ DONE | High |
| [T0003](completed/0003-verify-vulkan-backend.md) | Verify the Vulkan backend on real hardware | ✅ DONE | High |
| [T0004](open/0004-verify-windows-host-build.md) | Verify building *on* a Windows host | 🔜 OPEN — needs a real Windows machine | Medium |
| [T0005](open/0005-exercise-new-library-apis.md) | Actually call enkiTS / meshoptimizer / ozz | 🔜 OPEN | Medium |
| [T0006](open/0006-define-real-application.md) | Define and scaffold the real application | 🔜 OPEN — needs a product decision | High |
| [T0007](open/0007-retire-imgui-probe.md) | Retire `apps/imgui_probe` | ⏸ BLOCKED by T0006 | Low |
| [T0008](open/0008-remove-imgui-modifier-shim.md) | Remove the `ImGuiKey_Mod*` shim | ⏸ BLOCKED upstream | Low |
| [T0009](open/0009-wire-up-ufbx.md) | Wire up `ufbx`, or drop it | 🔜 OPEN | Low |
| [T0010](open/0010-offline-configure.md) | Make `configure` work offline | 🔜 OPEN | Low |
| [T0011](open/0011-aarch64-linux-target.md) | Add an aarch64 Linux target | 🔜 OPEN | Low |

## Status values

| Marker | Meaning |
|---|---|
| 🔜 TODO | Not started |
| 🚧 IN PROGRESS | Started; the file records where it got to |
| ⏸ BLOCKED | Waiting on something — the file names what |
| ✅ DONE | Finished **and verified**, with the evidence recorded in the file |
| ❌ DROPPED | Deliberately not doing it; the file records why |

Finished tasks move to `completed/` rather than being deleted — the rationale and
findings are usually worth more after the fact than during.

## Writing a task file

Keep the template in `0001` as the shape. What matters:

- **Done when** — concrete, checkable conditions, not a vague goal. If you cannot
  say what output would prove it, the task is not ready to start.
- **Notes / findings** — append as you go. This is what survives a context reset.
- Do not mark ✅ DONE without pasting the evidence into the file.
