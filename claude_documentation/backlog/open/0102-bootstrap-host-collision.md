# T0102 — `bootstrap.sh` and `bootstrap.ps1` destroy each other's toolchain

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 1 — Harden the build |
| **Created** | 2026-08-03 |
| **Found by** | T0004 |
| **Refs** | [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D5 |

## Why

Both bootstrap scripts install into `.harness/<tool>/<version>/` with **no host
discriminator**, and both delete the destination before extracting. So on a
machine that uses both — a Windows box with WSL, which is how this project is
actually developed — running one bootstrap silently destroys the other's
toolchain.

`bootstrap.sh:94`:

```sh
rm -rf "$ZIG_DIR" "$ROOT/.harness/zig/zig-$ZIG_SLUG-$ZIG_VERSION"
```

`$ZIG_DIR` is `.harness/zig/0.16.0` — precisely where `bootstrap.ps1` put
`zig.exe`. The same applies to cmake (`bootstrap.sh:111`) and ninja
(`bootstrap.sh:129`), and to `Install-Archive` in `bootstrap.ps1`, which does
`Remove-Item -Recurse -Force` on the same paths from the other direction.

Neither script notices: each checks for *its own* binary name (`zig` vs
`zig.exe`), finds it missing, and reinstalls over the top.

This is not hypothetical. During T0012 a Linux zig was needed for comparison
against the Windows one, and running `bootstrap.sh` would have wiped the
Windows toolchain mid-task. It was avoided only by reading the script first;
the Linux zig went into a scratch directory instead.

## Done when

- [ ] Running one bootstrap leaves the other host's toolchain intact
- [ ] Both hosts can build from the same working tree without re-bootstrapping
      each time they switch
- [ ] `build.zig`'s `harnessTool()` finds the right toolchain for the host it
      is running on
- [ ] `BUILDING.md` says what happens on a dual-host machine
- [ ] The download cache is still shared where it is safe to (`\.harness/dl/`
      holds host-specific archives but they do not collide by name)

## Subtasks

- [ ] 102.1 Decide the layout. Options: key by host
      (`.harness/zig/0.16.0-linux/`), key by a zig-style slug
      (`.harness/zig/x86_64-linux-0.16.0/`, which matches the upstream archive
      naming already used in both scripts), or keep the shared path and have
      each script refuse to delete a foreign install
- [ ] 102.2 Update `bootstrap.sh` and `bootstrap.ps1` together — they are
      required to stay in sync, and this is exactly the kind of change where
      they drift
- [ ] 102.3 Update `harnessTool()` in `build.zig`, which currently builds the
      path from `pinned_*_version` alone and appends `.exe` on Windows
- [ ] 102.4 Update the paths quoted in `BUILDING.md` and in both scripts'
      "next:" hints
- [ ] 102.5 Check the CI cache keys in `.github/workflows/ci.yml` still
      distinguish the two (`harness-linux-*` / `harness-windows-*` cache
      *entries* are already separate, but they populate the same path)

## Notes / findings

**Prefer keying by host over refusing to delete.** A refusal turns a silent
wipe into a confusing error on a machine that legitimately wants both. Keying
by host makes the dual-host case simply work, which is the case this project
actually has.

**`harnessTool()` falls back to PATH when the pinned tool is absent**
(`build.zig:179-184`), so a half-migrated layout does not fail loudly — it
quietly builds with whatever cmake the system has. That is worse than failing,
and is a reason to get 102.3 right in the same change rather than after.

**`.harness/dl/` is fine as-is.** The archives are named for their host
(`zig-x86_64-linux-0.16.0.tar.xz` vs `zig-x86_64-windows-0.16.0.zip`), so the
download cache does not collide and can stay shared. Only the *extracted*
directories collide.

**Related, not the same bug:** the two scripts must also keep their pinned
versions and checksums in sync with each other and with `build.zig`. Nothing
enforces that today. Worth considering in 102.2 whether a single pins file both
scripts read would be better than three copies, though shell/PowerShell/Zig all
reading one format is its own small problem.
