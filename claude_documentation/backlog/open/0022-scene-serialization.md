# T0022 — Scene serialization

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Created** | 2026-08-02 |

## Why

A scene that cannot be saved and reloaded is a demo. This is also the first real
consumer of the T0020 util and will expose whatever is wrong with it while the
schema is still small.

## Done when

- [ ] Save produces a readable, git-diffable `.hpscene` YAML
- [ ] Load reconstructs an equivalent scene — same entities, GUIDs, components
- [ ] Binary cook and load produce an identical scene to the YAML path
- [ ] Unknown components in a file are handled deliberately (see notes)
- [ ] Round-trip tests over a scene using every component type

## Subtasks

- [ ] 22.1 `.hpscene` schema — entities as a list, each with GUID, tag, components
- [ ] 22.2 Per-component serialize/deserialize, registered so adding a component
      does not mean editing a central switch
- [ ] 22.3 Save: iterate entities and their components
- [ ] 22.4 Load: recreate entities preserving GUIDs, then components
- [ ] 22.5 Wire binary cook/load through T0020
- [ ] 22.6 Round-trip tests, including an empty scene and a large one

## Notes / findings

**Preserve GUIDs on load.** Regenerating them breaks every reference into the
scene from outside and silently corrupts the project — a subtle enough failure
that it is worth an explicit test.

**Decide the unknown-component policy now:** skip-and-warn, or hard-fail. Skipping
is friendlier during development but silently discards data on save-after-load,
which destroys a file. Failing loudly is safer. Whichever we pick, write it down.

Component registration should be data-driven enough that adding a component type
touches one place. A central `if/else` chain over component types is the thing
that rots fastest in an engine of this shape.
