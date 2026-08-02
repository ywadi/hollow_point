# T0020 — Serialization util: rapidyaml + binary cook

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 3 — Data model |
| **Created** | 2026-08-02 |

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
