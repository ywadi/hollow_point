# T0013 — Split the tree into an engine library and app consumers

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Created** | 2026-08-02 |

## Why

The central architectural decision: **the editor must be a consumer of the engine,
not part of it.** When a game is exported the editor disappears entirely, so
anything the game needs at runtime has to live in the engine library. If that
boundary is not established structurally on day one, editor concerns leak into
engine code and the export in Phase 8 becomes impossible to untangle.

Structure it now, while there is nothing to move.

## Done when

- [ ] `engine/` builds as a static library target with no dependency on any app
- [ ] `apps/editor/` and `apps/runtime/` both link it and build for both targets
- [ ] The engine library contains no reference to the editor, checkable by grep
- [ ] `dist` stages each app correctly (`cmake/dist.cmake` already handles `apps/*`)

## Subtasks

- [ ] 13.1 Create `engine/` with `CMakeLists.txt`, `include/`, `src/`
- [ ] 13.2 Decide the public header layout — `engine/include/hp/...` so consumers
      write `#include <hp/Application.hpp>` and the namespace is unambiguous
- [ ] 13.3 Link the Diligent targets the engine needs, PUBLIC vs PRIVATE chosen
      deliberately (see notes — this leaks into consumers if got wrong)
- [ ] 13.4 Stub `apps/editor` and `apps/runtime`, each with the mandatory
      `readme.md` (G8) — the root glob picks them up with no root edit
- [ ] 13.5 Leave room for a fourth artifact: `game/`, a hot-reloadable shared
      library both apps load (T0048) — it is not an app, and not the engine
- [ ] 13.6 Build both targets, confirm no-op rebuild still clean

## Notes / findings

The root `CMakeLists.txt` already globs `apps/*/CMakeLists.txt`, so adding app
directories needs no root change. `engine/` does need one `add_subdirectory`.

**PUBLIC vs PRIVATE matters more than usual here.** `DiligentFX` links
`Diligent-Imgui` PUBLIC, so anything linking DiligentFX inherits ImGui whether it
wants it or not (see D6). Decide explicitly which Diligent targets the engine
re-exports; sloppiness here is what makes "the runtime accidentally needs the
editor's dependencies" happen.

Do not use `add_sample_app()` for these apps — that is DiligentSamples' framework.
T0015 covers the app shell via `DiligentTools/NativeApp` instead.

**There are four artifacts, not three.** engine (static lib), editor (app),
runtime (app), and `game/` — a *shared* library holding gameplay, loaded and
reloaded at runtime (T0048). The layout should anticipate it even if it is
stubbed empty at first, because retrofitting a shared-library boundary means
revisiting every gameplay type's linkage.

### Architecture review (2026-08-03) — "engine as static library" is not a safe default

This ticket's "Done when" fixes the engine as a **static** library, but the
`game/` shared module (T0048) has to link against the engine somehow, and
statically linking the engine into both the executable *and* the game DLL
duplicates every engine global — including entt's `ENTT_API`-marked type-index
counters, which must be a single instance for the registry to work across the
boundary. On Windows the module cannot even resolve symbols from the exe
without deliberate export machinery. **T0095 now owns that decision and blocks
T0048/T0062; make it before committing this layout.** If the answer is "engine
is a shared library in dev builds", this ticket's first Done-when changes, and
export macros (`HP_API`) are cheapest to add while the headers are being
written — not after.
