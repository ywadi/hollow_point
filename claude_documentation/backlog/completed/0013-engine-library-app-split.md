# T0013 — Split the tree into an engine library and app consumers

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Order** | 30 |
| **Created** | 2026-08-02 |
| **Refs** | [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D12; gated by T0095 (see notes) |

## Why

The central architectural decision: **the editor must be a consumer of the engine,
not part of it.** When a game is exported the editor disappears entirely, so
anything the game needs at runtime has to live in the engine library. If that
boundary is not established structurally on day one, editor concerns leak into
engine code and the export in Phase 8 becomes impossible to untangle.

Structure it now, while there is nothing to move.

## Done when

- [x] ~~`engine/` builds as a **static** library~~ — **superseded by D12**: it builds as a *shared* library, with no dependency on any app
- [x] `apps/editor/` and `apps/runtime/` both link it and build for both targets — and **run** on both
- [x] The engine library contains no reference to the editor — no editor includes, no link dependencies, no editor identifiers in code. See the note below on why the literal grep needed refining
- [x] `dist` stages each app correctly — **and the staged build is relocatable**, which it was not at first

## Subtasks

- [x] 13.1 Create `engine/` with `CMakeLists.txt`, `include/`, `src/`
- [x] 13.2 Decide the public header layout — `engine/include/hp/...` so consumers
      write `#include <hp/Application.hpp>` and the namespace is unambiguous
- [x] 13.3 Link the Diligent targets the engine needs, PUBLIC vs PRIVATE chosen
      deliberately (see notes — this leaks into consumers if got wrong)
- [x] 13.4 Stub `apps/editor` and `apps/runtime`, each with the mandatory
      `readme.md` (G8) — the root glob picks them up with no root edit
- [x] 13.5 Leave room for a fourth artifact: `game/`, a hot-reloadable shared
      library both apps load (T0048) — it is not an app, and not the engine
- [x] 13.6 Build both targets, confirm no-op rebuild still clean

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


### Architecture decision (2026-08-03) — the engine is a **shared** library (D12)

T0095 settled this, so the first Done-when above ("`engine/` builds as a static
library target") is **superseded**: the engine builds as a shared library. It
was measured working on both targets — one address for an engine global seen
from the executable and from a loaded module, and Windows needed no import
library, no `.def` and no export list.

Three consequences for this ticket, all cheapest now:

- **`HP_API` export macros go into the headers as they are written.** Retrofitting
  them across hundreds of headers later is exactly the mechanical churn worth
  spending ten minutes to avoid. Default visibility for anything crossing the
  boundary, `-fvisibility=hidden` for the rest.
- **13.5's "leave room for `game/`" is now a requirement, not foresight.** The
  gameplay module links the engine shared library; the editor and runtime link
  the same one. Four artifacts, one engine.
- **PUBLIC vs PRIVATE (13.3) matters more, not less.** With a shared engine the
  question becomes which Diligent symbols the engine *re-exports* to modules,
  and D6's note about DiligentFX linking Diligent-Imgui PUBLIC now has teeth:
  get it wrong and every gameplay module inherits ImGui.

Rich C++ crosses the boundary — no C ABI, no binding layer — because engine and
gameplay are always built together (D12). T0104 is the guard that makes that
safe and should land alongside the module loader, not after it.


## Findings

**`game/` was the wrong name and is now `samples/sandbox/`.** Raised during the
work: a top-level `game/` says "this repo contains *the* game", which is
backwards for an engine intended to power several games. Deleting it would also
be wrong — the engine repo needs a module that actually crosses the boundary, or
T0048's reload and the boundary suite have no subject and CI proves nothing. So
it stays, renamed to say what it is: the engine's own sample gameplay module.
Real games are separate projects. **This exposed a genuinely unowned question —
how an external game project builds its module against an installed engine —
which is now T0109.**

**Windows needed the engine DLL beside the executable, and failed silently
without it.** The Linux build ran, the Windows build linked cleanly, and
`hp_editor.exe` under wine produced *no output at all*. PE has no RPATH: an
executable searches its own directory and PATH, and the engine DLL was in
`engine/`. Fixed with a POST_BUILD copy driven by `$<TARGET_RUNTIME_DLLS>`
rather than naming the DLL, so it keeps working when the engine gains
dependencies.

**`dist` produced a build that only ran on this machine, and three different
tests failed to notice.** The staged executable carried

```
RUNPATH: [$ORIGIN:/media/ywadi/second/hollow_point/build/linux-x86_64-release/engine]
```

Running it from `dist/` passed. Copying the whole folder elsewhere and running
it *also* passed — both only because that absolute path still existed locally.
It failed the moment the build tree was hidden:

```
$ mv build/linux-x86_64-release/engine build/linux-x86_64-release/engine_hidden
$ ./bin/hp_editor
./bin/hp_editor: error while loading shared libraries: libhp_engine.so: cannot open shared object file
```

This is exactly the hazard T0043 recorded, and worth noting that **T0043's own
prescribed test — "move the folder before running it" — was not sufficient
here.** Moving it is necessary but not enough; the build tree has to be gone.
Fixed with `BUILD_WITH_INSTALL_RPATH ON` and `INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib"`,
covering both the co-located layout and `dist`'s bin/ + lib/ split. The staged
binaries now carry `[$ORIGIN:$ORIGIN/../lib]` and nothing absolute.

**The "no reference to the editor" grep needed refining, not passing.** A naive
`grep -i editor engine/` fails — on *comments* that explain the architecture
("the editor, the runtime and every gameplay module link this one copy"). The
condition's intent is no *dependency*, so it is checked as: no editor includes,
no link dependencies, no editor identifiers in code. Prose that explains the
boundary is not a violation of it, and a check that forbids describing the rule
is a bad check.

**13.3 resolved as "link nothing from Diligent, deliberately".** Nothing in the
engine needs a device yet. The rule is written into `engine/CMakeLists.txt` for
when T0015 and T0025 arrive: link Diligent targets PRIVATE unless a *public
engine header* names their types, because D6 records DiligentFX linking
Diligent-Imgui PUBLIC and a careless PUBLIC here hands ImGui to every gameplay
module.

## Evidence

```
$ zig build all           # 1095 targets, both targets, exit 0
$ ./build/linux-x86_64-release/apps/editor/hp_editor
HollowPoint editor
  engine 0.0.1-skeleton, 1 instance(s), 1 consumer(s)
$ wine build/windows-x86_64-release/apps/runtime/hp_runtime.exe
HollowPoint runtime
  engine 0.0.1-skeleton, 1 instance(s), 1 consumer(s)
```

Artifacts on both targets: `libhp_engine.so` / `libhp_engine.dll` (`PE32+
executable (DLL)`), `libhp_sandbox.so` / `.dll`, and both apps.

Relocatable `dist`, proven with the build tree hidden:

```
$ readelf -d dist/linux-x86_64/bin/hp_editor | grep RUNPATH
  Library runpath: [$ORIGIN:$ORIGIN/../lib]
$ mv build/linux-x86_64-release/engine build/linux-x86_64-release/engine_hidden
$ cd /tmp/distmove && ./bin/hp_editor && ./bin/hp_runtime
HollowPoint editor / HollowPoint runtime   -- both ran, exit 0
```

No-op rebuild clean (13.6): `ninja: no work to do.` for both targets. Full suite
still green: 7 cases / 38 assertions integration, 3 cases / 10029 fast, both
targets.

## Not done here

**The apps do nothing but print.** There is no window, no loop, no device — that
is T0014 (Application and main loop) and T0015 (window and platform layer), and
this ticket deliberately does not anticipate them.

**Nothing loads the sandbox module yet.** It builds and links the engine, but no
app calls `dlopen` on it; the module loader is T0048. The boundary itself is
already covered by `tests/integration/module_boundary_test.cpp`.
