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

| ID | Task | Phase | State | Priority |
|---|---|---|---|---|
| [T0001](completed/0001-run-windows-exe-under-wine.md) | Run the Windows executable under wine | 1 — Harden the build | ✅ DONE | High |
| [T0002](completed/0002-verify-windows-dist-staging.md) | Verify `dist` staging for Windows | 1 — Harden the build | ✅ DONE | High |
| [T0003](completed/0003-verify-vulkan-backend.md) | Verify the Vulkan backend on real hardware | 1 — Harden the build | ✅ DONE | High |
| [T0004](open/0004-verify-windows-host-build.md) | Verify building *on* a Windows host | 1 — Harden the build | 🔜 TODO | Medium |
| [T0007](completed/0007-retire-imgui-probe.md) | Retire `apps/imgui_probe` | 1 — Harden the build | ✅ DONE | Low |
| [T0010](completed/0010-offline-configure.md) | Make `configure` work offline | 1 — Harden the build | ✅ DONE | Low |
| [T0012](open/0012-test-harness.md) | Build a test harness for TDD | 1 — Harden the build | 🔜 TODO | High |
| [T0006](completed/0006-define-real-application.md) | Define and scaffold the real application | 2 — Engine skeleton | ❌ SUPERSEDED | High |
| [T0013](open/0013-engine-library-app-split.md) | Split the tree into an engine library and app consumers | 2 — Engine skeleton | 🔜 TODO | High |
| [T0014](open/0014-application-and-main-loop.md) | Application class, main loop and entry point | 2 — Engine skeleton | 🔜 TODO | High |
| [T0015](open/0015-window-platform-layer.md) | Window and platform layer via DiligentTools NativeApp | 2 — Engine skeleton | 🔜 TODO | High |
| [T0016](open/0016-guid-system.md) | GUID system | 2 — Engine skeleton | 🔜 TODO | High |
| [T0017](open/0017-layer-stack.md) | LayerStack (system layers) | 2 — Engine skeleton | 🔜 TODO | High |
| [T0018](open/0018-event-system.md) | Event system | 2 — Engine skeleton | 🔜 TODO | High |
| [T0019](open/0019-profiling-macro-surface.md) | Profiling macro surface (Tracy-ready, no-op for now) | 2 — Engine skeleton | 🔜 TODO | Medium |
| [T0044](open/0044-define-the-game.md) | Define the game | 2 — Engine skeleton | 🔜 TODO | High |
| [T0048](open/0048-hot-reloadable-gameplay-module.md) | Hot-reloadable gameplay module | 2 — Engine skeleton | 🔜 TODO | High |
| [T0020](open/0020-serialization-util-yaml-binary.md) | Serialization util: rapidyaml + binary cook | 3 — Data model | 🔜 TODO | High |
| [T0021](open/0021-scene-and-ecs.md) | Scene and entity-component system | 3 — Data model | 🔜 TODO | High |
| [T0022](open/0022-scene-serialization.md) | Scene serialization | 3 — Data model | 🔜 TODO | High |
| [T0023](open/0023-asset-manager.md) | AssetManager, asset pool and metafiles | 3 — Data model | 🔜 TODO | High |
| [T0024](open/0024-project-manager.md) | ProjectManager | 3 — Data model | 🔜 TODO | High |
| [T0025](open/0025-render-layer.md) | Render layer and device lifecycle | 4 — Render layer | 🔜 TODO | High |
| [T0026](open/0026-job-system-enkits.md) | Job system on enkiTS | 4 — Render layer | 🔜 TODO | Medium |
| [T0027](open/0027-render-stack.md) | RenderStack: composited visual layers | 4 — Render layer | 🔜 TODO | High |
| [T0028](open/0028-scene-draw-submission.md) | Scene draw submission and the frame-rendered event | 4 — Render layer | 🔜 TODO | High |
| [T0045](open/0045-culling-and-render-queues.md) | Culling, sorting and render queues | 4 — Render layer | 🔜 TODO | High |
| [T0046](open/0046-frame-render-targets.md) | Frame render target management | 4 — Render layer | 🔜 TODO | Medium |
| [T0047](open/0047-evaluate-render-graph.md) | Declarative pass layer (and why not an off-the-shelf frame graph) | 4 — Render layer | 🔜 TODO | Medium |
| [T0050](open/0050-threading-model.md) | Threading model and enkiTS workload map | 4 — Render layer | 🔜 TODO | High |
| [T0029](open/0029-tracy-cpu-profiling.md) | Tracy: vendor and CPU profiling | 5 — Profiling | 🔜 TODO | High |
| [T0030](open/0030-tracy-gpu-profiling.md) | Tracy GPU zones through Diligent | 5 — Profiling | 🔜 TODO | High |
| [T0031](open/0031-profiling-workflow.md) | Profiling workflow, budgets and documentation | 5 — Profiling | 🔜 TODO | Low |
| [T0032](open/0032-editor-layer-and-panels.md) | Editor layer and panel framework | 6 — Editor | 🔜 TODO | High |
| [T0033](open/0033-viewport-panel.md) | Viewport panel | 6 — Editor | 🔜 TODO | High |
| [T0034](open/0034-editor-state.md) | EditorState | 6 — Editor | 🔜 TODO | High |
| [T0035](open/0035-hierarchy-and-inspector.md) | Scene hierarchy and inspector panels | 6 — Editor | 🔜 TODO | High |
| [T0036](open/0036-assets-panel.md) | Assets panel | 6 — Editor | 🔜 TODO | Medium |
| [T0037](open/0037-play-mode.md) | Play / simulation mode | 6 — Editor | 🔜 TODO | Medium |
| [T0005](completed/0005-exercise-new-library-apis.md) | Actually call enkiTS / meshoptimizer / ozz | 7 — Content pipeline | ❌ SUPERSEDED | Medium |
| [T0009](completed/0009-wire-up-ufbx.md) | Wire up `ufbx`, or drop it | 7 — Content pipeline | ❌ SUPERSEDED | Low |
| [T0038](open/0038-fbx-to-gltf-converter.md) | FBX → glTF converter (host tool) | 7 — Content pipeline | 🔜 TODO | High |
| [T0039](open/0039-meshoptimizer-auto-lod.md) | Automatic LOD generation with meshoptimizer | 7 — Content pipeline | 🔜 TODO | High |
| [T0040](open/0040-runtime-lod-selection.md) | Runtime LOD selection | 7 — Content pipeline | 🔜 TODO | Medium |
| [T0041](open/0041-ozz-animation.md) | ozz-animation runtime and import | 7 — Content pipeline | 🔜 TODO | High |
| [T0049](open/0049-animation-runtime.md) | Animation runtime library | 7 — Content pipeline | 🔜 TODO | High |
| [T0042](open/0042-runtime-application.md) | Runtime application | 8 — Runtime & export | 🔜 TODO | Medium |
| [T0043](open/0043-export-pipeline.md) | Export pipeline and asset relocation | 8 — Runtime & export | 🔜 TODO | Medium |
| [T0008](open/0008-remove-imgui-modifier-shim.md) | Remove the `ImGuiKey_Mod*` compile-definition shim | 9 — Deferred | ⏸ BLOCKED on DiligentEngine upstream | Low |
| [T0011](open/0011-aarch64-linux-target.md) | Add an aarch64 Linux target | 9 — Deferred | 🔜 TODO | Low |


## Working a ticket — mandatory

This applies to **everyone**, humans and agents alike. It is not optional, and an
agent given a ticket is expected to follow it without being told again.

**1. Move the ticket to `inprogress/` before doing any work.**

```sh
git mv claude_documentation/backlog/open/00NN-*.md \
       claude_documentation/backlog/inprogress/
```

Then set **Status** inside the file to `🚧 IN PROGRESS`. Do this *first* — the
board is how progress is watched, and a ticket still sitting in `open/` while
someone works it makes the board lie.

**2. Tick the subtask checkboxes as you go, not at the end.**

Change `- [ ]` to `- [x]` the moment a subtask is genuinely finished. The board
renders a progress bar from these counts, so ticking them as you go is what makes
progress visible in real time. Batching them into one commit at the end defeats
the entire point.

**3. Append findings to `## Notes / findings` as you discover them.**

Especially anything surprising, anything that turned out to be wrong, and
anything you could not verify. This is what survives a context reset.

**4. Only move to `completed/` when it is actually verified.**

Paste the evidence — the command and its output — into the file. If a "Done when"
condition was not met, say so plainly rather than ticking it; a ticket that
overstates what was achieved is worse than one left open. See
`completed/0007-retire-imgui-probe.md` for how to record closing a ticket whose
precondition was not satisfied.

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

## Phases

Open tickets carry a `Phase` field, and the board groups the Open column by it so
the column reads in the order the work should happen. Groups are collapsible and
show an aggregate progress bar.

| Phase | Meaning |
|---|---|
| 1 — Harden the build | Make the foundation trustworthy before building on it |
| 2 — Engine skeleton | Library/app split, Application, window, GUID, layers, events |
| 3 — Data model | Serialization, Scene/ECS, assets, projects |
| 4 — Render layer | Device, render stack, draw submission, culling |
| 5 — Profiling | Tracy CPU and GPU instrumentation |
| 6 — Editor | Editor layer, panels, play mode |
| 7 — Content pipeline | FBX→glTF conversion, LOD generation, animation |
| 8 — Runtime & export | The second engine consumer, and shipping a game |
| 9 — Deferred | Blocked, speculative, or explicitly postponed |

Format is `<number> — <label>`; the number drives ordering. Completed tickets
predate phases and are ungrouped, which is why the Completed column renders flat.

Phases 2 and 3 can start immediately — neither needs to know what the game is.
That decision (T0044) only gates Phase 7.
