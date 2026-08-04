# T0015 — Window, input and platform layer via SDL3

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Order** | 90 |
| **Created** | 2026-08-02 |
| **Refs** | [../completed/0129-display-modes-and-window-control.md](../completed/0129-display-modes-and-window-control.md) |

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

- [x] A window opens on Linux and closes cleanly
- [x] The same code path builds for Windows — **and runs**, opening a window under wine with SDL's `windows` video driver
- [x] Window resize is observable — `onResize(width, height)` in *pixels*, which is what a swap chain is sized in
- [ ] Raw input reaches the engine — **only close and resize are surfaced today.** SDL delivers keyboard, mouse and gamepad into the same pump, but turning them into engine events is T0018's job and inventing a shape here would be the wrong thing to migrate from

## Subtasks

- [x] 15.1 Study `DiligentTools/NativeApp/include/AppBase.hpp` and the Linux and
      Win32 entry points before designing anything around them
- [x] 15.2 Decide how `Application` and `AppBase` relate — inherit, or own an
      instance (see notes)
- [x] 15.3 Wire window creation, per-frame pump and should-close
- [x] 15.4 Surface resize to the engine — input beyond close/resize deferred to T0018, see above
- [x] 15.5 Confirm both targets build

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


## Findings

**SDL's auto-detection scans the host, and that silently breaks the hermetic
build.** This is the finding worth carrying forward. Left to detect, SDL's
pkg-config probes found the build machine's Wayland, KMSDRM, PulseAudio, sndio,
EGL, fribidi and libthai, enabled all of them, and the link then failed on ten
libraries the vendored sysroot does not carry:

```
error: unable to find dynamic system library 'wayland-client' ...
error: unable to find dynamic system library 'pulse' ...
error: unable to find dynamic system library 'fribidi' ...
```

A build that succeeds only on a machine with the right `-dev` packages is
precisely what D4's sysroot exists to prevent, and it could never have worked
cross-compiling from a Windows host. **Every backend is now chosen explicitly in
the root `CMakeLists.txt`** — on for what the sysroot backs, off for everything
else — with a comment saying why. Turning one back on means vendoring its
headers and stubs first.

**D16's one recorded unknown is settled: SDL `dlopen`s X11.** `SDL_X11_SHARED`
is on by default, so the X11 libraries are loaded at run time rather than
linked. But its *configure* step still requires each to be **findable**, which
is what forced the sysroot from 8 stub libraries to 16 — Xcursor, Xrandr, Xi,
Xinerama, Xext, Xfixes, Xrender, Xss, Xtst. The headers were all already
vendored; only the link stubs were missing. The hermetic property holds: nothing
resolves from `/usr`.

**The public header exposes native handles as `void*` and `uint64_t`**, not
platform types. `<hp/Window.hpp>` is included by engine code that has no
business seeing `Xlib.h`, and `windows.h` in particular poisons a translation
unit with `min`, `max` and `CreateWindow` macros. It also keeps SDL a *private*
link of the engine, so gameplay cannot reach SDL behind the engine's back —
which matters because SDL is a C API and D12's boundary rules would not cover it.

**Resize reports pixels, not logical size.** `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`
and `SDL_GetWindowSizeInPixels`, because a swap chain is sized in pixels and the
two differ on a scaled display. Getting this wrong produces a viewport that is
subtly the wrong size only on high-DPI machines, which is a miserable bug to
find.

**SDL init is refcounted rather than a static.** `SDL_Quit` at
static-destruction time runs after `main` returns, which is a poor place to be
tearing down a display connection.

## Evidence

Linux, with the window confirmed present on screen by an independent tool
rather than by the program's own claim:

```
$ ./build/linux-x86_64-release/apps/editor/hp_editor &
$ xdotool search --name "HollowPoint"
48234545                                  <- a real mapped window

[info ] app: starting HollowPoint Editor
[info ] window: SDL 3.4.8, video driver 'x11'
[info ] window: window 'HollowPoint Editor' 1280x720
[info ] editor: resized to 1280x720
[info ] app: window close requested          <- closing the window exits cleanly
[info ] editor: editor shutting down
[info ] app: HollowPoint Editor ran 1080464 frame(s) in 2.999s, exit 0
```

Windows, under wine, opening a real window through SDL's `windows` driver:

```
$ wine build/windows-x86_64-release/apps/runtime/hp_runtime.exe
[info ] app: starting HollowPoint Runtime
[info ] window: SDL 3.4.8, video driver 'windows'
[info ] window: window 'HollowPoint' 1280x720
[info ] app: HollowPoint Runtime ran 3 frame(s) in 0.251s, exit 0
```

**1,080,464 frames in three seconds** is worth staring at: the loop is
completely unthrottled, which is exactly the uncapped behaviour T0110
(presentation, vsync and frame pacing) exists to fix, demonstrating itself
unprompted. It is a good argument for T0110 being ordered ahead of T0025.

## Not done

**Wayland** — deliberately not attempted, and now owned by
[T0119](../open/0119-wayland-and-linux-distribution.md). Three blockers made it
the wrong thing to bolt on here: this machine runs an X11 session so it could
not be verified; SDL finds Wayland through pkg-config against `/usr`, breaking
the hermetic property; and `wayland-scanner` would become an unpinned host
dependency, which is what D5 exists to prevent. XWayland means Wayland desktops
are already reached today.

**Input beyond close and resize** is T0018's. **The threading caveat in the
notes above is unaddressed and still true** — SDL's pump runs on the thread that
owns the window, so dragging the title bar stalls the loop. Nothing built here
assumes pump and logic share a thread.

### Where the display-mode work went (2026-08-04)

This ticket's "display modes have no owner" note scoped a floor —
borderless-fullscreen, runtime resolution change, basic DPI — and a later note
added monitor enumeration and clipboard. **None of it was built, and the "Not
done" section does not mention it**, so it was named twice as unowned and needed
and then carried nowhere.

It is now **T0129**, which quotes this ticket's own scoping rather than
re-deriving it. The gap surfaced when someone asked whether the engine could go
fullscreen: the answer was no, and nothing owned making it yes.
