# T0083 — Save and load game state

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 8 — Runtime & export |
| **Order** | 780 |
| **Created** | 2026-08-03 |

## Why

Distinct from scene serialization (T0022), and routinely confused with it. A scene
file is *authored content* — the level as the designer built it. A save game is
*runtime state* — where the player is now, what they are carrying, which doors
they opened.

They differ in what is stored, how often it is written, and how it must behave
when the game is updated after a player has saves.

## Done when

- [ ] Save captures the runtime state that must persist, not the whole scene
- [ ] Load restores it into a freshly loaded scene (T0077)
- [ ] Multiple save slots, plus autosave
- [ ] Saves are versioned and migratable (T0082) — old saves survive a patch
- [ ] Writing a save never corrupts the previous one — see notes
- [ ] Saving does not stall the frame
- [ ] A corrupt or unreadable save fails gracefully, without losing the others

## Subtasks

- [ ] 83.1 Decide what is saved — opt-in per component, not whole-scene dumps
- [ ] 83.2 A savable marker/attribute via reflection (T0053)
- [ ] 83.3 Slot management, metadata (timestamp, playtime, screenshot), autosave
- [ ] 83.4 Atomic write: temp file then rename
- [ ] 83.5 Async write off the main thread (T0050)
- [ ] 83.6 Versioning and migration (T0082)
- [ ] 83.7 Entity identity across save/load — see notes
- [ ] 83.8 Tests, including loading a save written by an older schema

## Notes / findings

**Do not save the whole scene.** It seems simplest and it is a trap: saves become
huge, and patching the game breaks every existing save because the authored
content changed underneath it. Save only runtime *deltas* — the player's state,
which pickups are gone, which doors are open — and reload authored content from
the scene file.

**Entity identity is the hard part.** A save referring to "the door in the east
corridor" must find that door after the scene is reloaded. Entity GUIDs from the
scene file are stable, so that works for authored entities — but *spawned* entities
have no stable identity and need their own scheme, or they must be reconstructed
from saved state rather than referenced.

**Atomic writes are not optional.** Writing directly over an existing save means a
crash or power loss mid-write destroys it — for many players the worst possible
bug. Write to a temporary file, fsync, rename.

Saves are exactly where T0082's migration pays off: players have saves, the game
gets patched, and "your saves are gone" is unacceptable.

### Second review pass (2026-08-03) — two identity dependencies this ticket inherits

- **Prefab-instance entities are only save-stable if T0059 persists the
  per-instance GUID map** (see T0059's second-pass note). "Entity GUIDs from
  the scene file are stable" is true for hand-placed entities and *false* for
  prefab-instance children unless T0059.9 lands — a door that is part of a
  prefab would get a new GUID every load, and saves referencing it silently
  break. Treat T0059.9 as a prerequisite of 83.7.
- **A save must record the loaded-scene set, not assume one scene.** T0077
  allows additive scenes; "load restores it into a freshly loaded scene"
  (singular) under-specifies. The save needs the active scene plus any
  additively loaded ones (and their autoload state, T0076), or loading a save
  taken mid-stream reconstructs a different world.
