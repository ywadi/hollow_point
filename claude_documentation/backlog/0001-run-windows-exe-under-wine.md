# T0001 — Run the Windows executable under wine

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Created** | 2026-08-02 |
| **Refs** | [../05-verification-status.md](../05-verification-status.md) |

## Why

The Windows cross-build succeeds (1105/1105, exit 0) and produces
`ImGuiProbe.exe` as `PE32+ executable (console) x86-64`. That proves it *links
and has the right format* — nothing more. No Windows binary from this harness has
ever been executed.

Running it would close the largest open verification gap, and would exercise
several fixes that currently have **zero runtime evidence**: the generated
capitalised import libraries (G3), the MSVC-named `.lib` copies (G4), and the
`zig rc` compiled resource.

`wine` is present at `/usr/bin/wine`, so this is cheap to attempt.

## Done when

- [ ] `ImGuiProbe.exe` starts under wine without a loader error
- [ ] It renders frames and `HP_PROBE_EXIT_FRAMES=120` prints the verdict line
- [ ] The verdict reports `ImGui 1.92.9b, docking ON`, confirming the docking swap
      holds on Windows too
- [ ] Findings recorded below either way — including "wine cannot do this", which
      is a legitimate outcome

## Subtasks

- [ ] 1.1 Stage the exe next to its DLLs (`zig build dist -Dtarget=windows`, or
      copy `GraphicsEngineOpenGL_64r.dll` / `GraphicsEngineVk_64r.dll` beside it).
      Windows resolves DLLs from the executable's directory — this will fail
      confusingly otherwise.
- [ ] 1.2 Copy the assets (`polygon.*`, `DGLogo*.png`) alongside; they are loaded
      relative to the working directory.
- [ ] 1.3 Run under Xvfb: `HP_PROBE_EXIT_FRAMES=120 xvfb-run -n 99 wine ImGuiProbe.exe --mode gl`
- [ ] 1.4 If GL fails under wine, try `--mode vk`, then plain `wine` on the host
      display.

## Notes / findings

- Expect noise from wine itself (missing `mscoree`, `winemenubuilder`) — that is
  normal and not a failure of the build.
- wine's OpenGL goes through the host GL stack; under Xvfb that means llvmpipe
  again, so this tests the *binary*, not the driver path.
- If wine turns out to be a dead end, say so plainly and leave the gap open
  rather than reporting a false pass. A real Windows machine is the fallback, and
  that is also what T0004 needs.
