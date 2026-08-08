# T0094 — Gameplay-extensible rendering

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 510 |
| **Created** | 2026-08-03 |
| **Blocks** | **Four tickets ride this transport and each names it** — [0148-post-process-stack.md](../open/0148-post-process-stack.md) (a game's own post effect, 148.5), [0088-sky-atmosphere-time-of-day.md](../open/0088-sky-atmosphere-time-of-day.md) (a game's own sky — this is how **D32**'s promise is discharged, since under D40 there is no sky shader of ours to override), [../completed/0091-volumetric-fog.md](../completed/0091-volumetric-fog.md) and [../completed/0096-hdr-pipeline-and-tonemapping.md](../completed/0096-hdr-pipeline-and-tonemapping.md). **Not a hard blocker on their engine halves** — an engine pass can go into `RenderStack` today — but a **design** blocker under **D35**: each would otherwise invent its own insertion API. [../completed/0047-evaluate-render-graph.md](../completed/0047-evaluate-render-graph.md) / **D41** — **answered "no" and it does not gate this ticket**: the seam is already expressible, and a frame graph would make 94.7 worse rather than better |
| **Refs** | [../completed/0046-frame-render-targets.md](../completed/0046-frame-render-targets.md), [../completed/0027-render-stack.md](../completed/0027-render-stack.md) — **`hp::SceneRenderLayer` is the worked example of the interface this ticket must prove from a module**, and `RenderPassContext` now carries `ClipSpace` because a gameplay layer building a projection has no other way to get it; [0147-engine-intermediates-for-shaders.md](../completed/0147-engine-intermediates-for-shaders.md) — 94.4/94.5 and 147.4 are one mechanism, designed together, referenced both ways; [0148-post-process-stack.md](../open/0148-post-process-stack.md) — game post effects ride this ticket's layer transport and hot-reload rules (94.7); **T0147** ([../completed/0147-engine-intermediates-for-shaders.md](../completed/0147-engine-intermediates-for-shaders.md)) / **D37** — **94.5's mechanism is built, and this ticket inherits it rather than designing one**: `SceneRenderLayer::setGameTexture(name, view)` feeds a texture this layer rendered to any material module that declared a `Texture2DArray` of that name (T0161's declaration, unchanged). Resolution order is `.hpmat` first, then the feed, then white. Two obligations: the view is **not** refcounted, so a layer re-feeds after every resize, and it calls `clearGameTextures()` on detach — a module's target outliving its module is the leak `FrameTargets` already refuses to allow |

## Why

**Rescoped 2026-08-08 by T0171 under D40. This ticket is one of the engine's two
extension seams, and it is the one that is not built.**

**There are exactly two seams here.** The **surface** seam is `IHpMaterial` —
per fragment, Slang generics, the default implementation *is* the standard path,
and it is finished (T0141, T0142, T0145, T0146, T0147, T0159, T0160, T0161). It
fits anything about a **surface**. It fits nothing about a **frame**.

**The frame seam is a pass, it lives in C++, and it is this ticket.** Everything
a game wants to do at frame granularity — a minimap, a portal or mirror view, a
security-camera monitor, a custom post effect, a decal buffer, player-drawn
markers, explored-area fog of war — is the same three capabilities: *own a
persistent target, run at an ordered point in the frame, read what the engine
already rendered.* Without them each one becomes an engine patch, which is
exactly what this architecture exists to prevent.

**And this ticket is now on the critical path for four others.** T0148's game
post effects, T0088's game-authored sky, T0091 and T0096's chain all ride this
transport. **D35's first half is the reason it comes first**: *a game-facing
mechanism is designed stage-agnostically or not at all.* Build the post chain
first and its insertion API gets retrofitted; build this first and the chain
plugs into a seam that already exists. Nine render tickets each proposed their
own hook before that rule was applied to them.

## What is actually missing — measured on T0171, and it is smaller than it looks

**The interface is built and correct. It has never once been used by a real
app.** That is the finding that reshapes this ticket, and it is why T0047 was
closed rather than done first ([D41](../../documentation/02-decision-log.md) —
a frame graph decides nothing here).

Already working, at the interface level:

- **Ordering** — `IRenderLayer::order` plus a `stable_sort`
  (`RenderStack.cpp:32-40`), ties keeping insertion order.
- **Reading engine resources** — `RenderPassContext::targets` →
  `FrameTargets::shaderResource(name)`, and **every** target carries
  `BIND_SHADER_RESOURCE` (`FrameTargets.cpp:110`), so anything the engine wrote
  is readable.
- **Feeding a game texture back into a material** — `SceneRenderLayer::setGameTexture`
  (T0147/D37), resolved `.hpmat` first, then the feed, then white.
- **Implementing a layer from a module at all** — the interfaces are pure
  virtual, so a module needs headers and no Diligent library (D22), and the
  linker still refuses to let it *create* a device.
- **A worked example** — `SceneRenderLayer` itself.

Missing, and this is the whole list:

1. **No shipping app instantiates a `RenderStack`.** The editor and the runtime
   both go through `SceneView`, which owns its own private `FrameTargets` and
   never touches the stack; `RenderStack` appears only in `tests/`. **There is
   no seam to insert into until an app builds one.**
2. **`ModuleServices` does not carry it.** `ModuleHost.hpp:62-81` hands a module
   `scene`, `assets`, `device` and `deviceContext` — **no `RenderStack*`** — so
   94.2 and 94.3 are not merely unbuilt, they are unreachable.
3. **Lifetime across a module unload.** A layer's vtable points into a library
   that can be unloaded, and the next frame crashes inside rendering. This is
   the genuinely hard part and it is unchanged.
4. **Persistent gameplay-owned targets.** `FrameTargets` is per-frame and
   rebuilt on resize; `RenderPassContext` hands out views valid for one call
   (D22). Neither is an answer for a target that must outlive both.

## The seam, stated once so the tickets that ride it do not each restate it

- **What a game implements**: `IRenderLayer` — `onRenderLayer(const RenderPassContext&)`
  plus `name()`, with `order`, `enabled`, `clear` and `useDepth` as data.
- **What the default is**: the engine's own layers. A game that adds nothing
  gets the standard frame, unchanged — the same property that makes
  `IHpMaterial` safe.
- **What it may read**: any `FrameTargets` slot by name, plus the scene colour
  and depth snapshots (D37's rules apply unchanged).
- **What it may write**: its own target, bound inside `onRenderLayer`.
- **What it may never do**: create a device or a swapchain (D22), or transition
  a resource off the frame thread (D41 quotes the line — automatic state
  management is not thread-safe).

**Anything a struct field could express is a setting, not a layer.** Which
tonemap operator, bloom threshold, cascade count: those are T0078's, and a
ticket proposing a hook for one of them is wrong.

## Done when

- [ ] **A shipping app builds a `RenderStack`** and renders through it — the
      seam is crossed by something that is not a test
- [ ] **`ModuleServices` exposes it**, and a gameplay module inserts a layer at
      a stated order without the engine knowing the module exists
- [ ] Gameplay can create and own a **persistent render target** that survives
      frames *and* resizes, with explicit lifetime
- [ ] Custom passes can read engine resources — depth, colour, snapshots — by
      name, through the existing `FrameTargets` API rather than a second one
- [ ] A gameplay-owned texture is bindable as a material parameter — **inherited
      from T0147, not designed here** (`setGameTexture`)
- [ ] Ordering relative to engine layers is explicit and controllable
- [ ] Render targets can be read back to CPU, for saving (T0083)
- [ ] **All of it survives a gameplay hot reload** (T0048) — see notes
- [ ] Misuse fails loudly in debug rather than corrupting the frame
- [ ] **A GPU zone scopes gameplay-submitted work** and attributes correctly in
      Tracy — a constraint on the API's *shape*, see the 2026-08-03 amendment
- [ ] **The four tickets that ride this transport are told the seam is ready**,
      by name: T0148, T0088, T0091, T0096

## Subtasks

- [ ] 94.0 **Build a `RenderStack` in the apps.** First, and it is the cheapest
      thing on this list: until an app has one, nothing below can be proved
      outside `tests/`. `SceneView` is what the editor uses today
- [ ] 94.1 **`RenderStack*` on `ModuleServices`** — one pointer, and 94.2/94.3
      become reachable
- [ ] 94.2 `IRenderLayer` implementations from the gameplay module
- [ ] 94.3 Insertion and ordering API for custom layers
- [ ] 94.4 `RenderTexture` — a gameplay-ownable persistent target with explicit
      lifetime. **Engine-owned and module-referenced** (see notes): a GPU
      resource that dies with its module takes the accumulated state with it
- [ ] 94.5 Documented read access to engine resources — depth, colour, snapshots
- [ ] 94.6 Async readback to CPU
- [ ] 94.7 **Hot-reload safety** — unregister before unload, re-register after,
      state serialized across. The hard one
- [ ] 94.8 Debug validation: writing a target being read, using a freed target,
      transitioning off the frame thread
- [ ] 94.9 **Worked example**, from a module, in `samples/` — not documentation,
      the proof that someone who did not write the engine can do this
- [ ] 94.10 Screenshot capture: grab the presented frame (post-tonemap), encode
      PNG, write via T0103.4's rules (see the 2026-08-03 amendment)
- [ ] 94.11 **Add the row to `12-vendored-capabilities.md`** — D40's rule. The
      row exists and says *"ours, unbuilt"*; keep it true

## Notes / findings

### 2026-08-08, T0171 — why this moved to the front, and what it does *not* need

**It is not a hard blocker on the engine halves of T0148/T0096/T0088/T0091.**
An engine pass can be inserted into `RenderStack` today; a tonemap pass needs
nothing from a gameplay module. **It is a design blocker**, which under D35 is
the stronger kind: *a game-facing mechanism is designed stage-agnostically or
not at all*. If the post chain ships first and then a game wants to insert an
effect, the chain's insertion API is retrofitted — and D35 exists because that
retrofit has already been paid for twice, on `IHpMaterial` and on T0160's
texture slots.

**T0047 was closed rather than done first** ([D41](../../documentation/02-decision-log.md)).
It had been sequenced ahead of this ticket on the belief that a declarative pass
layer decides this one's shape. It does not: the seam is already expressible,
and a frame graph would make 94.7 *worse*, because the graph would then own
resource declarations belonging to an unloaded module in addition to the
dangling vtable. The evidence and the numeric revisit triggers are on D41.

**The 2026-08-03 amendment about Tracy zones is a constraint on this ticket's
API shape and is now doubly load-bearing**, because 94.0 is the moment the shape
gets fixed. A submission interface with nowhere to put a zone scope cannot gain
one later without changing every call site.

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
