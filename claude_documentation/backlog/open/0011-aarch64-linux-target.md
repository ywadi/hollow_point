# T0011 — Add an aarch64 Linux target

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Low |
| **Complexity** | Moderate |
| **Phase** | 14 — Deferred |
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

### Feasibility re-assessed (2026-08-02) — two of my own concerns were wrong

**Correction 1 — `-mavx2` is already arch-guarded.** Subtask 11.4 worried the
x86-specific release flag would break an ARM build. It will not:
`DiligentCore/CMakeLists.txt:527` gates it on `TARGET_CPU STREQUAL "x86_64"`, and
lines 89-91 map `aarch64` to `TARGET_CPU=arm`. Diligent handles this itself as
long as the toolchain file sets `CMAKE_SYSTEM_PROCESSOR aarch64`. Not an issue.

**Correction 2 — the sysroot is much less of a blocker than stated.** Subtask
11.1 called generating an aarch64 sysroot from an x86_64 host "the actual work".
It mostly is not, because the stub generator only needs symbol *names*:

- exported symbol names of libX11/libxcb/libGL are the same C API on every
  architecture, so the x86_64 lists carry over directly
- the only arch-sensitive part is data-symbol sizes, and both x86_64 and aarch64
  Linux are LP64 with the same struct layouts — spot-checked
  (`_XimTransportRec` 64, `_XcmsRegColorSpaces` 144, all pointer-layout driven)
- only the final compile changes: `zig cc -target aarch64-linux-gnu.2.28`

So `tools/mk_linux_sysroot.sh` needs an arch parameter, not a second source of
libraries.

**The genuine blocker is verification, not implementation.** There is no aarch64
hardware here and `qemu-user` is not installed, so the result could be built but
not *run*. By this project's own standard it would close as ⚠️ UNVERIFIED, which
is a weak close for something with no stated need behind it.

Revised estimate: roughly an hour of work — extend the sysroot script, add a
near-copy toolchain file, add one entry to `specs` in `build.zig`. Installing
`qemu-user-static` would make it genuinely verifiable and is the thing to do
first if this is picked up.
