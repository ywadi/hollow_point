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

| # | ID | Task | Phase | State | Priority | Complexity |
|---|---|---|---|---|---|---|
| ▶ | [T0095](inprogress/0095-gameplay-module-abi-and-linkage.md) | Gameplay module ABI: engine linkage, one engine state, entt across the boundary | 2 — Engine skeleton | 🚧 IN PROGRESS | High | Complex |
| 1 | [T0055](open/0055-engine-conventions.md) | Engine conventions and error handling policy | 2 — Engine skeleton | 🔜 TODO | High | Trivial |
| 2 | [T0019](open/0019-profiling-macro-surface.md) | Profiling macro surface (Tracy-ready, no-op for now) | 2 — Engine skeleton | 🔜 TODO | Medium | Trivial |
| 3 | [T0013](open/0013-engine-library-app-split.md) | Split the tree into an engine library and app consumers | 2 — Engine skeleton | 🔜 TODO | High | Moderate |
| 4 | [T0054](open/0054-logging.md) | Logging and diagnostics | 2 — Engine skeleton | 🔜 TODO | High | Simple |
| 5 | [T0056](open/0056-core-utilities-policy.md) | Core utilities: math, memory, containers | 2 — Engine skeleton | 🔜 TODO | Medium | Simple |
| 6 | [T0016](open/0016-guid-system.md) | GUID system | 2 — Engine skeleton | 🔜 TODO | High | Trivial |
| 7 | [T0057](open/0057-time-system.md) | Time system | 2 — Engine skeleton | 🔜 TODO | High | Simple |
| 8 | [T0014](open/0014-application-and-main-loop.md) | Application class, main loop and entry point | 2 — Engine skeleton | 🔜 TODO | High | Simple |
| 9 | [T0015](open/0015-window-platform-layer.md) | Window and platform layer via DiligentTools NativeApp | 2 — Engine skeleton | 🔜 TODO | High | Moderate |
| 10 | [T0018](open/0018-event-system.md) | Event system | 2 — Engine skeleton | 🔜 TODO | High | Moderate |
| 11 | [T0017](open/0017-layer-stack.md) | LayerStack (system layers) | 2 — Engine skeleton | 🔜 TODO | High | Simple |
| 12 | [T0100](open/0100-frame-lifecycle-and-update-order.md) | Frame lifecycle and system update order | 2 — Engine skeleton | 🔜 TODO | High | Moderate |
| 13 | [T0104](open/0104-build-id-and-module-compatibility.md) | Build id stamping and module compatibility checks | 2 — Engine skeleton | 🔜 TODO | High | Simple |
| 14 | [T0053](open/0053-reflection-type-system.md) | Reflection and type system | 2 — Engine skeleton | 🔜 TODO | High | Very Complex |
| 15 | [T0048](open/0048-hot-reloadable-gameplay-module.md) | Hot-reloadable gameplay module | 2 — Engine skeleton | 🔜 TODO | High | Very Complex |
| 16 | [T0044](open/0044-define-the-game.md) | Define the game | 2 — Engine skeleton | 🔜 TODO | High | Trivial |
| 17 | [T0068](open/0068-input-mapping.md) | Input mapping and action system | 2 — Engine skeleton | 🔜 TODO | Medium | Moderate |
| 18 | [T0073](open/0073-gameplay-utilities.md) | Gameplay utility library | 2 — Engine skeleton | 🔜 TODO | Medium | Moderate |
| 19 | [T0076](open/0076-autoloads.md) | Autoloads: project and scene scoped services | 2 — Engine skeleton | 🔜 TODO | High | Moderate |
| 20 | [T0062](open/0062-entity-behaviours.md) | Entity behaviours: attaching C++ logic to entities | 2 — Engine skeleton | 🔜 TODO | High | Very Complex |
| 21 | [T0103](open/0103-virtual-filesystem-and-packs.md) | Virtual filesystem and content packs | 3 — Data model | 🔜 TODO | High | Moderate |
| 22 | [T0026](open/0026-job-system-enkits.md) | Job system on enkiTS | 3 — Data model | 🔜 TODO | Medium | Simple |
| 23 | [T0020](open/0020-serialization-util-yaml-binary.md) | Serialization util: rapidyaml + binary cook | 3 — Data model | 🔜 TODO | High | Complex |
| 24 | [T0021](open/0021-scene-and-ecs.md) | Scene and entity-component system | 3 — Data model | 🔜 TODO | High | Moderate |
| 25 | [T0101](open/0101-transform-hierarchy-and-world-transforms.md) | Transform hierarchy propagation and world transforms | 3 — Data model | 🔜 TODO | High | Moderate |
| 26 | [T0023](open/0023-asset-manager.md) | AssetManager, asset pool and metafiles | 3 — Data model | 🔜 TODO | High | Complex |
| 27 | [T0058](open/0058-asset-lifetime-hot-reload.md) | Asset lifetime, reference counting and hot reload | 3 — Data model | 🔜 TODO | Medium | Complex |
| 28 | [T0022](open/0022-scene-serialization.md) | Scene serialization | 3 — Data model | 🔜 TODO | High | Moderate |
| 29 | [T0071](open/0071-entity-references.md) | Entity references | 3 — Data model | 🔜 TODO | High | Moderate |
| 30 | [T0024](open/0024-project-manager.md) | ProjectManager | 3 — Data model | 🔜 TODO | High | Moderate |
| 31 | [T0059](open/0059-prefabs.md) | Prefabs and entity templates | 3 — Data model | 🔜 TODO | High | Complex |
| 32 | [T0074](open/0074-gameplay-tags.md) | Hierarchical gameplay tags | 3 — Data model | 🔜 TODO | High | Moderate |
| 33 | [T0072](open/0072-entity-signals.md) | Entity signals and messaging | 3 — Data model | 🔜 TODO | Medium | Moderate |
| 34 | [T0075](open/0075-message-bus.md) | Message bus with addressing | 3 — Data model | 🔜 TODO | High | Complex |
| 35 | [T0077](open/0077-scene-management.md) | Scene loading, transitions and additive scenes | 3 — Data model | 🔜 TODO | High | Complex |
| 36 | [T0078](open/0078-settings-and-config.md) | Settings and configuration | 3 — Data model | 🔜 TODO | Medium | Simple |
| 37 | [T0082](open/0082-schema-versioning.md) | Schema versioning and migration | 3 — Data model | 🔜 TODO | Medium | Moderate |
| 38 | [T0025](open/0025-render-layer.md) | Render layer and device lifecycle | 4 — Render layer | 🔜 TODO | High | Moderate |
| 39 | [T0046](open/0046-frame-render-targets.md) | Frame render target management | 4 — Render layer | 🔜 TODO | Medium | Simple |
| 40 | [T0027](open/0027-render-stack.md) | RenderStack: composited visual layers | 4 — Render layer | 🔜 TODO | High | Moderate |
| 41 | [T0028](open/0028-scene-draw-submission.md) | Scene draw submission and the frame-rendered event | 4 — Render layer | 🔜 TODO | High | Moderate |
| 42 | [T0081](open/0081-camera-system.md) | Camera system | 4 — Render layer | 🔜 TODO | Medium | Simple |
| 43 | [T0085](open/0085-layers-and-masks.md) | Object layers and masks | 4 — Render layer | 🔜 TODO | High | Moderate |
| 44 | [T0045](open/0045-culling-and-render-queues.md) | Culling, sorting and render queues | 4 — Render layer | 🔜 TODO | High | Complex |
| 45 | [T0060](open/0060-material-system.md) | Material assets and custom shader materials | 4 — Render layer | 🔜 TODO | High | Complex |
| 46 | [T0096](open/0096-hdr-pipeline-and-tonemapping.md) | HDR pipeline, tonemapping and the linear-workflow policy | 4 — Render layer | 🔜 TODO | High | Moderate |
| 47 | [T0079](open/0079-lighting-system.md) | Lights and per-object light selection | 4 — Render layer | 🔜 TODO | High | Complex |
| 48 | [T0086](open/0086-shadows.md) | Shadow rendering | 4 — Render layer | 🔜 TODO | High | Complex |
| 49 | [T0087](open/0087-environment-lighting.md) | Environment lighting, IBL and skybox | 4 — Render layer | 🔜 TODO | Medium | Moderate |
| 50 | [T0061](open/0061-debug-draw.md) | Debug draw service | 4 — Render layer | 🔜 TODO | Medium | Simple |
| 51 | [T0094](open/0094-gameplay-extensible-rendering.md) | Gameplay-extensible rendering | 4 — Render layer | 🔜 TODO | High | Complex |
| 52 | [T0050](open/0050-threading-model.md) | Threading model and enkiTS workload map | 4 — Render layer | 🔜 TODO | High | Complex |
| 53 | [T0047](open/0047-evaluate-render-graph.md) | Declarative pass layer (and why not an off-the-shelf frame graph) | 4 — Render layer | 🔜 TODO | Medium | Complex |
| 54 | [T0093](open/0093-visibility-and-fog-of-war.md) | Visibility, vision cones and fog of war | 4 — Render layer | 🔜 TODO | High | Very Complex |
| 55 | [T0080](open/0080-particles.md) | Particle and VFX system | 4 — Render layer | 🔜 TODO | Medium | Complex |
| 56 | [T0029](open/0029-tracy-cpu-profiling.md) | Tracy: vendor and CPU profiling | 5 — Profiling | 🔜 TODO | High | Moderate |
| 57 | [T0030](open/0030-tracy-gpu-profiling.md) | Tracy GPU zones through Diligent | 5 — Profiling | 🔜 TODO | High | Very Complex |
| 58 | [T0031](open/0031-profiling-workflow.md) | Profiling workflow, budgets and documentation | 5 — Profiling | 🔜 TODO | Low | Simple |
| 59 | [T0034](open/0034-editor-state.md) | EditorState | 6 — Editor | 🔜 TODO | High | Simple |
| 60 | [T0032](open/0032-editor-layer-and-panels.md) | Editor layer and panel framework | 6 — Editor | 🔜 TODO | High | Moderate |
| 61 | [T0033](open/0033-viewport-panel.md) | Viewport panel | 6 — Editor | 🔜 TODO | High | Moderate |
| 62 | [T0063](open/0063-editor-camera-and-picking.md) | Editor camera controls and entity picking | 6 — Editor | 🔜 TODO | High | Moderate |
| 63 | [T0035](open/0035-hierarchy-and-inspector.md) | Scene hierarchy and inspector panels | 6 — Editor | 🔜 TODO | High | Complex |
| 64 | [T0064](open/0064-transform-gizmos.md) | Transform gizmos | 6 — Editor | 🔜 TODO | High | Moderate |
| 65 | [T0065](open/0065-undo-redo.md) | Undo/redo command system | 6 — Editor | 🔜 TODO | High | Complex |
| 66 | [T0036](open/0036-assets-panel.md) | Assets panel | 6 — Editor | 🔜 TODO | Medium | Moderate |
| 67 | [T0066](open/0066-console-panel.md) | Console panel | 6 — Editor | 🔜 TODO | Medium | Simple |
| 68 | [T0037](open/0037-play-mode.md) | Play / simulation mode | 6 — Editor | 🔜 TODO | Medium | Moderate |
| 69 | [T0067](open/0067-launcher.md) | Project launcher | 6 — Editor | 🔜 TODO | Medium | Simple |
| 70 | [T0038](open/0038-fbx-to-gltf-converter.md) | FBX → glTF converter (host tool) | 7 — Content pipeline | 🔜 TODO | High | Complex |
| 71 | [T0097](open/0097-texture-import-pipeline.md) | Texture import pipeline: mips, compression, colour space | 7 — Content pipeline | 🔜 TODO | Medium | Moderate |
| 72 | [T0039](open/0039-meshoptimizer-auto-lod.md) | Automatic LOD generation with meshoptimizer | 7 — Content pipeline | 🔜 TODO | High | Moderate |
| 73 | [T0040](open/0040-runtime-lod-selection.md) | Runtime LOD selection | 7 — Content pipeline | 🔜 TODO | Medium | Moderate |
| 74 | [T0041](open/0041-ozz-animation.md) | ozz-animation runtime and import | 7 — Content pipeline | 🔜 TODO | High | Complex |
| 75 | [T0049](open/0049-animation-runtime.md) | Animation runtime library | 7 — Content pipeline | 🔜 TODO | High | Very Complex |
| 76 | [T0042](open/0042-runtime-application.md) | Runtime application | 8 — Runtime & export | 🔜 TODO | Medium | Simple |
| 77 | [T0043](open/0043-export-pipeline.md) | Export pipeline and asset relocation | 8 — Runtime & export | 🔜 TODO | Medium | Complex |
| 78 | [T0083](open/0083-save-system.md) | Save and load game state | 8 — Runtime & export | 🔜 TODO | Medium | Complex |
| 79 | [T0099](open/0099-crash-handling-and-diagnostics.md) | Crash handling and shipped-build diagnostics | 8 — Runtime & export | 🔜 TODO | Low | Moderate |
| 80 | [T0051](open/0051-physics-jolt.md) | Physics engine integration (Jolt) | 9 — Physics | 🔜 TODO | High | Very Complex |
| 81 | [T0098](open/0098-navigation-and-pathfinding.md) | Navigation and pathfinding | 9 — Physics | 🔜 TODO | Medium | Complex |
| 82 | [T0052](open/0052-audio.md) | Audio engine | 10 — Audio | 🔜 TODO | Medium | Complex |
| 83 | [T0088](open/0088-sky-atmosphere-time-of-day.md) | Sky, atmosphere and time of day | 11 — World & environment | 🔜 TODO | Medium | Complex |
| 84 | [T0089](open/0089-fog-and-atmospherics.md) | Fog and atmospherics | 11 — World & environment | 🔜 TODO | Medium | Moderate |
| 85 | [T0091](open/0091-volumetric-fog.md) | Volumetric fog and light shafts | 11 — World & environment | 🔜 TODO | Medium | Very Complex |
| 86 | [T0092](open/0092-wet-surfaces.md) | Wet surfaces | 11 — World & environment | 🔜 TODO | Medium | Complex |
| 87 | [T0090](open/0090-weather-system.md) | Weather system | 11 — World & environment | 🔜 TODO | Low | Complex |
| 88 | [T0069](open/0069-game-ui.md) | Game UI system | 12 — Game UI | 🔜 TODO | Medium | Complex |
| 89 | [T0070](open/0070-networking.md) | Networking | 13 — Networking | 🔜 TODO | Low | Very Complex |
| 90 | [T0011](open/0011-aarch64-linux-target.md) | Add an aarch64 Linux target | 14 — Deferred | 🔜 TODO | Low | Moderate |
| 91 | [T0008](open/0008-remove-imgui-modifier-shim.md) | Remove the `ImGuiKey_Mod*` compile-definition shim | 14 — Deferred | ⏸ BLOCKED on DiligentEngine upstream | Low | Trivial |
| — | [T0001](completed/0001-run-windows-exe-under-wine.md) | Run the Windows executable under wine | 1 — Harden the build | ✅ DONE | High | Simple |
| — | [T0002](completed/0002-verify-windows-dist-staging.md) | Verify `dist` staging for Windows | 1 — Harden the build | ✅ DONE | High | Simple |
| — | [T0003](completed/0003-verify-vulkan-backend.md) | Verify the Vulkan backend on real hardware | 1 — Harden the build | ✅ DONE | High | Trivial |
| — | [T0004](completed/0004-verify-windows-host-build.md) | Verify building *on* a Windows host | 1 — Harden the build | ✅ DONE | Medium | Moderate |
| — | [T0005](completed/0005-exercise-new-library-apis.md) | Actually call enkiTS / meshoptimizer / ozz | 7 — Content pipeline | ❌ SUPERSEDED | Medium | Moderate |
| — | [T0006](completed/0006-define-real-application.md) | Define and scaffold the real application | 2 — Engine skeleton | ❌ SUPERSEDED | High | Trivial |
| — | [T0007](completed/0007-retire-imgui-probe.md) | Retire `apps/imgui_probe` | 1 — Harden the build | ✅ DONE | Low | Simple |
| — | [T0009](completed/0009-wire-up-ufbx.md) | Wire up `ufbx`, or drop it | 7 — Content pipeline | ❌ SUPERSEDED | Low | Simple |
| — | [T0010](completed/0010-offline-configure.md) | Make `configure` work offline | 1 — Harden the build | ✅ DONE | Low | Moderate |
| — | [T0012](completed/0012-test-harness.md) | Build a test harness for TDD | 1 — Harden the build | ✅ DONE | High | Complex |
| — | [T0084](completed/0084-continuous-integration.md) | Continuous integration | 1 — Harden the build | ✅ DONE | Medium | Moderate |
| — | [T0102](completed/0102-bootstrap-host-collision.md) | `bootstrap.sh` and `bootstrap.ps1` destroy each other's toolchain | 1 — Harden the build | ✅ DONE | Medium | Simple |


## Execution order

The **#** column is the order to work the tickets in, top to bottom. `▶` marks
what is in progress; `—` marks completed tickets, which are listed flat at the
end because they predate phases.

The order is derived, in this priority:

1. **Hard blockers**, where a ticket names another in its `Blocks` field. There
   are only three: T0095 gates T0013's layout, T0104 gates T0048, and T0103
   gates T0023. Each exists because getting the order wrong there means redoing
   work rather than merely doing it later.
2. **Structural dependency** — logging before the code that logs, the
   engine/app split before anything that lands inside it, the device before the
   render stack.
3. **Priority, then complexity**, to break ties.

Two things worth doing early that dependency alone would not surface: T0055
(conventions) is first because it is the rules everything else is written
against, and T0019 (the profiling macro surface) is second because it is
trivial and its macros want to be in the code as it is written, not retrofitted
across the engine afterwards.

### The order is not a promise that phases are sequential

Reading down the # column works, but **the tail of Phase 2 is not really Phase
2**, and this is worth knowing before planning around it:

| Ticket | Nominally | Actually needs |
|---|---|---|
| T0062 Entity behaviours | Phase 2 | T0021 Scene/ECS and T0022 scene serialization (Phase 3), T0035 inspector (Phase 6) |
| T0076 Autoloads | Phase 2 | T0024, T0071, T0077 (Phase 3), T0042 (Phase 8) |
| T0073 Gameplay utilities | Phase 2 | T0045 (Phase 4), T0049 (Phase 7) |

Their phase fields have been left alone rather than quietly rewritten — the
observation belongs to whoever owns the phase plan. In practice these three sit
after Phase 3's data model exists, so expect Phase 2 to end at T0104/T0053/T0048
and resume once the ECS is real.

Phases 5 and later are ordered structurally, but far enough out that the
ordering is a reasonable guess rather than a worked dependency graph. Phases 9
and 10 remain placeholder epics.

### The HTML board does not know this order

`board/index.html` reads the ticket folders directly and groups by phase; it has
no way to see the # column, because execution order lives in this file rather
than in a field inside each ticket. Making the board honour it means adding an
order field to all 92 tickets and a sort mode to read it — worth doing if the
board becomes the primary view, not worth it while this table is.

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

## Complexity

An estimate of how hard a ticket is to do *well*, independent of how important it
is. Priority says whether to do it; complexity says what you are walking into.

| Level | Rough meaning |
|---|---|
| Trivial | An hour or two, no design decisions |
| Simple | Understood approach, mostly writing it out |
| Moderate | Real design choices, or unfamiliar territory |
| Complex | Several interacting concerns; expect it to take longer than planned |
| Very Complex | Genuinely hard, with a real chance of needing a different approach |

Current spread: 7 Trivial, 20 Simple, 39 Moderate, 28 Complex, 9 Very Complex.

The **Very Complex** ones are worth knowing up front: **T0048** hot-reloadable
gameplay module (state must survive reload), **T0030** Tracy GPU zones (Diligent
manages command buffers internally), **T0049** the animation runtime (IK, layers,
root motion, events), and **T0051** Jolt physics (fixed timestep, and reconciling
root motion with simulation).

Phases 9 and 10 are **placeholder epics** — recorded so the architecture accounts
for them, not ready to start. They get broken into real tickets when reached.

High priority and high complexity together is the combination to plan around, not
to discover halfway through.

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
the column reads roughly in the order the work should happen. For the precise
order — and for where it crosses phase boundaries — see [Execution order](#execution-order). Groups are collapsible and
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
| 9 — Physics | Jolt integration, character controller, fixed timestep |
| 10 — Audio | Library still undecided; animation-event driven playback |
| 11 — World & environment | Sky, time of day, fog, volumetrics, weather, wet surfaces |
| 12 — Game UI | HUD/menus — ImGui ships but is unsuitable for player-facing UI |
| 13 — Networking | Placeholder; constrains determinism and state ownership if ever wanted |
| 14 — Deferred | Blocked, speculative, or explicitly postponed |

Format is `<number> — <label>`; the number drives ordering. Completed tickets
predate phases and are ungrouped, which is why the Completed column renders flat.

Phases 2 and 3 can start immediately — neither needs to know what the game is.
That decision (T0044) only gates Phase 7.
