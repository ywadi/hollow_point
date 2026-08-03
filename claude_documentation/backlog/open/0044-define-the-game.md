# T0044 — Define the game

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Trivial |
| **Phase** | 2 — Engine skeleton |
| **Order** | 160 |
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
- **Navigation/pathfinding** (T0098) — near-certain if the game has moving
  NPCs (which the vision-cone requirement in T0093 implies), droppable if not
- **Terrain, water, vegetation** — no tickets exist for any of them. The
  deleted `terrain_lab` app suggests outdoor terrain has been of interest;
  if the game is outdoors, terrain alone is a subsystem on the scale of
  Phase 4 and needs planning, not discovering. If indoors/authored geometry,
  say so and close the question

Deciding late is not fatal, but it means building for a hypothetical. Nothing in
Phases 2-3 is wasted either way, which is why they can start first.

### Questions added by the design-gap survey (2026-08-03)

The survey (`documentation/07-design-gaps.md`) found several absences whose
right size depends entirely on this ticket's answer. Each is one question to
ask when the game is defined; the referenced tickets hold the consequences.

- **Indoor/outdoor mix** (survey item 6) -- extends the terrain bullet above in
  the other direction: if the game has authored interiors under a scene-global
  IBL, some local ambient control is needed and T0087.8 owns dispositioning
  it. All-outdoor or all-interior collapses the question.
- **Scripted scenes / cutscenes** (item 7) -- does this game have them at all?
  A sequencer/timeline is a subsystem and must not be built speculatively;
  T0081's amendment keeps a cheap camera-offset/blend seam open either way.
- **Platform / store** (item 10) -- platforms are currently asked only as
  "Windows + Linux confirmed". Whether the game ships on Steam decides
  achievements, cloud saves and overlay work at shipping time; the two cheap
  constraints that do not wait are already noted on T0103 (write-directory
  layout) and T0075 (platform code subscribes to gameplay events).
- **Ragdoll, and morph targets** (item 14) -- death physics is a yes/no with a
  seam consequence (T0051's note); facial animation or shape-key props touch
  importer, vertex format and animation runtime at once (T0038's note). A
  rejection line in either direction closes them cheaply.
- **Aspect-ratio policy** (item 15) -- a *design* question for this game, not
  polish: 21:9 sees more world, which in a vision-cone stealth game (T0093) is
  a gameplay advantage. Free aspect, letterbox to a design ratio, or clamped
  horizontal FOV -- decide, then one line each in T0081 and T0069.
- **Content scale: does anything ever stream?** (item 16) -- for a
  confined-scene desktop game the answer is probably no, and memory budgets
  (T0031's note) get sized accordingly. If the answer is ever yes, streaming
  is a Phase-4-shaped retrofit -- which is exactly why the question is asked
  once, here, now.
