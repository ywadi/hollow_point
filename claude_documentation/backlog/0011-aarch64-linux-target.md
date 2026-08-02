# T0011 — Add an aarch64 Linux target

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Low |
| **Created** | 2026-08-02 |

## Why

Speculative — no stated need for ARM. Recorded because the harness is *almost*
there and the remaining gap is specific and easy to forget: `bootstrap.sh`
already carries aarch64 hashes for zig, cmake and ninja, and Zig cross-compiles
to aarch64 without any extra toolchain. The only real blocker is that the
vendored sysroot is x86_64-only.

Do not start this without an actual reason to.

## Done when

- [ ] `third_party/sysroot/linux-aarch64/` exists with aarch64 stubs
- [ ] `cmake/toolchains/aarch64-linux-gnu.cmake` exists
- [ ] `build.zig` lists the target and `zig build linux-arm64` works
- [ ] The binary is confirmed to run on real ARM hardware

## Subtasks

- [ ] 11.1 Generate the aarch64 sysroot. `tools/mk_linux_sysroot.sh` reads
      symbols from the **host's** libraries, so it cannot produce an aarch64
      sysroot on an x86_64 machine as written — it needs either an ARM host or a
      source of aarch64 `.so` files (e.g. extracted Debian arm64 `.deb`s). This
      is the actual work.
- [ ] 11.2 Add the toolchain file (a near-copy of the x86_64 one; target
      `aarch64-linux-gnu.2.28`)
- [ ] 11.3 Add the entry to `specs` in `build.zig`
- [ ] 11.4 Check `-mavx2`: Diligent adds x86-specific flags for release builds and
      they will not apply on ARM

## Notes / findings

- The symbol *names* in the stubs are architecture-independent; only the ELF
  machine type and data-symbol sizes differ. Generating aarch64 stubs from an
  x86_64 host's symbol lists is therefore *nearly* valid — but data symbol sizes
  can differ between ABIs, so do not take that shortcut without checking.
- `zig dlltool`/`zig cc` handle the aarch64 output fine; the sysroot is the
  whole problem.
