# T0119 — Wayland support and Linux distribution

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 8 — Runtime & export |
| **Order** | 755 |
| **Created** | 2026-08-03 |
| **Refs** | T0015, T0043, T0011, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D4, D16 |

## Why

Two separate questions that both answer "will this run on the machine someone
actually has", and neither has an owner.

**Wayland.** The vendored sysroot is X11 only (D4). SDL3 supports both backends
and picks one at run time, so today a Linux build reaches a Wayland desktop
through **XWayland**, which every Wayland compositor ships. That works — this is
a fidelity gap, not a compatibility one, and it is worth being precise about
that because "we do not support Wayland" reads far more alarming than it is.

What XWayland actually costs: **fractional scaling is visibly blurry** at 125%
and 150%, which is a common laptop configuration; no HDR; per-monitor DPI is
approximated; and some input paths take a longer route. GNOME, KDE and SteamOS
now all default to Wayland, so this is most users eventually rather than a
minority.

**Distribution.** T0043 exports *a folder*. On Windows that is nearly enough. On
Linux it is not: there is no desktop entry, no icon, no launcher, and no answer
to "where does this go". Grepping for `appimage`, `flatpak`, `steam runtime`,
`.deb` and `installer` across every ticket returns nothing — the only hit is
T0011, which is about the aarch64 *target*. This is the same shape as the other
gaps found in this project: T0043 looks complete, and the step immediately after
it has no owner.

## Done when

- [ ] A decision on Wayland: supported natively, or XWayland accepted — recorded
      with its consequences, not left implicit
- [ ] If native: the sysroot carries Wayland headers and stubs, and a build runs
      on a Wayland session without XWayland
- [ ] Fractional scaling is correct on whichever path is chosen, or the
      limitation is written down
- [ ] A decision on how a Linux build is distributed and installed
- [ ] An exported build runs on a distribution that is not the one it was built
      on, verified rather than assumed

## Subtasks

- [ ] 119.1 Confirm what SDL needs for Wayland at build time. It `dlopen`s the
      X11 libraries (`SDL_X11_SHARED`), and the Wayland path is expected to work
      the same way — but T0015 found SDL's *configure* still requires each
      library to be findable, which is what forced eight new sysroot stubs.
      Assume nothing here; the pattern already surprised us once
- [ ] 119.2 Extend `tools/mk_linux_sysroot.sh` for Wayland if 119.1 says so.
      `wayland-client`, `wayland-egl`, `wayland-cursor`, `xkbcommon` — note
      `xkbcommon` was the one header missing when SDL was first vendored
- [ ] 119.3 Wayland also needs `wayland-scanner` to generate protocol code at
      build time. That is a **host tool**, and the pinned toolchain (D5) does not
      ship one — decide whether to vendor generated sources or require the tool
- [ ] 119.4 Verify on a real Wayland session, and on XWayland, and compare
      fractional scaling
- [ ] 119.5 Choose a distribution format: AppImage, Flatpak, a Steam depot, or a
      plain tarball with a launcher script. They are not exclusive
- [ ] 119.6 Desktop integration — `.desktop` entry, icon, MIME associations if
      the editor opens project files
- [ ] 119.7 Verify an exported build on a different distribution from the build
      host. The glibc pin (2.28) should make this work; "should" is why it needs
      testing

## Notes / findings

**Steam Deck is the case that makes both of these concrete.** SteamOS runs
gamescope, a Wayland compositor, and the Deck is x86_64 with a glibc far newer
than our 2.28 floor — so a native Linux build should reach it through XWayland
today. SDL is also what Valve builds the Deck's input story around, so the
gamepad and Steam Input work without special handling (D16). The remaining
questions are exactly the two above: scaling fidelity, and how the thing is
delivered. Worth testing on real hardware before claiming Deck support.

**Do not conflate the two halves.** Wayland is a display-server question and
distribution is a packaging question; they land in the same ticket because both
answer "runs on the user's machine" and both were unowned, not because they
share an implementation. Split this if either grows.

**The glibc pin is the reason this is tractable at all.** Pinning 2.28 (D4) is
what makes a binary built here run on distributions much newer, and it is
already verified. Most Linux distribution pain comes from *not* having done
that, so the hard part is behind us.

**Audio needs no work.** SDL found ALSA, PipeWire and PulseAudio, all with
`_SHARED` set, so they are `dlopen`ed exactly like X11 and need nothing in the
sysroot. Recorded so nobody goes looking.
