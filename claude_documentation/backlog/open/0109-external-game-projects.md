# T0109 — How an external game project builds against the engine

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 8 — Runtime & export |
| **Order** | 745 |
| **Created** | 2026-08-03 |
| **Refs** | T0013, T0024, T0043, T0048, T0104, T0118, D12 |

## Why

HollowPoint is an engine for **several** games. A game is therefore a separate
project — its own assets, scenes and gameplay module — built against an engine
it did not build itself.

Nothing describes how that works. T0024 (ProjectManager) is about project files
and T0043 (export) is about shipping a finished game, but the step in between —
*a game repo compiling a C++ gameplay module against an installed engine* — has
no owner. It surfaced while doing T0013: the engine repo grew a `game/`
directory, which was the wrong shape for a multi-game studio and became
`samples/sandbox/` instead. That rename fixed the naming and left the real
question open.

It matters more here than in most engines because of D12. Gameplay is **C++
compiled against the engine's real headers in lockstep**, not script and not a
stable C ABI. So a game project needs the engine's headers, its import
libraries, the same pinned toolchain, and the same build flags — get any of them
wrong and the failure is a mismatched module, which is exactly what T0104 exists
to refuse at load.

## Done when

- [ ] A game project living outside this repository can build a gameplay module
      that the editor and runtime load successfully
- [ ] It consumes a *released* engine, not a source checkout of it
- [ ] The toolchain it builds with is the pinned one, not whatever the developer
      has (D5), so the lockstep guarantee is real rather than hoped for
- [ ] The build id matches by construction, so T0104's check passes when things
      are correct and fails loudly when they are not
- [ ] Proven with a real second project, outside this tree

## Subtasks

- [ ] 109.1 Decide what "an installed engine" is: headers, import libs, shared
      libraries, the editor and runtime executables, and a version stamp. This
      is the SDK layout question and everything else follows it
- [ ] 109.2 How a game project gets the pinned toolchain — reuse `bootstrap.sh`
      / `bootstrap.ps1`, or ship a smaller equivalent with the engine release
- [ ] 109.3 The build interface. CMake package config (`find_package(HollowPoint)`)
      is the conventional answer; decide against the alternative of a game
      project vendoring the engine as a submodule
- [ ] 109.4 A project template a new game starts from
- [ ] 109.5 How the editor finds and loads a game project's module (T0024, T0048)
- [ ] 109.6 Verify end to end with a second repository, not a directory in this one

## Notes / findings

**Do not answer this by letting games live in this repository.** It is the
tempting shortcut and it defeats the purpose: the engine would acquire
game-specific content, the "single lib powers multiple games" property would
never be tested, and the export pipeline would grow assumptions about paths that
only hold in-tree. `samples/sandbox/` exists to exercise the boundary, and it is
deliberately not a game.

**Lockstep makes this stricter than it first appears.** D12's decision to use
rich C++ across the module boundary buys a great deal — no C ABI, no binding
layer — at the price that the module must be built with the *same* compiler and
flags as the engine. A game project cannot simply "have a C++ compiler". That is
what 109.2 is about, and it is the part most likely to be got wrong, because it
works fine on the machine that built the engine.

**This should probably land before there is a second game, not after.** The cost
of getting it wrong is paid by every game project that already exists when it is
fixed. Right now that number is zero.

**Phase 8 is where it sits, but the constraint is earlier.** 109.1's SDK layout
constrains what T0043's export produces and what T0013's artifacts must be
installable as, so a sketch of the answer is worth having well before Phase 8
arrives.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0118**: the generated API reference is part of what an installed engine
  ships — 109.1's SDK layout must carry `docs/api` (regenerated per release)
  alongside headers and libraries. T0118's notes state the expectation
  ("109.1's SDK-layout decision should account for it once both tickets are
  further along"); an agent or developer writing gameplay against a released
  engine needs the reference without building the engine first.
