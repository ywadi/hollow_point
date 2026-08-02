# T0003 — Verify the Vulkan backend on real hardware

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Created** | 2026-08-02 |

## Why

Every runtime check so far used **OpenGL via llvmpipe under Xvfb** — software
rasterisation, no real driver. Vulkan is the primary backend for both targets
(and on Windows it is the *only* modern one, since D3D is disabled by design —
see D2). It has never been exercised.

Vulkan also loads differently: Diligent uses `volk`, which `dlopen`s the loader
at runtime rather than linking it. That path is untested, and it is exactly the
kind of thing the stub sysroot (D4/G6) could plausibly have broken.

## Done when

- [ ] `./ImGuiProbe --mode vk` runs on a real GPU and renders
- [ ] The engine log reports a real adapter, not llvmpipe/lavapipe
- [ ] ImGui still draws — the Diligent ImGui renderer backend has only ever been
      seen working on GL
- [ ] Result recorded for both targets if T0001 makes the Windows side runnable

## Subtasks

- [ ] 3.1 `vulkaninfo | head` to confirm a real driver is present
- [ ] 3.2 Run on the host display (not Xvfb — it has no Vulkan driver)
- [ ] 3.3 Compare the reported adapter against `vulkaninfo`
- [ ] 3.4 Exercise the ImGui widgets, since the renderer backend is per-backend code

## Notes / findings

- This needs the user's actual desktop; it cannot be completed headlessly.
- If Vulkan fails while GL works, suspect the volk/loader path first, then the
  swapchain surface creation (xcb vs xlib) — Diligent has both, and the sysroot
  provides both stubs.
