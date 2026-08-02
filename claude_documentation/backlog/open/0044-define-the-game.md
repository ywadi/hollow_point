# T0044 — Define the game

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Phase** | 2 — Engine skeleton |
| **Created** | 2026-08-02 |
| **Supersedes** | part of T0006 |

## Why

The product is a game, with the editor built well enough to reuse. The engine
skeleton (Phase 2) and data model (Phase 3) can proceed without knowing what the
game is — but Phase 7 content work cannot, and several earlier decisions get
sharper once it is known.

This is a **product decision, not an engineering task.** It needs the project
owner, not an agent.

## Done when

- [ ] Genre, core loop and perspective (2D/3D, first/third person) written down
- [ ] Target platforms confirmed — currently Windows + Linux x86_64
- [ ] The handful of engine features the game genuinely needs, listed
- [ ] Recorded in `documentation/01-project-overview.md`

## Subtasks

- [ ] 44.1 Decide the game — **needs the owner**
- [ ] 44.2 List the engine features it actually requires
- [ ] 44.3 Re-check the phase plan against that list; drop what is not needed
- [ ] 44.4 Update the project overview

## Notes / findings

Answering this early is cheap and changes real decisions already on the board:

- **Transform hierarchy** (T0021) — needed for characters and attachments, less
  so for a flat world
- **Skeletal animation** (T0041) — an entire phase of work that some games do not
  need at all
- **LOD** (T0039/T0040) — high value for large open scenes, near-worthless for a
  confined one
- **Render stack composition** (T0027) — how much HUD/UI layering is required

Deciding late is not fatal, but it means building for a hypothetical. Nothing in
Phases 2-3 is wasted either way, which is why they can start first.
