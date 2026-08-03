# T0015 — Window, input and platform layer via SDL3

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Order** | 90 |
| **Created** | 2026-08-02 |

## Why

The Application needs a window to construct and pump each frame, and the loop
needs a real exit condition. Rather than write an `IWindow` abstraction over
GLFW, build on `DiligentTools/NativeApp`, which already provides `AppBase` plus
entry points for Win32, Linux (X11 and XCB), macOS, Android, iOS, UWP and
Emscripten — and is already cross-compiling in this harness.

This is engine tooling (`DiligentTools`), not the samples framework
(`DiligentSamples/SampleBase`). The distinction matters: SampleBase carries
demo-shaped assumptions we would have to unpick later.

## Done when

- [ ] A window opens on Linux and closes cleanly
- [ ] The same code path builds for Windows (running it needs wine or T0004)
- [ ] Window resize is observable by the engine
- [ ] Raw input reaches the engine, ready to become events in T0018

## Subtasks

- [ ] 15.1 Study `DiligentTools/NativeApp/include/AppBase.hpp` and the Linux and
      Win32 entry points before designing anything around them
- [ ] 15.2 Decide how `Application` and `AppBase` relate — inherit, or own an
      instance (see notes)
- [ ] 15.3 Wire window creation, per-frame pump and should-close
- [ ] 15.4 Surface resize and input to the engine
- [ ] 15.5 Confirm both targets build

## Notes / findings

**Known caveat, worth designing around rather than discovering later:** the OS
message pump runs on the same thread as game logic and rendering, so dragging the
window title bar stalls the frame loop. This is standard for GLFW/NativeApp-style
setups. The fix is moving the pump to its own thread with queue-based input
handoff. Not required now, but do not build anything that *assumes* pump and
logic share a thread.

Linux has both X11 and XCB paths in NativeApp; the vendored sysroot provides
stubs for both, so either works.

### Architecture review (2026-08-03) — NativeApp has no input surface; translation is ours

Checked `AppBase.hpp` directly: it exposes `Update/Render/Present/WindowResize`
and **nothing about input**. The platform bases hand over *raw* OS events —
`HandleXEvent`/`HandleXCBEvent` on Linux, `HandleWin32Message` on Win32 — and
Diligent's actual input translation (`InputController`) lives in
**DiligentSamples/SampleBase**, which this ticket rightly refuses to depend on.
Consequence: translating raw Win32 messages and X11/XCB keycodes (keysyms,
modifiers, mouse buttons/wheel, text input) into engine events is *our* code,
on two platforms, and belongs to 15.4/T0018.5 explicitly rather than
implicitly. `Diligent-Imgui`'s per-platform impls feed **ImGui only** — they do
not feed the engine. Not a blocker, but "Moderate" is only honest if this work
is known to be in scope.

### Second review pass (2026-08-03) — display modes have no owner

No ticket owns fullscreen/borderless/windowed switching, monitor enumeration,
resolution changes at runtime, or DPI awareness — yet T0078's game options
list "resolution" as a player-facing setting, so *something* must implement
applying it. That something is this platform layer (window/display control is
per-OS code; the swapchain resize path from T0025 already handles the GPU
side). Scope it here: borderless-fullscreen toggle + runtime resolution change
+ basic DPI handling are the needed floor; exclusive fullscreen and
multi-monitor selection can wait for evidence. `AppBase` has a
`HOT_KEY_FLAG_ALLOW_FULL_SCREEN_SWITCH` hint but the actual mode work is in
the per-platform bases — check what they provide before writing it.


### Architecture decision (2026-08-03) — SDL3, not NativeApp (D16)

**This ticket's premise is superseded.** It framed the choice as "rather than
write an `IWindow` abstraction over GLFW, build on `DiligentTools/NativeApp`" —
the wrong pair. The real choice was NativeApp versus a platform library, and
**Diligent attaches to a window created by anything**: `LinuxNativeWindow` takes
an X11 `WindowId` and `Display*`, the Win32 form takes an `HWND`. Device and
swap-chain creation are decoupled from window creation, so using SDL costs
nothing in engine integration.

The layer is **SDL3**. What changes here:

- 15.1 becomes: study SDL3's window, event and gamepad APIs. `AppBase` is not
  involved
- 15.2's "how do `Application` and `AppBase` relate" dissolves — `Application`
  owns an SDL window and pumps SDL events. There is no framework base class to
  inherit from, which is simpler than either original option
- Window creation, the per-frame pump, should-close, resize and raw input all
  come from SDL rather than being surfaced by hand
- **Add: clipboard, DPI/display scale, monitor enumeration and
  fullscreen/borderless.** None were in this ticket and all are needed by the
  editor; SDL provides them, so they are configuration rather than code
- **Add: confirm what SDL resolves at link time versus `dlopen` on Linux.** SDL
  is understood to load most X11 dependencies at runtime, which matters because
  the hermetic-Linux property (D4) is a *verified* claim in
  `05-verification-status.md` and must not quietly become false

**The threading caveat in the notes above still applies unchanged.** SDL's event
pump has the same property: it runs on the thread that owns the window, so
dragging the title bar stalls the loop unless the pump is moved off the frame
thread. Do not build anything that assumes pump and logic share a thread.

**Pin SDL3, and ignore SDL2 material.** SDL3 renamed a great deal —
`SDL_GameController` became `SDL_Gamepad` among much else — so SDL2 examples and
answers will mislead.
