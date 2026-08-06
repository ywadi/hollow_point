# T0033 — Viewport panel

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 6 — Editor |
| **Order** | 610 |
| **Created** | 2026-08-02 |
| **Refs** | [../completed/0028-scene-draw-submission.md](../completed/0028-scene-draw-submission.md) — **consumes `FrameRenderedEvent`, and deletes the dev present path this panel replaces.** T0028 left that path unable to show a frame on Vulkan; see below |

## Inherited from T0028 (2026-08-05)

**The frame already exists and is already published.** `hp::SceneView` renders
the scene into an offscreen colour target and the editor dispatches a
`hp::FrameRenderedEvent` carrying its shader-resource view every frame. This
panel is the first real consumer: draw that view as an ImGui image. **Do not
reach for a renderer pointer** — the event exists precisely so the viewport does
not need one, and that is what keeps the editor deletable from a shipped build.

**This ticket also deletes the dev present path, and that fixes a known gap.**
`RenderLayer::setPresentSource` blits the frame to the back buffer with a plain
`CopyTexture`, which needs an exact format match. It has one on OpenGL and not on
Vulkan, whose surface here is `BGRA8_UNORM_SRGB` against the scene target's
`RGBA8_UNORM_SRGB` — so **a Vulkan editor currently shows a clear colour rather
than the scene**, deliberately, because copying regardless would put a
red/blue-swapped image on screen and that reads as a shader bug.

Sampling the texture in a shader — which is what an ImGui image does — converts
formats as a matter of course, so this problem disappears here rather than
needing a fix first. **If this panel is going to be a while**, the alternative is
a fullscreen-triangle blit with a trivial shader, which is also what a compositor
does and would then belong to T0027 rather than being written twice. Recorded so
whoever gets here first can decide, rather than discovering a black Vulkan
window and assuming the renderer is broken.

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

### Inherited from T0111 / D25 (2026-08-05) — the panel is native, the scene may not be

**The editor viewport is the first place D25's sizing rule bites.** The rule is:
world and post-process targets size from **output x renderScale**; UI, HUD and
editor panels are **always native**.

So this panel displays a texture that **may not be panel-sized** — at render scale
0.7 the scene texture is smaller and must be upscaled into the panel, and at scale
above 1 (SSAA) it is larger and downsampled. Sampling it in a shader, which this
ticket does anyway to fix T0028's Vulkan format mismatch, handles both — but the
filter is then a choice rather than an accident, and the panel must not assume
`sceneTexture.size == panel.size`.

D25 also records that **T0027.5's single-target compositing collides with
UI-at-native**, and that the fix is the same seam as a tonemap pass. If this
ticket reaches that seam first, it owns it. See [../completed/0111-anti-aliasing-and-render-scale.md](../completed/0111-anti-aliasing-and-render-scale.md).

**Resize needs debouncing.** Recreating the render target on every pixel of a
drag will stutter badly and thrash GPU memory. Resize on drag-end, or throttle.

Watch the render-target lifetime: the texture handed over in the event must stay
alive while ImGui draws it, which is *after* the panel's update returns. Freeing
or resizing it in the same frame is a use-after-free that often appears to work
until it doesn't.

The texture-space V direction is still a device-reported property
(`ClipSpace::yToV`, negative on Vulkan) — read it rather than assuming,
even though OpenGL and its opposite convention are gone (D29/T0144).
