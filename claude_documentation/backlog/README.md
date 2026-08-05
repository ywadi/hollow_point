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

| Order | ID | Task | Phase | State | Priority | Complexity |
|---|---|---|---|---|---|---|
| — | [T0095](completed/0095-gameplay-module-abi-and-linkage.md) | Gameplay module ABI: engine linkage, one engine state, entt across the boundary | 2 — Engine skeleton | ✅ DONE | High | Complex |
| 10 | [T0055](completed/0055-engine-conventions.md) | Engine conventions and error handling policy | 2 — Engine skeleton | ✅ DONE | High | Trivial |
| 30 | [T0013](completed/0013-engine-library-app-split.md) | Split the tree into an engine library and app consumers | 2 — Engine skeleton | ✅ DONE | High | Moderate |
| 35 | [T0019](completed/0019-profiling-macro-surface.md) | Profiling macro surface (Tracy-ready, no-op for now) | 2 — Engine skeleton | ✅ DONE | Medium | Trivial |
| 40 | [T0054](completed/0054-logging.md) | Logging and diagnostics | 2 — Engine skeleton | ✅ DONE | High | Simple |
| 45 | [T0118](completed/0118-generated-api-reference-for-agents.md) | Generated API reference for coding agents | 2 — Engine skeleton | ✅ DONE | High | Moderate |
| 50 | [T0056](completed/0056-core-utilities-policy.md) | Core utilities: math, memory, containers | 2 — Engine skeleton | ✅ DONE | Medium | Simple |
| 60 | [T0016](completed/0016-guid-system.md) | GUID system | 2 — Engine skeleton | ✅ DONE | High | Trivial |
| 70 | [T0057](completed/0057-time-system.md) | Time system | 2 — Engine skeleton | ✅ DONE | High | Simple |
| 80 | [T0014](completed/0014-application-and-main-loop.md) | Application class, main loop and entry point | 2 — Engine skeleton | ✅ DONE | High | Simple |
| 90 | [T0015](completed/0015-window-platform-layer.md) | Window, input and platform layer via SDL3 | 2 — Engine skeleton | ✅ DONE | High | Moderate |
| 100 | [T0018](completed/0018-event-system.md) | Event system | 2 — Engine skeleton | ✅ DONE | High | Moderate |
| 110 | [T0017](completed/0017-layer-stack.md) | LayerStack (system layers) | 2 — Engine skeleton | ✅ DONE | High | Simple |
| — | [T0126](completed/0126-remove-single-game-framing.md) | Remove the single-game framing from the backlog | 1 — Harden the build | ✅ DONE | High | Moderate |
| — | [T0127](completed/0127-exceptions-across-the-module-boundary.md) | A typed exception cannot cross the module boundary on Linux | 2 — Engine skeleton | ✅ DONE | High | Moderate |
| — | [T0048](completed/0048-hot-reloadable-gameplay-module.md) | Hot-reloadable gameplay module | 2 — Engine skeleton | ✅ DONE | High | Very Complex |
| — | [T0105](completed/0105-module-linkage-loose-ends.md) | Module linkage: the parts that need something built first | 2 — Engine skeleton | ✅ DONE | High | Moderate |
| — | [T0068](completed/0068-input-mapping.md) | Input mapping and action system | 2 — Engine skeleton | ✅ DONE | Medium | Moderate |
| 172 | [T0132](open/0132-gamepad-and-rumble.md) | Gamepad, rumble and hot-plug | 2 — Engine skeleton | ⏸ BLOCKED | Medium | Moderate |
| 174 | [T0133](open/0133-cursor-control-and-pointer-input.md) | Cursor control and pointer input as actions | 2 — Engine skeleton | 🔜 TODO | Medium | Moderate |
| 176 | [T0136](open/0136-module-hot-copy-fails-under-wsl-interop.md) | Module hot-reload staging fails when the Windows suite runs via WSL interop | 2 — Engine skeleton | 🔜 TODO | High | Simple |
| 180 | [T0103](completed/0103-virtual-filesystem-and-packs.md) | Virtual filesystem and content packs | 3 — Data model | ✅ DONE | High | Moderate |
| 185 | [T0112](completed/0112-string-identity-and-localisation.md) | String identity: keys before literals | 3 — Data model | ✅ DONE | Medium | Simple |
| 190 | [T0020](completed/0020-serialization-util-yaml-binary.md) | Serialization util: rapidyaml + binary cook | 3 — Data model | ✅ DONE | High | Complex |
| 200 | [T0021](completed/0021-scene-and-ecs.md) | Scene and entity-component system | 3 — Data model | ✅ DONE | High | Moderate |
| 210 | [T0101](completed/0101-transform-hierarchy-and-world-transforms.md) | Transform hierarchy propagation and world transforms | 3 — Data model | ✅ DONE | High | Moderate |
| 220 | [T0023](completed/0023-asset-manager.md) | AssetManager, asset pool and metafiles | 3 — Data model | ✅ DONE | High | Complex |
| 230 | [T0026](open/0026-job-system-enkits.md) | Job system on enkiTS | 3 — Data model | 🔜 TODO | Medium | Simple |
| 240 | [T0058](open/0058-asset-lifetime-hot-reload.md) | Asset lifetime, reference counting and hot reload | 3 — Data model | 🔜 TODO | Medium | Complex |
| 250 | [T0022](open/0022-scene-serialization.md) | Scene serialization | 3 — Data model | 🔜 TODO | High | Moderate |
| 260 | [T0071](open/0071-entity-references.md) | Entity references | 3 — Data model | 🔜 TODO | High | Moderate |
| 270 | [T0062](open/0062-entity-behaviours.md) | Entity behaviours: attaching C++ logic to entities | 3 — Data model | 🔜 TODO | High | Complex |
| 280 | [T0073](open/0073-gameplay-utilities.md) | Gameplay utility library | 3 — Data model | 🔜 TODO | Medium | Moderate |
| 290 | [T0024](open/0024-project-manager.md) | ProjectManager | 3 — Data model | 🔜 TODO | High | Moderate |
| 300 | [T0059](open/0059-prefabs.md) | Prefabs and entity templates | 3 — Data model | 🔜 TODO | High | Complex |
| 310 | [T0074](open/0074-gameplay-tags.md) | Hierarchical gameplay tags | 3 — Data model | 🔜 TODO | High | Moderate |
| 320 | [T0072](open/0072-entity-signals.md) | Entity signals and messaging | 3 — Data model | 🔜 TODO | Medium | Moderate |
| 330 | [T0075](open/0075-message-bus.md) | Message bus with addressing | 3 — Data model | 🔜 TODO | High | Complex |
| 340 | [T0077](open/0077-scene-management.md) | Scene loading, transitions and additive scenes | 3 — Data model | 🔜 TODO | High | Complex |
| 350 | [T0076](open/0076-autoloads.md) | Autoloads: project and scene scoped services | 3 — Data model | 🔜 TODO | High | Moderate |
| 360 | [T0078](inprogress/0078-settings-and-config.md) | Settings and configuration | 3 — Data model | 🚧 IN PROGRESS | Medium | Simple |
| 370 | [T0082](open/0082-schema-versioning.md) | Schema versioning and migration | 3 — Data model | 🔜 TODO | Medium | Moderate |
| — | [T0110](completed/0110-presentation-and-frame-pacing.md) | Presentation: vsync, present modes, frame pacing and focus loss | 4 — Render layer | ✅ DONE | High | Moderate |
| — | [T0025](completed/0025-render-layer.md) | Render layer and device lifecycle | 4 — Render layer | ✅ DONE | High | Moderate |
| — | [T0129](completed/0129-display-modes-and-window-control.md) | Display modes: fullscreen, resolution, DPI and monitors | 4 — Render layer | ✅ DONE | High | Moderate |
| — | [T0113](completed/0113-device-loss.md) | Device loss: decide the policy, fail distinguishably | 4 — Render layer | ✅ DONE | Medium | Simple |
| 385 | [T0111](completed/0111-anti-aliasing-and-render-scale.md) | Anti-aliasing and render scale: decide before the formats freeze | 4 — Render layer | ✅ DONE | High | Moderate |
| 386 | [T0135](open/0135-gpu-tests-on-real-hardware.md) | The gpu bucket must run on real hardware, and say which device it used | 4 — Render layer | 🔜 TODO | High | Simple |
| 390 | [T0046](completed/0046-frame-render-targets.md) | Frame render target management | 4 — Render layer | ✅ DONE | Medium | Simple |
| 400 | [T0027](completed/0027-render-stack.md) | RenderStack: composited visual layers | 4 — Render layer | ✅ DONE | High | Moderate |
| 410 | [T0028](completed/0028-scene-draw-submission.md) | Scene draw submission and the frame-rendered event | 4 — Render layer | ✅ DONE | High | Moderate |
| 415 | [T0130](completed/0130-camera-lens-model.md) | Camera lens model: decide what a camera describes | 4 — Render layer | ✅ DONE | High | Simple |
| 420 | [T0081](completed/0081-camera-system.md) | Camera system | 4 — Render layer | ✅ DONE | Medium | Simple |
| 430 | [T0085](completed/0085-layers-and-masks.md) | Object layers and masks | 4 — Render layer | ✅ DONE | High | Moderate |
| 440 | [T0045](open/0045-culling-and-render-queues.md) | Culling, sorting and render queues | 4 — Render layer | 🔜 TODO | High | Complex |
| 445 | [T0134](completed/0134-pbr-renderer-adoption.md) | How far DiligentFX's PBR renderer goes, and what inherits it | 4 — Render layer | ✅ DONE | High | Moderate |
| 450 | [T0060](open/0060-material-system.md) | Material assets and custom shader materials | 4 — Render layer | 🔜 TODO | High | Complex |
| 460 | [T0096](open/0096-hdr-pipeline-and-tonemapping.md) | HDR pipeline, tonemapping and the linear-workflow policy | 4 — Render layer | 🔜 TODO | High | Moderate |
| 470 | [T0079](completed/0079-lighting-system.md) | Lights and per-object light selection | 4 — Render layer | ✅ DONE | High | Complex |
| 480 | [T0086](open/0086-shadows.md) | Shadow rendering | 4 — Render layer | 🔜 TODO | High | Complex |
| 490 | [T0087](open/0087-environment-lighting.md) | Environment lighting, IBL and skybox | 4 — Render layer | 🔜 TODO | Medium | Moderate |
| 495 | [T0117](open/0117-font-and-text-rendering.md) | Font and text rendering | 4 — Render layer | 🔜 TODO | High | Moderate |
| 500 | [T0061](open/0061-debug-draw.md) | Debug draw service | 4 — Render layer | 🔜 TODO | Medium | Simple |
| 510 | [T0094](open/0094-gameplay-extensible-rendering.md) | Gameplay-extensible rendering | 4 — Render layer | 🔜 TODO | High | Complex |
| 515 | [T0120](open/0120-render-to-texture.md) | Camera render-to-texture | 4 — Render layer | 🔜 TODO | High | Complex |
| 520 | [T0050](open/0050-threading-model.md) | Threading model and enkiTS workload map | 4 — Render layer | 🔜 TODO | High | Complex |
| 530 | [T0047](open/0047-evaluate-render-graph.md) | Declarative pass layer (and why not an off-the-shelf frame graph) | 4 — Render layer | 🔜 TODO | Medium | Complex |
| 535 | [T0108](open/0108-decals.md) | Decals | 4 — Render layer | 🔜 TODO | Medium | Moderate |
| 540 | [T0093](open/0093-visibility-and-fog-of-war.md) | Visibility as an engine capability: prove a vision mechanic needs no engine changes | 4 — Render layer | 🔜 TODO | High | Moderate |
| 545 | [T0106](open/0106-vfx-sprites-and-flipbooks.md) | VFX sprites, flipbooks and blend modes | 4 — Render layer | 🔜 TODO | High | Moderate |
| 550 | [T0080](open/0080-particles.md) | Particle and VFX system | 4 — Render layer | 🔜 TODO | Medium | Complex |
| 555 | [T0107](open/0107-composed-vfx-assets.md) | Composed VFX assets: an effect is more than an emitter | 4 — Render layer | 🔜 TODO | Medium | Complex |
| 560 | [T0029](open/0029-tracy-cpu-profiling.md) | Tracy: vendor and CPU profiling | 5 — Profiling | 🔜 TODO | High | Moderate |
| 570 | [T0030](open/0030-tracy-gpu-profiling.md) | Tracy GPU zones through Diligent | 5 — Profiling | 🔜 TODO | High | Very Complex |
| 580 | [T0031](open/0031-profiling-workflow.md) | Profiling workflow, budgets and documentation | 5 — Profiling | 🔜 TODO | Low | Simple |
| 590 | [T0034](open/0034-editor-state.md) | EditorState | 6 — Editor | 🔜 TODO | High | Simple |
| 600 | [T0032](open/0032-editor-layer-and-panels.md) | Editor layer and panel framework | 6 — Editor | 🔜 TODO | High | Moderate |
| 610 | [T0033](open/0033-viewport-panel.md) | Viewport panel | 6 — Editor | 🔜 TODO | High | Moderate |
| 620 | [T0063](open/0063-editor-camera-and-picking.md) | Editor camera controls and entity picking | 6 — Editor | 🔜 TODO | High | Moderate |
| 630 | [T0035](open/0035-hierarchy-and-inspector.md) | Scene hierarchy and inspector panels | 6 — Editor | 🔜 TODO | High | Complex |
| 640 | [T0064](open/0064-transform-gizmos.md) | Transform gizmos | 6 — Editor | 🔜 TODO | High | Moderate |
| 650 | [T0065](open/0065-undo-redo.md) | Undo/redo command system | 6 — Editor | 🔜 TODO | High | Complex |
| 655 | [T0116](open/0116-csg-and-in-editor-geometry-authoring.md) | CSG / in-editor geometry authoring | 6 — Editor | 🔜 TODO | Medium | Complex |
| 660 | [T0036](open/0036-assets-panel.md) | Assets panel | 6 — Editor | 🔜 TODO | Medium | Moderate |
| 665 | [T0115](open/0115-editor-content-operations.md) | Editor content operations at scale | 6 — Editor | 🔜 TODO | Medium | Moderate |
| 670 | [T0066](open/0066-console-panel.md) | Console panel | 6 — Editor | 🔜 TODO | Medium | Simple |
| 675 | [T0114](open/0114-developer-console-and-cvars.md) | Runtime developer console and cvars | 6 — Editor | 🔜 TODO | Low | Moderate |
| 680 | [T0037](open/0037-play-mode.md) | Play / simulation mode | 6 — Editor | 🔜 TODO | Medium | Moderate |
| 690 | [T0067](open/0067-launcher.md) | Project launcher | 6 — Editor | 🔜 TODO | Medium | Simple |
| 700 | [T0038](open/0038-fbx-to-gltf-converter.md) | FBX → glTF converter (host tool) | 7 — Content pipeline | 🔜 TODO | High | Complex |
| 710 | [T0097](open/0097-texture-import-pipeline.md) | Texture import pipeline: mips, compression, colour space | 7 — Content pipeline | 🔜 TODO | Medium | Moderate |
| 720 | [T0039](open/0039-meshoptimizer-auto-lod.md) | Automatic LOD generation with meshoptimizer | 7 — Content pipeline | 🔜 TODO | High | Moderate |
| 730 | [T0040](open/0040-runtime-lod-selection.md) | Runtime LOD selection | 7 — Content pipeline | 🔜 TODO | Medium | Moderate |
| 740 | [T0041](open/0041-ozz-animation.md) | ozz-animation runtime and import | 7 — Content pipeline | 🔜 TODO | High | Complex |
| 740 | [T0128](open/0128-what-dist-actually-contains.md) | `dist` stages by glob, so nobody decides what ships | 8 — Runtime & export | 🔜 TODO | Medium | Moderate |
| 745 | [T0109](open/0109-external-game-projects.md) | How an external game project builds against the engine | 8 — Runtime & export | 🔜 TODO | High | Complex |
| 750 | [T0049](open/0049-animation-runtime.md) | Animation runtime library | 7 — Content pipeline | 🔜 TODO | High | Very Complex |
| 755 | [T0119](open/0119-wayland-and-linux-distribution.md) | Wayland support and Linux distribution | 8 — Runtime & export | 🔜 TODO | Medium | Moderate |
| 760 | [T0042](open/0042-runtime-application.md) | Runtime application | 8 — Runtime & export | 🔜 TODO | Medium | Simple |
| 770 | [T0043](open/0043-export-pipeline.md) | Export pipeline and asset relocation | 8 — Runtime & export | 🔜 TODO | Medium | Complex |
| 780 | [T0083](open/0083-save-system.md) | Save and load game state | 8 — Runtime & export | 🔜 TODO | Medium | Complex |
| 790 | [T0099](open/0099-crash-handling-and-diagnostics.md) | Crash handling and shipped-build diagnostics | 8 — Runtime & export | 🔜 TODO | Low | Moderate |
| 800 | [T0051](open/0051-physics-jolt.md) | Physics engine integration (Jolt) | 9 — Physics | 🔜 TODO | High | Very Complex |
| 810 | [T0098](open/0098-navigation-and-pathfinding.md) | Navigation and pathfinding | 9 — Physics | 🔜 TODO | Medium | Complex |
| 820 | [T0052](open/0052-audio.md) | Audio engine | 10 — Audio | 🔜 TODO | Medium | Complex |
| 830 | [T0088](open/0088-sky-atmosphere-time-of-day.md) | Sky, atmosphere and time of day | 11 — World & environment | 🔜 TODO | Medium | Complex |
| 840 | [T0089](open/0089-fog-and-atmospherics.md) | Fog and atmospherics | 11 — World & environment | 🔜 TODO | Medium | Moderate |
| 850 | [T0091](open/0091-volumetric-fog.md) | Volumetric fog and light shafts | 11 — World & environment | 🔜 TODO | Medium | Very Complex |
| 860 | [T0092](open/0092-wet-surfaces.md) | Wet surfaces | 11 — World & environment | 🔜 TODO | Medium | Complex |
| 870 | [T0090](open/0090-weather-system.md) | Weather system | 11 — World & environment | 🔜 TODO | Low | Complex |
| 880 | [T0069](open/0069-game-ui.md) | Game UI system | 12 — Game UI | 🔜 TODO | Medium | Complex |
| 890 | [T0070](open/0070-networking.md) | Networking | 13 — Networking | 🔜 TODO | Low | Very Complex |
| 900 | [T0011](open/0011-aarch64-linux-target.md) | Add an aarch64 Linux target | 14 — Deferred | 🔜 TODO | Low | Moderate |
| 910 | [T0008](open/0008-remove-imgui-modifier-shim.md) | Remove the `ImGuiKey_Mod*` compile-definition shim | 14 — Deferred | ⏸ BLOCKED on DiligentEngine upstream | Low | Trivial |
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
| — | [T0121](completed/0121-ci-build-time.md) | CI build time | 1 — Harden the build | ✅ DONE | Medium | Moderate |
| — | [T0131](completed/0131-ci-warm-cache-rebuilds-everything.md) | CI: the restored build tree never prevents a compile | 1 — Harden the build | ✅ DONE | High | Moderate |
| — | [T0100](completed/0100-frame-lifecycle-and-update-order.md) | Frame lifecycle and system update order | 2 — Engine skeleton | ✅ DONE | High | Moderate |
| — | [T0122](completed/0122-zig-cache-and-build-tree-host-collision.md) | Zig cannot build from a working tree on `/mnt/c` under WSL | 1 — Harden the build | ✅ DONE | High | Simple |
| — | [T0123](completed/0123-api-docs-should-not-be-hand-cranked.md) | The API reference is hand-cranked and always re-runs | 1 — Harden the build | ✅ DONE | Medium | Simple |
| — | [T0125](completed/0125-wsl-interop-detection-reads-proc-wrong.md) | WSL-interop detection silently loses to wine | 1 — Harden the build | ✅ DONE | Medium | Trivial |
| — | [T0124](completed/0124-backfill-cross-ticket-references.md) | Backfill cross-ticket references across the whole backlog | 1 — Harden the build | ✅ DONE | High | Moderate |
| — | [T0053](completed/0053-reflection-type-system.md) | Reflection and type system | 2 — Engine skeleton | ✅ DONE | High | Complex |
| — | [T0104](completed/0104-build-id-and-module-compatibility.md) | Build id stamping and module compatibility checks | 2 — Engine skeleton | ✅ DONE | High | Simple |
| — | [T0044](completed/0044-define-the-game.md) | Define the game | 2 — Engine skeleton | ❌ DROPPED | — | — |


## Execution order

The **Order** column is the order to work the tickets in, top to bottom. `▶`
marks what is in progress; `—` marks completed tickets, which are listed flat
at the end because they predate phases.

The number is not a row count — it is the ticket's own **Order** field. Every
open ticket carries `| **Order** | N |` in its header table, and this table
merely displays it. That is what keeps the three views honest: this table, the
HTML board and the ticket files all read one field, so they cannot disagree
about what comes next. Numbers step by ten so a new ticket slots between two
existing ones without renumbering anything; an existing Order only changes when
a ticket is genuinely re-sequenced, and that edit belongs in the ticket with a
note saying why.

The order is derived, in this priority:

1. **Hard blockers**, where one ticket explicitly gates another: `Blocks`
   fields record that T0095 gates T0048/T0062, T0104 gates T0048, T0103 gates
   T0023, and T0053 gates T0022/T0035/T0062 (the last recorded 2026-08-03 — a
   promotion of what those tickets already said in prose, not a new judgement).
   Two more recorded 2026-08-03 out of the design-gap survey: T0110 gates T0025
   (the present-mode policy must exist before the swap-chain code hardens — the
   default that runs otherwise is already visible in T0003's log), and T0111
   gates T0046 (the anti-aliasing/render-scale decision changes what the
   frame-target format declarations declare).
   T0095 also gates T0013's layout; that one is recorded on T0013's side, in
   its Refs and notes, because T0095 is in progress and its file is not being
   rewritten under whoever is working it. Each blocker exists because getting
   the order wrong there means redoing work rather than merely doing it later.
2. **Structural dependency** — logging before the code that logs, the
   engine/app split before anything that lands inside it, the device before the
   render stack.
3. **Priority, then complexity**, to break ties.

Two things worth doing early that dependency alone would not surface: T0055
(conventions) is first because it is the rules everything else is written
against, and T0019 (the profiling macro surface) is second because it is
trivial and its macros want to be in the code as it is written, not retrofitted
across the engine afterwards.

### Phases are sequential again

An earlier revision of this file flagged three Phase 2 tickets whose real
dependencies sat in Phase 3 and later, and left their phase fields alone. That
observation has now been acted on — all three are re-phased to **3 — Data
model**, each with a dated note in the ticket recording what actually forced
the move:

| Ticket | Was | Now | The dependency that forced it |
|---|---|---|---|
| T0062 Entity behaviours | 2 | 3 | cannot close before T0021/T0022 exist; sits after T0071, whose `EntityRef`s behaviour properties hold |
| T0073 Gameplay utilities | 2 | 3 | lives inside T0062's behaviours; blackboard and spatial queries need T0021. (The earlier claim here that it needed T0045/T0049 was wrong — both are cited in the ticket as contrasts, not dependencies; see its note) |
| T0076 Autoloads | 2 | 3 | config-as-data needs T0024/T0022, and "survives scene transitions" needs T0077 to exist first |

One ordering fix inside Phase 3 while re-deriving: T0026 (job system) now sits
late in the phase — after T0023, immediately before T0058, its first real
consumer — which is where its own note says to land it, rather than at the
phase's start where this table previously had it.

Acceptance still spans phases in places, deliberately: T0062's inspector items
close with T0035 (Phase 6), T0076's runtime parity with T0042 (Phase 8), and
several Phase 4 "visible in Tracy" conditions close with T0029 (Phase 5). The
affected tickets each say so — a ticket's phase is where the bulk of its work
happens, not a promise that every checkbox closes there.

Phases 5 and later are ordered structurally, but far enough out that the
ordering is a reasonable guess rather than a worked dependency graph. Phases 9
and 10 remain placeholder epics.

### The HTML board reads this order

`board/index.html` parses the `Order` field out of each ticket's header table
and sorts the Open column by it — its default **Execution order** view is this
table's order, flat; its **Phase** view groups by phase and orders cards by the
same field within each group. Since order respects phases, the two views agree
on what sits on top. The board still reads the ticket folders directly, so
neither its columns nor its ordering can drift from the repository.

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

**There are three states, and only two are legal in a closed ticket.**

| | Meaning | Legal in a ✅ DONE ticket? |
|---|---|---|
| `- [x]` | Done and verified | yes |
| `- [~]` | **Partly** achieved, with the shortfall stated inline | yes |
| `- [ ]` | Not done | **no** |

`check_backlog.py` enforces the last row: an unticked box in `completed/` is an
error. That is not pedantry — a ticket sitting at DONE with half its boxes empty
reads as abandoned rather than finished, and it makes the board claim something
untrue. It happened to T0085 and to fourteen others before the rule existed.

So when closing a ticket, every remaining `- [ ]` is one of two things:

- **Partly achieved** → `- [~]`, and say what is missing on the same line. The
  honest use, from T0027: gameplay implementing its own render layer is *"proven
  in principle by D22's link measurement and not in fact"*. It really was partly
  done.
- **Somebody else's** → **delete the line** and record it under a
  `## Descoped` heading: a `| Was | Went to | Because |` table, plus the
  obligation written onto the receiving ticket so the linkage reads both ways.
  See `completed/0085-layers-and-masks.md` for the shape to copy.

"Moving the remainder" means the item **leaves this checklist**. Leaving it here
unticked forever is the thing the rule forbids.

**Never tick a box to make a number look better.** If it cannot be established
from evidence that something was done, it was not done — and if it is genuinely
undone and still this ticket's, the ticket is not DONE.

`❌ SUPERSEDED` and `❌ DROPPED` tickets are exempt, and deliberately: unfinished
work is precisely what those states mean.

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

Current spread across all 126 tickets: 7 Trivial, 25 Simple, 53 Moderate, 32 Complex,
8 Very Complex (plus T0044, dropped and no longer rated). Recounted 2026-08-04;
`check_backlog.py` does not verify these numbers, so they go stale quietly — recount
rather than trusting them.

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
- **Order** — every open ticket carries an execution-order number in its header
  (see [Execution order](#execution-order)). For a new ticket, pick an unused
  number between its neighbours — the steps of ten exist so nothing else needs
  renumbering — and add the matching row to the Board table above.
- Do not mark ✅ DONE without pasting the evidence into the file.

## Phases

Open tickets carry a `Phase` field, and the board's Phase view groups the Open
column by it; within each group, cards follow the `Order` field, so the column
reads in the order the work should happen — see
[Execution order](#execution-order). Groups are collapsible and show an
aggregate progress bar.

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

**Phase numbers are engine-building order, not release order.** They say which
capability is safe to build on which, and nothing else. Phase 8 is called
"Runtime & export" and contains T0042, T0043 and T0083 — a runtime, an export
pipeline and save games — four phases before T0069 makes it possible to *draw a
menu*. That is not an oversight and it is not a promise that a game ships at
phase 8: it means the export path is buildable and testable before the UI
library is chosen, and that choosing the UI library early would gate real work on
a decision with no forcing function. A game ships when the phases it happens to
need are done, which is not the same as reaching a numbered milestone.
Recorded by T0112, which found the ordering easy to misread.

Phases 2 and 3 can start immediately — neither needs to know what the game is.
Nothing gates them on knowing "what the game is" — that question was dropped with T0044, because this is an engine for a studio's several games rather than for one game (T0109). Engine capabilities are decided on their own tickets, on engineering grounds.
