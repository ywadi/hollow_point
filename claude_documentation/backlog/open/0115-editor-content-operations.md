# T0115 — Editor content operations at scale

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 6 — Editor |
| **Order** | 665 |
| **Created** | 2026-08-03 |
| **Refs** | T0023, T0035, T0036, T0037, T0043, T0058, T0065, T0074, T0099, [../../documentation/07-design-gaps.md](../../documentation/07-design-gaps.md) item 13 |

## Why

Individually small operations, collectively the "editor feels hostile after
500 assets" cluster. None appeared anywhere at survey time (2026-08-03):

- **Asset rename/move/delete with reference handling.** T0036 imports and
  assigns; T0023 flags *externally* missing or moved sources. No ticket lets
  you rename or delete an asset *from the editor* and have references fixed or
  at least reported.
- **Find usages / dependency view.** T0043 walks asset dependencies at export;
  the editor never surfaces them. "What uses this texture?" is unanswerable by
  design so far.
- **Hierarchy search/filter.** T0035 lists entities live with no search. Fine
  at 50 entities, not at 2000. (Multi-select is owned -- T0064 forces the
  decision; duplicate is owned -- T0035.3b. Deliberately not re-scoped here.)
- **Editor scene autosave/backup.** T0099's note names losing an unsaved scene
  as "the editor equivalent of losing a save" -- and no ticket owns preventing
  it.

None of these changes an interface, which is why they can safely sit late in
Phase 6; all of them get linearly more painful with content volume, which is
why they should not slip further.

## Done when

- [ ] An asset can be renamed or moved from the assets panel and every
      reference still resolves. GUID indirection (T0023/T0058) should make
      this nearly free -- references are GUIDs, not paths -- and part of this
      item is *verifying* that stays true (metafile and cooked-cache
      relationships included)
- [ ] Deleting an asset checks usage first: in use → warn and list the users;
      unused → delete cleanly including metafile and cooked artefacts
- [ ] Find usages: select an asset, see the scenes, prefabs and materials that
      reference it -- driven by the same dependency index the delete check
      uses, and the same walk T0043 does at export, built once rather than
      three times
- [ ] The hierarchy panel has search/filter by name (tag filtering via T0074
      is a bonus, not a requirement)
- [ ] Scene autosave exists: periodic and on entering play mode (T0037),
      written to a backup location that never overwrites the user's file, with
      a recovery offer after a crash
- [ ] Operations that touch scene state go through undo (T0065); file-level
      operations (rename/move/delete) at minimum confirm before acting

## Subtasks

- [ ] 115.1 Reference/dependency index: which assets and scenes reference
      which GUIDs, derivable from metafiles plus serialized component data.
      Decide whether it is computed on demand or maintained incrementally --
      on demand is fine at this project's scale until proven otherwise
- [ ] 115.2 Rename/move with the index consulted; verify GUID references,
      metafiles and cooked caches all survive
- [ ] 115.3 Delete with usage check and a listed-users dialog
- [ ] 115.4 Find-usages surfaced in the assets panel (T0036)
- [ ] 115.5 Hierarchy search field (T0035)
- [ ] 115.6 Autosave/backup and crash recovery (the T0099 note's answer)
- [ ] 115.7 Undo integration where the operation is scene-state-shaped (T0065)

## Notes / findings

- **The GUID invariant is what keeps this ticket small.** Because components
  reference assets by GUID resolved through the pool (T0023, protected by
  T0058), rename is a metafile-and-display concern rather than a
  scan-and-rewrite of every scene. If any code path is found referencing
  assets by path, that is a bug against T0023/T0103 and should be fixed there,
  not worked around here.
- One dependency walk, three consumers: delete-check, find-usages, and
  T0043's export walk. Build it as a reusable query over the asset database,
  or accept writing it twice and reconciling forever.
- Autosave interacts with play mode (T0037): saving on play-entry is the
  cheapest possible insurance and catches exactly the "crashed while testing"
  case that loses work.
