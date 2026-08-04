# T0103 — Virtual filesystem and content packs

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
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

- [~] Every asset read in engine code goes through the VFS; no `std::filesystem`
      or `fopen` on asset paths, checkable by grep
- [x] A loose directory and an archive mount into the same tree and a path
      resolves identically from either
- [x] A later mount overrides an earlier one per file — the patch/DLC mechanism,
      proven by a test that mounts two archives with a shared path
- [x] Dev (loose files) and shipped (packs) use the **same** code path, differing
      only in what is mounted
- [x] Writes are confined to a designated write directory and cannot escape it
- [x] Builds and runs on both targets

## Subtasks

- [x] 103.1 Vendor PhysicsFS as a submodule under `third_party/`, wired like the
      others — no install rules, no its own tests, consumed as a target
- [x] 103.2 A thin engine wrapper over it. Not a passthrough: PhysicsFS is a C
      library with global state, and the engine should expose a small
      RAII/`std::span`-shaped surface rather than let `PHYSFS_*` calls spread
      through the codebase. This is also the seam that makes it replaceable
- [~] 103.3 Mount policy: what is mounted, in what order, for editor vs runtime.
      Write it down — override order *is* the DLC semantics and must not be
      incidental
- [x] 103.4 Write directory for saves and logs, per platform
- [x] 103.5 Tests: identical resolution from loose dir and archive; later mount
      overrides earlier; a write cannot escape the write directory; a missing
      file fails the same way from both sources
- [x] 103.6 Decide the shipping archive format (ZIP is the obvious default, and
      PhysicsFS reads it without a custom format to maintain)
- [x] 103.7 Spike concurrent reads before any threaded loader exists (see below)

## Notes / findings

## Built — 2026-08-05

PhysicsFS pinned to **release-3.2.0**, static and PRIVATE, reached only through
`hp/Vfs.hpp`. No `PHYSFS_` type appears in a public header, so the library is
replaceable behind that seam and its global state has exactly one door — which
matters more here than for SDL, because the engine is a shared library that the
editor, the runtime and every gameplay module all link (D12).

### The trap: PhysicsFS disables RPATH for the entire build

`third_party/physfs/CMakeLists.txt:36` runs

```cmake
set(CMAKE_SKIP_RPATH ON CACHE BOOL "Skip RPATH" FORCE)
```

under any GCC or Clang. Not scoped to its own targets, not optional, and
**FORCE**, so it lands in our cache and strips RPATH from every target in the
build — engine, editor, runtime, every test binary.

**The symptom looks nothing like a CMake option.** Everything links cleanly and
then dies at startup with `libhp_engine.so: cannot open shared object file` —
which reads as a missing file, and the file is sitting right beside the binary.
Diligent's backends fail identically one step later. It survives a clean
rebuild, because the cause is in the source rather than the tree, and that is
what makes it expensive: the instinct is to distrust the build tree first.

Fixed by resetting the cache entry *and* the directory-scope variable
immediately after `add_subdirectory(third_party/physfs)`. Both are needed: the
FORCE wrote the cache, so a plain `set` alone is shadowed by it.

This is a new instance of a known class — "the Linux build is hermetic, and a
dependency's CMake that reaches for global state will break it". Worth
remembering the next time a vendored library is added.

### 103.6 — ZIP, and only ZIP

PhysicsFS ships readers for fifteen archive formats. Fourteen are for other
people's games — Quake PAK, Descent HOG, Gothic VDF, Resident Evil 3 ROFS — and
each is parser code compiled into the engine and reachable by any file a player
can place in a mount directory. All off; ZIP on. One line to reverse if that is
ever wrong.

### 103.7 — concurrency: measured, and correct

The ticket asked for a measurement rather than an assumption, and this is it:
**8 threads x 40 reads over 16 overlapping archive entries, 0 failures, 0
mismatches**, every thread reading every file so the same entry is decoded
concurrently.

What that proves is **correctness, not scalability**. Whether reads serialise
behind one internal lock is not answered and does not need to be yet — "reads
are serialised and that is fine" remains an acceptable outcome for T0026 and
T0050, provided it is a known one. What is now ruled out is corruption, which
was the outcome that would have been discovered late and expensively.

### Tests

`tests/integration/vfs_test.cpp`, 17 cases. **The archives are built byte by
byte by a minimal ZIP writer in the test file** rather than checked in: a binary
fixture is something nobody can review, nobody updates, and which silently
encodes whatever tool produced it. Stored, not deflated — deflating would be
testing zlib rather than the mount path.

The case that matters most is `a later mount overrides an earlier one`, because
that is the patch and DLC mechanism (D13) rather than a filesystem nicety. It
asserts override *per file*: a file the patch does not carry still resolves to
the base pack, which is what makes a patch a patch rather than a replacement.
`resolvedSource` is asserted alongside it, since when this goes wrong in the
field the only useful question is which copy won.

## 103.3 — mount policy, written down

The subtask asks for the policy to be recorded, and here it is. **The wiring is
not built**, because nothing loads assets yet; T0023 is what will apply it.

Search order, first match wins. Later entries are fallbacks:

| Order | Mount | Present in |
|---|---|---|
| 1 | Write directory (saves, user config) | both |
| 2 | Loose working directories, most recent first | editor only |
| 3 | Patch packs, newest first | both |
| 4 | DLC packs | both |
| 5 | Base game pack(s) | both |

- **The editor prepends loose directories** so an edited file beats the packed
  copy — that is the whole dev workflow, and it is the same `MountOrder::Prepend`
  a patch uses rather than a separate mechanism.
- **A patch prepends; DLC appends.** A patch exists to override, and DLC exists
  to add — a DLC pack that silently shadowed base content would be a bug that
  only shows up as the wrong art on a base-game level.
- **The runtime never mounts a loose directory** in a shipped build, so a stray
  file beside the executable cannot change behaviour.

## Not done

- **"Every asset read goes through the VFS" is not yet meaningful**, because
  there are no asset reads. The rule is stated in the header and is checkable by
  grep; enforcing it is T0023's problem, and its ticket now says so.
- **103.3 is written, not wired.** No editor or runtime code mounts anything.
- **No streaming reader.** `read` is whole-file, which is what every planned
  asset loader wants. Audio (T0052) is the likely first caller to need
  streaming, and it can be added beside `read` without disturbing it.
- **Write-directory escape is tested, not audited.** The refusal covers absolute
  paths, drive letters, backslashes and `..` segments, and PhysicsFS refuses
  independently. No adversarial review of the pair has been done.


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

### Note (2026-08-03) -- 103.4's layout should assume a cloud-sync root may wrap it

From the design-gap survey (`documentation/07-design-gaps.md`, item 10):
no store platform is committed to, and none needs to be for this to matter:
cloud saves constrain the write directory, and the constraint is free to honour
now whether or not one is ever added.
Cloud sync wants the synced directory **stable, small, and free of non-save
junk**. Crash dumps land "beside save data" per T0099.3 -- fine, but choose
103.4's layout knowing a sync root may one day wrap part of it: separate
`saves/` from `logs/` and `crash/` under the write directory, so a future
sync configuration can target saves alone. Costs nothing today; re-homing
files after players have saves is the expensive version.
