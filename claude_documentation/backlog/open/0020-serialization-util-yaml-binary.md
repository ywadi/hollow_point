# T0020 — Serialization util: rapidyaml + binary cook

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 3 — Data model |
| **Order** | 190 |
| **Created** | 2026-08-02 |
| **Refs** | T0016, T0082, [../inprogress/0068-input-mapping.md](../inprogress/0068-input-mapping.md) |

## Why

Everything in Phase 3 serializes — scenes, asset metafiles, project files,
EditorState. Building that on an ad-hoc basis per system produces four
inconsistent formats. One util, used everywhere.

The chosen model: **YAML is the source of truth, binary is a derived cache.**
YAML is what gets written on save, diffs in git and can be hand-edited; a binary
form is cooked from it for fast loads, and only the binary ships. That gets
authoring ergonomics and load speed without choosing between them.

## Done when

- [ ] rapidyaml vendored as a submodule and cross-compiling to both targets
- [ ] A wrapper API that engine code uses, so rapidyaml is swappable
- [ ] Round-trip: object → YAML → object, value-identical
- [ ] Cook: YAML → binary, and load binary → object, value-identical
- [ ] Staleness detection — binary is rebuilt when its YAML changes
- [ ] Tests round-trip every supported type through both paths

## Subtasks

- [ ] 20.1 Vendor rapidyaml (`biojppm/rapidyaml`) at a pinned tag; confirm it
      cross-compiles to `x86_64-windows-gnu` (it is CMake C++, expected fine)
- [ ] 20.2 Wrapper API — do not let `ryml::` types escape into engine headers
- [ ] 20.3 Serialization concept/traits so types opt in uniformly
- [ ] 20.4 Binary writer/reader with a version header and endianness decision
- [ ] 20.5 Staleness: content hash of the YAML stored in the binary, not mtime
- [ ] 20.6 Tests for both paths and for a deliberately corrupt binary

## Notes / findings

**Hash, not mtime.** Timestamps lie after a git checkout, a copy, or a clock
change, and a stale binary that loads without error is a genuinely nasty class of
bug. Store the source hash inside the binary and compare.

**Version the binary format from the first commit.** A binary with no version
field cannot be migrated, only discarded — and it will need migrating.

The binary format is a *cache*, so it must always be safely discardable: if it is
missing, stale or unreadable, the correct behaviour is silently re-cooking from
YAML, never failing. That property is also exactly what Phase 8 export needs.

rapidyaml is chosen over yaml-cpp for speed and active maintenance; the wrapper
is what makes that reversible. Its API is lower-level (tree-based), so the
wrapper carries more weight than it would with yaml-cpp — that is expected.

### Architecture review (2026-08-03) — reconcile 20.3 with reflection (T0053)

This ticket predates T0053 by a day and subtask 20.3 ("serialization
concept/traits so types opt in uniformly") now half-overlaps it. If traits and
reflection both exist as opt-in mechanisms, components get serialized two ways
and they drift — the exact four-switches problem T0053 exists to kill. The
reconciliation: **reflected types get serialization *derived from* T0053's
property enumeration automatically**; hand-written traits are only for the
leaf/primitive types reflection bottoms out in (GUID, math types, strings,
containers). 20.3 should be read as "define the leaf-type layer reflection
sits on", not as a parallel per-component mechanism.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0016** deliberately left the GUID binary round-trip untested — "there is
  no serializer yet (T0020)". The every-type round-trip in 20.6 is what closes
  that gap; treat GUID as a first-class case there, not an incidental uint64.
- **T0082.5** ties the binary cache to the schema version: the version header
  in 20.4 must participate in staleness alongside the content hash, or a
  schema change silently loads a stale cook that still parses.

### Cross-ticket obligation — T0068 (2026-08-04)

**T0068.7 is waiting here, and it is a loader rather than a design.** The action
layer is built and its bindings are deliberately *data* — an `InputMap` is a
list of (action, physical input) pairs plus per-axis tuning — so serializing
them needs a serializer to write against and nothing else.

Two properties of that data are worth knowing before designing the format:

- **An action's identity is a name hash (FNV-1a of e.g. `"Jump"`)**, not an
  index. A binding file must therefore store the *name*; storing the hash would
  work and would be unreadable and unfixable by hand, and storing an index would
  break the moment someone inserts an action.
- **User-rebindable means round-tripping**, so the format needs to survive being
  written by the engine and edited by a person, which is the argument for the
  text side of 20.x rather than the binary cook.

Key *display* names for a rebinding UI are T0112's concern, not this one — see
T0068's own note.

### Cross-ticket obligation — T0112 (2026-08-04)

**The English string table is a plain data asset, and this ticket picks its
format.** T0112 settled that all player-facing text is authored as a key
resolved through one authoritative English table, and deliberately did *not*
decide how that table is stored — it is an ordinary asset through the VFS (D13),
so whatever this ticket lands is what it uses. Nothing new is required of the
format; the point is that the table must not grow its own bespoke loader when
the time comes.

Two properties it needs, both of which the rebinding-file argument above already
implies: it round-trips human edits (translators and writers edit it by hand),
and it survives entries being inserted and reordered, since keys are the identity
and line position means nothing.
