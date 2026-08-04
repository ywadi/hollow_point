# T0022 — Scene serialization

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Order** | 250 |
| **Created** | 2026-08-02 |
| **Refs** | T0053 (Blocks this), T0101 |

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
- [ ] 22.2 Per-component serialize/deserialize **driven by reflection (T0053)**
      — registration falls out of the type's one reflection declaration, so
      adding a component still touches exactly one place
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

### Architecture review (2026-08-03)

This ticket predates T0053 (reflection) and was written as if each component
hand-registers its own serialize functions. T0053 explicitly lists
serialization as a consumer that uses reflection "and nothing else" — so 22.2
has been reworded: per-component serialization is *generated from* the
reflection declaration, and the only hand-written serializers are the leaf
types the reflection layer bottoms out in (see the matching note on T0020).
**T0053 is therefore a hard prerequisite of this ticket**, which its own notes
already claim but this file previously did not acknowledge.

### Second review pass (2026-08-03) — the unknown-component policy has a third case

"Skip-and-warn vs hard-fail" misses the case this engine is guaranteed to hit:
**component/behaviour types that live in the gameplay module** (T0048/T0062).
When the module fails to build, or a type is renamed mid-refactor, its types
are *legitimately absent* at load time — hard-fail makes the editor unusable
exactly when the developer is mid-change, and skip-and-warn destroys the data
on the next save. The answer both Unity and Godot converged on: **preserve
unknown component data as an opaque blob** (keep the raw YAML subtree),
round-trip it through save, and re-materialise it when the type reappears.
Warn loudly meanwhile. Worth building from the start, because it also covers
files from newer schema versions more gracefully than refusal alone (T0082
still refuses newer *file* versions; this handles a missing *type*).

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0101.1** owns how transform parent/child links are represented on disk
  (GUIDs, fixed up on load) versus at runtime — and T0101's hierarchy
  round-trip test lands through this serializer. Consume its representation
  decision rather than inventing a link format here, or the two fix-up paths
  drift and hierarchies break only on load.
