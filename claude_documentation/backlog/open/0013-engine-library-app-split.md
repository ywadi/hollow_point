# T0013 — Split the tree into an engine library and app consumers

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
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
- [ ] 13.5 Build both targets, confirm no-op rebuild still clean

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
