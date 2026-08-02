# T0033 — Viewport panel

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 6 — Editor |
| **Created** | 2026-08-02 |

## Why

First panel to build, because it depends only on the event system and produces
visible output immediately — which makes everything after it far easier to debug.

It displays the render layer's offscreen texture, received via the
"new frame rendered" event (T0028). Neither side holds a pointer to the other.

## Done when

- [ ] The rendered scene appears inside a dockable ImGui panel
- [ ] Resizing the panel resizes the render target, without stretching artefacts
- [ ] Three resize modes work: stretch, aspect-preserving fit, crop-and-centre
- [ ] The chosen mode persists across restarts (via EditorState, T0034)
- [ ] Mouse position maps correctly from panel space into scene space

## Subtasks

- [ ] 33.1 Panel subscribing to the frame-rendered event
- [ ] 33.2 Display the texture via `ImGui::Image` with Diligent's texture binding
- [ ] 33.3 Tell the render layer the target size when the panel resizes
- [ ] 33.4 The three fit modes
- [ ] 33.5 Panel-space to scene-space coordinate mapping — picking needs it later
- [ ] 33.6 Handle the degenerate cases: zero-size panel, panel hidden/undocked

## Notes / findings

**Resize needs debouncing.** Recreating the render target on every pixel of a
drag will stutter badly and thrash GPU memory. Resize on drag-end, or throttle.

Watch the render-target lifetime: the texture handed over in the event must stay
alive while ImGui draws it, which is *after* the panel's update returns. Freeing
or resizing it in the same frame is a use-after-free that often appears to work
until it doesn't.

Y-axis convention differs between Vulkan and OpenGL; the image may appear flipped
on one backend. Check both rather than fixing it blindly for one.
