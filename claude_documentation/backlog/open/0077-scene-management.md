# T0077 — Scene loading, transitions and additive scenes

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 3 — Data model |
| **Order** | 340 |
| **Created** | 2026-08-03 |
| **Refs** | T0100, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md) |

## Why

T0024 opens *a* scene when a project opens. Nothing covers changing scenes **at
runtime** — which every game needs: menu to level, level to level, level to game
over.

Also missing: loading a scene without blocking the frame, and having more than one
scene loaded at once (a persistent gameplay scene plus a streamed area, or a UI
scene over a world scene).

## Done when

- [ ] Change the active scene at runtime, from gameplay code
- [ ] Loading is asynchronous — no multi-second frozen frame
- [ ] Additive load: multiple scenes resident, with one active
- [ ] Unload releases entities and scene-scoped autoloads (T0076) in the right order
- [ ] Project-scoped autoloads survive the transition; scene-scoped ones do not
- [ ] Assets shared between old and new scenes are not reloaded (T0058)
- [ ] A transition hook exists so a loading screen is possible
- [ ] Entity references across scenes are handled or explicitly forbidden (T0071)

## Subtasks

- [ ] 77.1 Scene registry: which scenes are loaded, which is active
- [ ] 77.2 Async load on the job system — parse off-thread, GPU upload on main (T0050)
- [ ] 77.3 Additive load and unload
- [ ] 77.4 Teardown order: scene autoloads, then entities, then scene-scoped assets
- [ ] 77.5 Asset retention across transitions via reference counting (T0058)
- [ ] 77.6 Transition events for loading screens
- [ ] 77.7 Decide on persistent entities across scene change — see notes
- [ ] 77.8 Editor support for previewing additive setups
- [ ] 77.9 Tests: load, additive load, unload, and transition with shared assets

## Notes / findings


### Frame anatomy — phase 12 — the end-of-frame safe point (T0100, D17)

A scene transition requested from gameplay applies at **phase 12**, never
immediately — it must not destroy the scene currently being iterated. Additive
scenes must also decide whether phases 4–9 run per-scene or once across all of
them; the frame anatomy explicitly leaves that to this ticket.

The full order is in [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md); the decision and what it rejected is **D17** in the
decision log. If this ticket needs a phase that does not exist, that is a change
to T0100's document and to D17 — not a new call bolted into `Application::run`.

**Asset retention is what makes transitions fast, and it depends on T0058.**
Without reference counting, unloading a scene either drops assets the next scene
needs (and reloads them, causing a stall) or keeps everything forever. Reference
counting makes "shared assets stay resident" fall out naturally. Sequencing
matters: load the new scene *before* unloading the old one, so shared assets never
hit zero references.

**Persistent entities are a real question** — Unity's `DontDestroyOnLoad`. It is
genuinely useful for a player that carries across levels, and it is genuinely
messy: which scene owns it, where is it saved, what happens on reload. Deciding
*not* to support it is respectable, provided it is a decision. The alternative is
usually to hold that state in a project-scoped autoload (T0076) and respawn the
entity per scene, which is cleaner.

**Async loading is not optional at any real scale.** A synchronous load of a scene
with a few hundred entities and their assets is a visible freeze. The two-stage
pattern from T0058 applies: parse and deserialize on a worker, create GPU
resources on the main thread.

### Second review pass (2026-08-03) — additive scenes make two ambiguities load-bearing

- **Can the same scene be loaded additively twice?** (Streaming the same
  area-scene as two instances.) If yes, its entity GUIDs collide across the
  two copies — the play-mode-clone situation, but permanent — so the per-scene
  GUID lookup (T0021's review note) becomes mandatory and every "find by GUID"
  API needs a scene in hand. If no, say so and assert it on load. Either answer
  is fine; the silent middle is not.
- **Which scenes do scene-spanning services see?** Bus radius/tag queries
  (T0075), spatial queries (T0073.6) and tag queries (T0074.7) — across *all*
  resident scenes, or the active one? Decide once here, not per-system.
  Cross-scene `EntityRef` is already flagged in T0071 ("disallow rather than
  half-support"); this is the query-side twin of that decision.

Both belong in 77.1's design, and T0083 now notes that saves must record the
resident-scene set.

### Cross-ticket obligation — T0101 (2026-08-04)

**Nothing calls transform propagation in the frame, because nothing owns a
`Scene`.** T0101 built `Scene::propagateTransforms()` — one pass, parents before
children, dirty-tracked so an unchanged scene recomputes nothing — and D17 places
it at frame phases 7 and 9: phase 7 serves the followers that run at phase 8,
phase 9 catches whatever phase 8 itself moved.

What is missing is an owner. `Application` holds a window, a layer stack and an
input system, but no scene, so there is no place in `Application::run` for the
call to go. When this ticket gives the engine a loaded, held scene, wiring the
two propagation calls is the small remaining piece — and the second pass may be
made incremental (most frames it has almost nothing to do) but **may not be
removed**, per D17.
