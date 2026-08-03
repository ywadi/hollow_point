# T0103 — Virtual filesystem and content packs

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Order** | 180 |
| **Created** | 2026-08-03 |
| **Blocks** | T0023, T0043, T0083 |
| **Refs** | [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D13 |

## Why

Nothing in the backlog described where bytes come from. T0043 exported a folder
of loose files, T0023 said nothing about archives, and no ticket mentioned
patching or DLC. That is a structural gap rather than a missing feature: if
`AssetManager` is written against `std::filesystem` — which is the obvious way
to write it — then packs, patches and DLC each become a rewrite of every read
site instead of a mount.

One mechanism gives all three. Every read goes through a virtual filesystem;
directories and archives are mounted into a single tree; mount order decides who
wins when the same path exists twice. Shipping assets in one file, patching
(ship a later pack that overrides), and DLC (ship a pack that adds) stop being
three features and become one. This is what Godot does with `.pck` and
`load_resource_pack`, and what Quake 3 did before it.

**The ordering constraint is the point of this ticket.** It has to land before
T0023 hardens, not after.

## Done when

- [ ] Every asset read in engine code goes through the VFS; no `std::filesystem`
      or `fopen` on asset paths, checkable by grep
- [ ] A loose directory and an archive mount into the same tree and a path
      resolves identically from either
- [ ] A later mount overrides an earlier one per file — the patch/DLC mechanism,
      proven by a test that mounts two archives with a shared path
- [ ] Dev (loose files) and shipped (packs) use the **same** code path, differing
      only in what is mounted
- [ ] Writes are confined to a designated write directory and cannot escape it
- [ ] Builds and runs on both targets

## Subtasks

- [ ] 103.1 Vendor PhysicsFS as a submodule under `third_party/`, wired like the
      others — no install rules, no its own tests, consumed as a target
- [ ] 103.2 A thin engine wrapper over it. Not a passthrough: PhysicsFS is a C
      library with global state, and the engine should expose a small
      RAII/`std::span`-shaped surface rather than let `PHYSFS_*` calls spread
      through the codebase. This is also the seam that makes it replaceable
- [ ] 103.3 Mount policy: what is mounted, in what order, for editor vs runtime.
      Write it down — override order *is* the DLC semantics and must not be
      incidental
- [ ] 103.4 Write directory for saves and logs, per platform
- [ ] 103.5 Tests: identical resolution from loose dir and archive; later mount
      overrides earlier; a write cannot escape the write directory; a missing
      file fails the same way from both sources
- [ ] 103.6 Decide the shipping archive format (ZIP is the obvious default, and
      PhysicsFS reads it without a custom format to maintain)
- [ ] 103.7 Spike concurrent reads before any threaded loader exists (see below)

## Notes / findings

**PhysicsFS is file I/O, not an asset database.** It owns *where bytes come
from*; GUIDs, metafiles, reference counting and hot reload (T0023, T0058) stay
ours. Keep that line clean — the temptation will be to let asset identity leak
into path handling.

**Concurrency is the open risk.** PhysicsFS is a C library with global init
state and its guarantees for concurrent reads were not confirmed when D13 was
taken. The job system (T0026) and threading model (T0050) will want async asset
loading, and discovering a global lock at that point is expensive. 103.7 exists
to find out early; the answer may be "reads are serialised behind one mutex and
that is fine", but it should be a measurement, not an assumption.

**`PHYSFS_setRoot` mounts a subset of an archive**, which is useful if a DLC
pack should only contribute part of its contents.

**Do not invent a pack format.** The pull toward a bespoke format with an
optimised index is strong and premature. ZIP is readable by every tool, already
supported, and can be replaced later behind 103.2's seam if profiling ever
justifies it.
