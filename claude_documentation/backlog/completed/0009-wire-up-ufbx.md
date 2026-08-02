# T0009 — Wire up `ufbx`, or drop it

| | |
|---|---|
| **Status** | ❌ SUPERSEDED |
| **Priority** | Low |
| **Phase** | 7 — Content pipeline |
| **Created** | 2026-08-02 |

## Why

`third_party/ufbx` (v0.23.0) is in the tree and **referenced by no build rule at
all**. It predates this work — it was already staged when the harness started.

Unwired dependencies are a trap: they look available, so someone assumes FBX
loading exists, and they silently rot against engine and toolchain updates
because nothing compiles them.

Either it is wanted, in which case it should build, or it is not, in which case
it should go. It should not stay in the current state.

## Done when

Either:
- [ ] `ufbx` builds as a target for both Linux and Windows, and something calls it

Or:
- [ ] It is removed from `third_party/` and from `.gitmodules`, and
      `01-project-overview.md` updated

## Subtasks

- [ ] 9.1 Ask whether FBX import is actually wanted — **needs the user**
- [ ] 9.2 If yes: add a small `CMakeLists.txt` for it (ufbx ships none — it is a
      single `ufbx.c`/`ufbx.h` pair, so this is a one-file static library)
- [ ] 9.3 Confirm it cross-compiles; it is plain C99 and should be uneventful
- [ ] 9.4 Decide how it relates to ozz's animation import (see notes)

## Notes / findings

- ufbx is a single-file C library. Adding it is genuinely a few lines — the
  effort is in deciding whether it is the right tool, not in the wiring.
- **Overlaps with T0005.** ozz has its own FBX pipeline, disabled because it
  needs the proprietary FBX SDK. ufbx is an independent, permissively licensed
  FBX parser with no SDK dependency, so "ufbx for mesh/skeleton import, feeding
  ozz's runtime format" is a plausible pipeline that avoids the FBX SDK entirely.
  Decide T0005 and T0009 together.
- `third_party/dxc` (v1.8.2407, 34 MB of binaries) is in the same unwired state.
  Diligent has its own bundled DXC support, so the standalone copy may simply be
  redundant — worth checking before it is carried around forever.

### Superseded (2026-08-02)

Replaced by **T0038** (ufbx -> glTF converter tool).

The open question in this ticket -- ufbx versus ozz's FBX pipeline -- was settled
differently than either option: glTF becomes the engine's source of truth via
Diligent's existing GLTFLoader, and ufbx is repurposed as the *input* side of an
offline FBX -> glTF converter. That removes the Blender round-trip without
duplicating the mesh/material import Diligent already provides.

So ufbx is neither wired as a runtime loader nor dropped. It becomes a host-only
tool dependency, which also keeps it out of the shipped binary.

Left here rather than deleted: the analysis in it is still accurate and the
successor tickets refer back to it.
