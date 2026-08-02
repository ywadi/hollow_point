# Backlog

One file per task, each carrying its own subtasks, rationale and findings.
Filenames are `NNNN-slug.md` and the number never changes or gets reused, so a
task can be referred to as **T0003** anywhere without ambiguity.

This is the *forward* work. For what is already proven to work — and what only
appears to — see [../05-verification-status.md](../05-verification-status.md).

## Board

| ID | Task | Status | Priority |
|---|---|---|---|
| [T0001](0001-run-windows-exe-under-wine.md) | Run the Windows executable under wine | 🔜 TODO | High |
| [T0002](0002-verify-windows-dist-staging.md) | Verify `dist` staging for Windows | 🔜 TODO | High |
| [T0003](0003-verify-vulkan-backend.md) | Verify the Vulkan backend on real hardware | 🔜 TODO | High |
| [T0004](0004-verify-windows-host-build.md) | Verify building *on* a Windows host | 🔜 TODO | Medium |
| [T0005](0005-exercise-new-library-apis.md) | Actually call enkiTS / meshoptimizer / ozz | 🔜 TODO | Medium |
| [T0006](0006-define-real-application.md) | Define and scaffold the real application | 🔜 TODO | High |
| [T0007](0007-retire-imgui-probe.md) | Retire `apps/imgui_probe` | 🚧 BLOCKED by T0006 | Low |
| [T0008](0008-remove-imgui-modifier-shim.md) | Remove the `ImGuiKey_Mod*` shim | 🚧 BLOCKED upstream | Low |
| [T0009](0009-wire-up-ufbx.md) | Wire up `ufbx`, or drop it | 🔜 TODO | Low |
| [T0010](0010-offline-configure.md) | Make `configure` work offline | 🔜 TODO | Low |
| [T0011](0011-aarch64-linux-target.md) | Add an aarch64 Linux target | 🔜 TODO | Low |

## Status values

| Marker | Meaning |
|---|---|
| 🔜 TODO | Not started |
| 🚧 IN PROGRESS | Started; the file records where it got to |
| ⏸ BLOCKED | Waiting on something — the file names what |
| ✅ DONE | Finished **and verified**, with the evidence recorded in the file |
| ❌ DROPPED | Deliberately not doing it; the file records why |

Finished tasks stay here with status ✅ DONE rather than being deleted — the
rationale and findings are usually worth more after the fact than during.

## Writing a task file

Keep the template in `0001` as the shape. What matters:

- **Done when** — concrete, checkable conditions, not a vague goal. If you cannot
  say what output would prove it, the task is not ready to start.
- **Notes / findings** — append as you go. This is what survives a context reset.
- Do not mark ✅ DONE without pasting the evidence into the file.
