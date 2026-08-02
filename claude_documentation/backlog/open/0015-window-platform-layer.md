# T0015 — Window and platform layer via DiligentTools NativeApp

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
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
