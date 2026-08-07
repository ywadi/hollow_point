# T0094 — Gameplay-extensible rendering

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 510 |
| **Created** | 2026-08-03 |
| **Refs** | [../completed/0046-frame-render-targets.md](../completed/0046-frame-render-targets.md), [../completed/0027-render-stack.md](../completed/0027-render-stack.md) — **`hp::SceneRenderLayer` is the worked example of the interface this ticket must prove from a module**, and `RenderPassContext` now carries `ClipSpace` because a gameplay layer building a projection has no other way to get it; [0147-engine-intermediates-for-shaders.md](../inprogress/0147-engine-intermediates-for-shaders.md) — 94.4/94.5 and 147.4 are one mechanism, designed together, referenced both ways; [0148-post-process-stack.md](0148-post-process-stack.md) — game post effects ride this ticket's layer transport and hot-reload rules (94.7); **T0147** ([../inprogress/0147-engine-intermediates-for-shaders.md](../inprogress/0147-engine-intermediates-for-shaders.md)) / **D37** — **94.5's mechanism is built, and this ticket inherits it rather than designing one**: `SceneRenderLayer::setGameTexture(name, view)` feeds a texture this layer rendered to any material module that declared a `Texture2DArray` of that name (T0161's declaration, unchanged). Resolution order is `.hpmat` first, then the feed, then white. Two obligations: the view is **not** refcounted, so a layer re-feeds after every resize, and it calls `clearGameTextures()` on detach — a module's target outliving its module is the leak `FrameTargets` already refuses to allow |

## Why

Game code has to be able to extend the renderer without modifying the engine.
The immediate driver is explored-area fog of war (T0093): the engine deliberately
does **not** implement that policy, so the game must be able to own a persistent
texture, accumulate visibility into it each frame, and sample it back in a
material.

That is not a niche case. Minimaps, portal and mirror views, security-camera
monitors, custom post effects, decal buffers and player-drawn markers all need the
same three capabilities. Without them, every one of these becomes an engine patch
— exactly the outcome this architecture exists to prevent.

## Done when

- [ ] Gameplay can create and own a **persistent render target** that survives frames
- [ ] Gameplay can implement a render layer and insert it into the RenderStack (T0027)
- [ ] Custom passes can read engine resources — visibility (T0093), depth, GBuffer
- [ ] A gameplay-owned texture is bindable as a material parameter (T0060)
- [ ] Ordering relative to engine layers is explicit and controllable
- [ ] Render targets can be read back to CPU, for saving (T0083)
- [ ] **All of it survives a gameplay hot reload** (T0048) — see notes
- [ ] Misuse fails loudly in debug rather than corrupting the frame

## Subtasks

- [ ] 94.1 `RenderTexture` resource ownable by gameplay, with explicit lifetime
- [ ] 94.2 Allow `IRenderLayer` implementations from the gameplay module
- [ ] 94.3 Insertion and ordering API for custom layers
- [ ] 94.4 Documented read access to engine resources — visibility, depth, colour
- [ ] 94.5 Bind a gameplay texture as a material parameter
- [ ] 94.6 Async readback to CPU
- [ ] 94.7 **Hot-reload safety** — unregister and re-register custom layers
- [ ] 94.8 Debug validation: writing a target being read, using a freed target
- [ ] 94.9 Worked example: accumulate visibility into a persistent texture
- [ ] 94.10 Screenshot capture: grab the presented frame, encode to PNG, write
      to a decided location (see the 2026-08-03 amendment)

## Notes / findings

### From T0046 (2026-08-05) — gameplay-owned persistent targets land here

T0046 closed with "gameplay-owned persistent targets are supported alongside
frame targets" **moved to this ticket**, because it is gameplay-extensible
rendering and that is this ticket's whole subject. Nothing in T0046 blocks it.

What already exists to build on:

- `hp::FrameTargets` owns the engine's per-frame targets, declared by *role*
  (`Colour`, `ColourHDR`, `Depth`) so no pass names a format. Every target
  carries `BIND_SHADER_RESOURCE`, so anything can be read as well as written.
- Resize is debounced by size, verified on device by pointer identity — an
  unchanged size returns the *same* view rather than quietly reallocating.
- `declarePingPong` gives a multi-pass effect a pair whose source and target
  cannot be the same texture.

The open question this ticket has to answer is **lifetime**. Frame targets are
recreated on every resize, and a gameplay module's target may want to outlive
that — or may want to die with the module, which is the harder case, because an
unloaded module leaves the engine holding GPU resources nobody owns.
`RenderPassContext` hands out raw `ITextureView*` valid for one call only (D22),
which is the right shape for a frame target and is explicitly *not* an answer
for a persistent one.


**Hot reload is the constraint that makes this non-trivial**, and it is the same
problem as T0062. A render layer implemented in the gameplay module has a vtable
pointing into that module; on reload the RenderStack holds a dangling pointer and
the next frame crashes — during rendering, which is a miserable place to debug.

The fix mirrors behaviours: custom layers are unregistered before unload and
re-registered after, with their state serialized across. **Render targets
themselves must be engine-owned** and merely *referenced* by the module, or the
GPU resource dies with the module and takes the accumulated fog of war with it.

**Resource state is the other hazard.** Transitions are not thread-safe (T0050),
and a custom pass that reads a target the engine is writing this frame produces
undefined results. Validate in debug builds — a clear assert beats a driver-level
mystery.

**Do not expose Diligent types directly** across the gameplay boundary if it can
be avoided. It couples game code to the RHI and makes replacing or upgrading
Diligent much harder. A thin engine-owned wrapper is worth the indirection here.

The worked example in 94.9 is not optional documentation — this is a capability
whose whole point is being usable by someone who did not write the engine.


### Amendment (2026-08-03) — the submission seam decides whether game draws can be profiled

Add to this ticket's acceptance: **a GPU zone can be scoped around work
submitted by gameplay code, and it appears correctly attributed in a Tracy
capture.**

This is not a profiling nicety that belongs only to T0030. It is a constraint on
*this* ticket's API shape, because a submission interface that has no place to
put a zone scope cannot gain one later without changing every call site. Game
code does not own the device or the command list, so if this seam does not
carry the zone, game draws are attributed to whichever engine pass wrapped them
— which hides the cost of exactly the code a game developer is trying to
profile.

See T0030's amendment for the mechanism and T0029's for why the Tracy client
lives in the engine shared library.

### Amendment (2026-08-03) -- screenshot capture is assigned here (94.10)

T0083.3 requires save-slot metadata including a "screenshot", and no ticket
provided one -- the design-gap survey (`documentation/07-design-gaps.md`,
item 9) found `screenshot` only as T0083's metadata line, an editor-evidence
line in T0032, and verification prose. The adjacent machinery is this ticket's
94.6 (async readback to CPU), which is why capture lands here rather than
growing a ticket of its own: capture-the-presented-frame is a readback with a
known source, plus PNG encode, plus a destination.

Scope of 94.10: capture the final presented frame (post-tonemap, T0096 -- a
screenshot of the raw HDR target is a bug), encode PNG, and write via the
write-directory rules (T0103.4). Consumers: T0083's slot thumbnails (scaled-
down variant wanted), a developer hotkey, and eventually T0036's asset
thumbnails -- which remain explicitly deferred; 94.10 does not take them on.

### Note (2026-08-03) -- the render itself is a separate ticket (T0120)

This ticket's "Why" names minimaps, portal and mirror views and security-camera
monitors as motivating examples, and 94.1/94.4/94.5 give gameplay a persistent
texture, read access to engine resources, and a way to bind that texture as a
material parameter. What none of that provides is the thing that actually
fills the texture with a second, independently-positioned camera's view of the
3D scene -- scene draw submission (T0028) is wired to one implicit camera and
one viewport-sized target, and a custom `IRenderLayer` here can *read* the
existing frame's resources but cannot re-invoke submission against a different
camera. **T0120 owns that mechanism** and is ordered directly after this
ticket because it reuses 94.1's `RenderTexture` type as its output rather than
inventing a second one. This ticket's own worked example (94.9, accumulating
visibility into a persistent texture) does not need T0120 -- it is a 2D
accumulation, not a re-render -- so the two tickets do not overlap; T0120 is
what makes the *other* named examples (portal, mirror, security monitor)
actually deliverable.
