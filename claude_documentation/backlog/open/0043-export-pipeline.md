# T0043 — Export pipeline and asset relocation

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 8 — Runtime & export |
| **Created** | 2026-08-02 |

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
