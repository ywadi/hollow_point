# T0016 — GUID system

| | |
|---|---|
| **Status** | ✅ DONE |
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

- [x] A 64-bit `GUID` type with random generation, equality and hashing
- [x] Usable as a key in `std::unordered_map` and comparable for sorting
- [x] Round-trips through **text** without loss, tested over 1000 generated values. **Binary round-trip is not tested here** — there is no serializer yet (T0020); the type is a plain `uint64_t` so it has no representation of its own to get wrong
- [x] Unit tests exist — `tests/fast/guid_test.cpp`, 7 cases

## Subtasks

- [x] 16.1 `GUID` value type, 64-bit, with `std::hash` specialisation
- [x] 16.2 Thread-safe random generation seeded from a proper entropy source
- [x] 16.3 Text formatting and parsing (hex), stable across platforms
- [x] 16.4 Tests: uniqueness over a large batch, round-trip, hashing
- [x] 16.5 Decide the invalid/null GUID representation and document it

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


## Findings

**64-bit is the decision, and it means UUID libraries do not apply.** This
identifies one project's content, not something on the internet. It halves the
storage in every asset reference, scene file and map key, and the collision risk
is negligible at the scale involved — with 100,000 assets the probability of any
collision is about 2.7e-10. `stduuid` and friends implement 128-bit RFC-4122
UUIDs, which is a different type at twice the size; vendoring one would give us
the wrong thing.

The corollary, recorded so nobody assumes otherwise: this is **not**
cryptographic and not unguessable. Nothing may use it as a capability or a
secret.

**Zero is the null GUID** so a `memset`'d or default-constructed struct is
invalid rather than accidentally referencing a real asset. `generate()` retries
in the astronomically unlikely event it produces zero — "astronomically
unlikely" is how you get a bug that appears once and cannot be reproduced.

**Seeding is the part that would have been quietly wrong.** The generator is
`thread_local` (asset import and scene loading are exactly what T0026 will
parallelise, and a global lock there would be a contention point), and each
thread seeds from a full `std::seed_seq` of eight `random_device` words rather
than a single call. Seeding a 64-bit Mersenne twister from one 32-bit value
gives it 32 bits of entropy — two importer threads starting together on a
platform with a weak `random_device` can then produce *identical streams*, and
therefore identical asset GUIDs.

**Parsing is strict because it parses data files.** Exactly 16 hex digits,
nothing else: no `0x`, no whitespace, no short or long forms. A lenient parser
turns a corrupted scene file into a silently wrong asset reference, which
surfaces much later as a missing mesh rather than as a parse error. Uppercase is
accepted on read but never written, so a hand-edited file works.

## Evidence

```
$ zig build test -Dtest=fast
[doctest] test cases:     24 |     24 passed | 0 failed | 0 skipped
[doctest] assertions: 213091 | 213091 passed | 0 failed |      (x2 -- both targets)
```

The assertion count is dominated by the uniqueness case: 100,000 generated
GUIDs, each asserted valid and previously unseen. That is the property the whole
type rests on, so it is checked at a scale where a broken generator cannot hide.
