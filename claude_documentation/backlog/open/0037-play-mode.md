# T0037 — Play / simulation mode

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 6 — Editor |
| **Order** | 680 |
| **Created** | 2026-08-02 |
| **Refs** | T0057, T0100, T0115 |

## Why

The point at which the editor stops being a scene viewer and becomes a place to
test the game. Entering play mode **clones** the open scene so that changes made
while simulating are discarded on exit — otherwise every playtest silently
corrupts the authored scene, which is a genuinely destructive failure.

## Done when

- [ ] Play/stop switches the scene manager into simulation mode
- [ ] Entering play clones the scene; exiting discards the clone and restores it
- [ ] Edits made during play are provably discarded — tested, not assumed
- [ ] The editor UI clearly indicates simulating vs editing
- [ ] The renderer can use a different settings profile while simulating

## Subtasks

- [ ] 37.1 Simulation mode in the scene manager, backed by `Scene::Copy` (T0021)
- [ ] 37.2 Play/stop controls, and a pause if cheap
- [ ] 37.3 Unmistakable visual state indication — a border tint or similar
- [ ] 37.4 Separate render settings profile for play mode
- [ ] 37.5 Route input to the game while playing, to the editor while editing
- [ ] 37.6 Test that a change made during play does not survive stop

## Notes / findings

**`Scene::Copy` must be a genuine deep copy of everything mutable.** A shallow
copy that shares component storage means play-mode edits write straight through
to the authored scene — which looks fine until someone loses an hour of work.
This is the single most important correctness requirement in the ticket, and it
is why T0021 lists scene copy as a first-class deliverable rather than an
afterthought.

Input routing is the other trap: while playing, the game wants input, but the
editor still needs its own shortcuts (stop, in particular). The event system's
consume semantics (T0018) is the mechanism — the editor layer sits above and
takes only what it needs.

### Second review pass (2026-08-03) — play mode is a *session*, and cloning the scene is only part of it

The clone covers entity/component state. Four other things hold game state or
in-flight work, and each needs an explicit start/stop story or state leaks
across play sessions — the same failure T0052 already flags for audio:

- **Project-scoped autoloads (T0076).** A game-state manager or fog-of-war
  memory created "at startup" and merely *reused* across play sessions keeps
  its mutated state after stop — the autoload equivalent of the shallow-copy
  bug this ticket exists to prevent. Play entry must construct the gameplay
  project-scope autoloads fresh (and stop must destroy them); in the exported
  runtime the equivalent point is process startup, so the semantics stay
  identical (T0042). See the matching note on T0076.
- **Scene-scoped autoloads** must be created against the *clone* on play entry
  and destroyed on stop — never pointed at the authored scene.
- **Deferred queues (T0072/T0075).** Stop must drain-or-discard queued
  messages; a payload delivered after the clone is destroyed dangles. The
  end-of-frame safe point (T0100) is where play enter/exit belongs.
- **In-flight jobs (T0026) and async loads (T0058/T0077)** referencing the
  clone must be fenced before the clone is destroyed.

Physics (T0051, later) follows the same shape: the simulation world is built
for the clone on entry and torn down on stop. Nothing to build now — but 37.1
should be designed as "enter/exit a simulation *session*", not just "swap the
scene pointer", so these attach cleanly instead of by patch.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0057** built pause as a state distinct from a zero time scale *for this
  ticket*: use `Clock`'s pause so unpausing restores whatever slow-motion
  scale was set, and use the separate editor/game clock instances so pausing
  the simulation does not freeze the editor's own UI.
- **T0115.6** autosaves on play-entry — the insurance that catches "crashed
  while testing". Keep a pre-play hook available where it can attach.
