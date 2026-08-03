# T0016 — GUID system

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Trivial |
| **Phase** | 2 — Engine skeleton |
| **Order** | 60 |
| **Created** | 2026-08-02 |

## Why

Every engine object — entities, assets, scenes, projects — needs a stable
identity that survives serialization, and incrementing integers do not: they
break as soon as objects are deleted and recreated, and they collide across
files merged from different sources.

Nothing depends on GUIDs yet, and *everything* built after this depends on them.
So it has to exist before the data model in Phase 3, not alongside it.

## Done when

- [ ] A 64-bit `GUID` type with random generation, equality and hashing
- [ ] Usable as a key in `std::unordered_map` and comparable for sorting
- [ ] Round-trips through text (YAML) and binary without loss
- [ ] Unit tests exist — this is a good first real target for T0012

## Subtasks

- [ ] 16.1 `GUID` value type, 64-bit, with `std::hash` specialisation
- [ ] 16.2 Thread-safe random generation seeded from a proper entropy source
- [ ] 16.3 Text formatting and parsing (hex), stable across platforms
- [ ] 16.4 Tests: uniqueness over a large batch, round-trip, hashing
- [ ] 16.5 Decide the invalid/null GUID representation and document it

## Notes / findings

64-bit random gives ~50% collision probability around 5 billion objects
(birthday bound), which is far beyond any plausible project. 128-bit is the
safer classical choice but doubles the cost in every component and every
serialized file. 64 is the deliberate trade — record it in the decision log.

Do **not** seed from wall-clock time alone: two objects created in the same tick,
or two editor instances launched together, will collide. Use
`std::random_device` and a per-thread engine.

The random generator must be thread-safe from the start, because the job system
(T0026) will create objects from worker threads.
