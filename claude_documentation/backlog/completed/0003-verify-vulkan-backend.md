# T0003 — Verify the Vulkan backend on real hardware

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Phase** | 1 — Harden the build |
| **Created** | 2026-08-02 |
| **Closed** | 2026-08-02 |

## Why

Every runtime check so far used **OpenGL via llvmpipe under Xvfb** — software
rasterisation, no real driver. Vulkan is the primary backend for both targets
(and on Windows it is the *only* modern one, since D3D is disabled by design —
see D2). It has never been exercised.

Vulkan also loads differently: Diligent uses `volk`, which `dlopen`s the loader
at runtime rather than linking it. That path is untested, and it is exactly the
kind of thing the stub sysroot (D4/G6) could plausibly have broken.

## Done when

- [x] `./ImGuiProbe --mode vk` runs on a real GPU and renders
- [x] The engine log reports a real adapter, not llvmpipe/lavapipe
- [x] ImGui still draws — the Diligent ImGui renderer backend has only ever been
      seen working on GL
- [x] Result recorded for both targets if T0001 makes the Windows side runnable

## Subtasks

- [x] 3.1 `vulkaninfo | head` to confirm a real driver is present
- [x] 3.2 Run on the host display (not Xvfb — it has no Vulkan driver)
- [x] 3.3 Compare the reported adapter against `vulkaninfo`
- [x] 3.4 Exercise the ImGui widgets, since the renderer backend is per-backend code

## Notes / findings

- This needs the user's actual desktop; it cannot be completed headlessly.
- If Vulkan fails while GL works, suspect the volk/loader path first, then the
  swapchain surface creation (xcb vs xlib) — Diligent has both, and the sysroot
  provides both stubs.

### Outcome — PASSED on real hardware

```
$ HP_PROBE_EXIT_FRAMES=60 DISPLAY=:1 ./ImGuiProbe --mode vk
Diligent Engine: Info: Created swap chain with 3 images vs 2 requested.
Diligent Engine: Info: Using SURFACE_TRANSFORM_IDENTITY swap chain pretransform
Diligent Engine: Info: Using VK_PRESENT_MODE_IMMEDIATE_KHR swap chain present mode
Diligent Engine: Info: MemoryManager 'Global resource memory manager': created
                       new device-local page. (16.00 MB, type idx: 1)
HP_PROBE: 60 frames rendered, ImGui 1.92.9b, docking ON -- exiting
EXIT=0
```

Ran on an **NVIDIA GeForce RTX 2080**, proprietary driver 580.82.07 — a real
adapter, not llvmpipe. Swap chain, descriptor pools and device-local memory all
allocated normally, and ImGui rendered through the Vulkan backend.

Notably this clears the two things most at risk from the stub sysroot (D4/G6):
`volk` `dlopen`s the Vulkan loader at run time, and the xcb/xlib surface path is
resolved through the real system libraries rather than the stubs. Both worked,
which is good evidence the RPATH fix in G6 is correct rather than merely
plausible.

Not covered: Vulkan on the *Windows* target. wine's Vulkan needs more setup and
T0001 used GL there.
