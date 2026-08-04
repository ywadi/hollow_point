# T0043 — Export pipeline and asset relocation

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 8 — Runtime & export |
| **Order** | 770 |
| **Created** | 2026-08-02 |
| **Refs** | T0103 (Blocks this — D13), T0098, T0115, T0116, T0119 |

## Why

The final step that turns a project into something shareable. It depends on every
system beneath it — GUIDs, metafiles, the asset pool, the project format, the
layer and render pipeline — all working correctly.

The genuinely hard part is that asset metafiles point at source files scattered
anywhere on the developer's disk. Export has to find them all, copy them in, and
rewrite the copied metafiles to point at the new local paths, or the exported game
works only on the machine that built it.

## Done when

- [ ] Export produces a folder that runs on a machine that has never seen the project
- [ ] The precompiled runtime executable is copied in, not rebuilt
- [ ] Every referenced asset is located, copied and its metafile rewritten
- [ ] Scenes are cooked to binary; YAML sources are not shipped
- [ ] A missing source asset fails export loudly, before producing a broken build
- [ ] Verified by exporting and running from a different directory

## Subtasks

- [ ] 43.1 Export command in the editor
- [ ] 43.2 Copy the prebuilt runtime plus the required engine DLLs/SOs
- [ ] 43.3 Walk metafiles, resolve every source asset, copy into the export
- [ ] 43.4 Rewrite copied metafiles with paths relative to the export root
- [ ] 43.5 Cook scenes and assets to binary (T0020)
- [ ] 43.6 Fail loudly on anything unresolvable
- [ ] 43.7 Test by exporting, moving the folder, and running it

## Notes / findings

**Rewrite paths to be relative to the export root, not absolute.** An absolute
path that happens to work on the build machine is precisely the bug this step
exists to prevent, and it will not show up until someone else runs it.

**Verify by moving the folder before running it.** Testing in place passes even
when absolute paths leaked, which makes it a worthless test.

`cmake/dist.cmake` already solves the adjacent problem of staging executables
with their DLLs and assets co-located — reuse its rules rather than inventing a
second staging mechanism. Note the Windows requirement that DLLs sit beside the
executable.

Deduplicate assets referenced by more than one scene; a naive per-reference copy
bloats the export and breaks GUID identity if the copies drift.

### Architecture review (2026-08-03) — Linux RUNPATH is a latent export-breaker

The build's RUNPATH currently points at **absolute build-tree directories**
(that is what G6's verification shows: `RUNPATH` containing the two engine
directories under `build/`). That is correct for development and *wrong for an
exported game*: on a player's machine those paths do not exist, Linux does not
search the executable's own directory for shared libraries, and the runtime
will fail to load `GraphicsEngineVk.so` even though it sits right next to the
exe. The exported Linux runtime needs `$ORIGIN`-relative RUNPATH — either
linked that way for the runtime app (`CMAKE_BUILD_RPATH`/`INSTALL_RPATH` with
`$ORIGIN`) or rewritten at export time (patchelf). Windows is unaffected
(exe-adjacent DLL search is native, and dist already co-locates). Add this to
43.7's "move the folder and run it" test on a machine — or at least a chroot —
without the build tree, because testing on the dev machine passes even when
this is broken, which is exactly the class of false pass this ticket warns
about.


### Architecture decision (2026-08-03) — export produces packs, not a loose folder (D13)

The Done-when above describes exporting "a folder that runs on a machine that
has never seen the project". That still holds, but the assets inside it are
**packs** mounted through the VFS (T0103), not loose files.

What this changes here:

- 43.5's cooking step feeds a pack builder rather than a copy
- 43.3/43.4's "copy each asset and rewrite its metafile path" becomes "resolve
  each asset into the pack", and the relative-path discipline in the notes above
  applies to *mount-relative* paths
- Two new capabilities fall out of the same mechanism rather than needing their
  own design: a **patch** is a later-mounted pack that overrides files per path,
  and **assets-only DLC** is a pack that adds them. Neither needs a separate
  feature, which is the reason for choosing a VFS at all

**Code-bearing DLC is out of scope by decision, not oversight** (D14). New
behaviour ships as a game update, which under D12's lockstep already ships the
binary. Assets-only DLC needs no build.

The RUNPATH problem recorded above is unaffected and still needs fixing — it is
about the executable's library search path, not about content.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0098**: the navmesh is a per-scene cooked asset export must ship — its
  own note says "worth remembering in T0043's asset walk". Missing it produces
  an exported game whose NPCs cannot path.
- **T0115.1** needs the same asset-dependency walk this ticket does at export,
  for delete-check and find-usages. Build it once as a reusable query over the
  asset database, "or accept writing it twice and reconciling forever" —
  T0115's words; make the choice deliberately here.
- **T0116.5**: the CSG asset-pipeline decision (source vs cooked vs evaluated
  at load) is taken in T0116 and lands its change here or in T0023 — do not
  let export discover CSG geometry as an unclassified asset type.
- **T0119** owns the step after export on Linux (desktop entry, icon,
  packaging format). Keep the exported layout wrappable by AppImage/Flatpak
  rather than treating a bare folder as the end state.
