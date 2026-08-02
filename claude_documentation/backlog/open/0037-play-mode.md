# T0037 — Play / simulation mode

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Phase** | 6 — Editor |
| **Created** | 2026-08-02 |

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
