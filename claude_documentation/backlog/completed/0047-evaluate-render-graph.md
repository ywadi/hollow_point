# T0047 — Declarative pass layer (and why not an off-the-shelf frame graph)

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 530 |
| **Created** | 2026-08-02 |
| **Closed** | 2026-08-08 — **answered "no", as [D41](../../documentation/02-decision-log.md)**, on T0171's sweep |
| **Refs** | [0094-gameplay-extensible-rendering.md](../open/0094-gameplay-extensible-rendering.md) — **this ticket was sequenced ahead of it on the belief that it decides T0094's shape. It does not**, and that is the useful part of the answer: the pass seam is already expressible and what is missing is three pieces of plumbing, all T0094's. The finding is on that ticket too; [0171-expose-not-replace-sweep.md](0171-expose-not-replace-sweep.md); [../../documentation/12-vendored-capabilities.md](../../documentation/12-vendored-capabilities.md) — the pass-orchestration and render-target-pooling rows are this ticket's evidence, kept where a revisit will find them; **D26**, **D29**, **D40**, **D41** |

## Why

Diligent provides no render graph / frame graph — no automatic resource aliasing,
barrier insertion, pass reordering or culling of unused passes. It is a real
absence, and the question of whether to build one will keep resurfacing.

This ticket existed to **answer it deliberately, with evidence** — not to build
one. **It is answered: no.** The reasoning, the measurements and the numeric
revisit triggers are **D41**.

## Done when

- [x] **A decision is recorded in the decision log, either way** — **D41**, with
      five measurements and the rejections
- [~] If yes: scoped into its own tickets with a concrete justification —
      **not applicable, the answer is no.** No tickets were created
- [x] **If no: the trigger conditions for revisiting are written down** — D41's
      four triggers, three of them numeric, plus the instruction to re-read the
      capability matrix's pass-orchestration row first, in case upstream has
      grown one by then

## Subtasks

- [x] 47.1 **Count the actual passes in the frame** — **three draw passes**:
      opaque (`SceneRenderer.cpp:2618`), blend (`:2770`, conditional on blended
      primitives existing), and the present blit (`Render.cpp:469-482`). Two are
      unconditional. Plus a conditional non-draw scene snapshot
      (`SceneRenderer.cpp:2679-2757`, two `CopyTexture` calls) when a blended
      material reads the screen. The ticket's own revisit trigger was 15–20
- [x] 47.2 **Measure whether render-target memory is genuinely under pressure** —
      **no.** Four full-resolution targets, declared from a single call site
      (`SceneView.cpp:73-87`): `scene.colour`, `scene.depth` and the two
      snapshots. `PrecomputeCubemaps`'s ~60 passes are create-time and once per
      device (`SceneRenderer.cpp:2204`), not per frame
- [x] 47.3 **Assess how often pass ordering changes in practice** — it does not,
      and the reason is stronger than "rarely": **no shipping app builds a
      `RenderStack` at all.** Editor and runtime both go through `SceneView`;
      the stack appears only in `tests/`. The pass order is fixed in
      `SceneRenderer` and has changed once, when T0147 split phase 10.9
- [x] 47.4 **Decide, and record it** — D41

## Notes / findings

### 2026-08-08 — the answer, and the four things it rests on

**A frame graph's two biggest benefits are one free and one unreachable.**

- **Barriers are already Diligent's**, inserted automatically per resource at
  every `SetRenderTargets` / `Clear` / `Copy` under
  `RESOURCE_STATE_TRANSITION_MODE_TRANSITION` (`DeviceContext.h:257`). The
  engine already uses that mode everywhere (`RenderStack.cpp:115-123`,
  `SceneView.cpp:202-207`, `Render.cpp:470-661`).
- **Aliasing is unreachable and always will be.** `STATE_TRANSITION_FLAG_ALIASING`
  (`DeviceContext.h:2295-2297`) is documented as sparse-resources-only, and
  Diligent exposes no image-memory allocation API. This was blocker 3 of the
  2026-08-03 Granite rejection below; it is confirmed unchanged at the current
  pin.

**Nothing in the vendored tree pools render targets.** `PostFXContext`
(`.cpp:246`), `GBuffer`, `HnFrameRenderTargets` (`HnBeginFrameTask.cpp:193-198`)
and each of DiligentFX's five post components all use the identical policy: one
persistent texture per *named slot*, recreated when the viewport size changes.
`ResourceRegistry` is a `std::vector` indexed by an enum — no desc keying, no
free list, no lifetime tracking. `IRenderStateCache` caches **shaders and PSOs
only**. So "pool targets by lifetime" is not something to adopt; it would be
ours to write, and it would not reach inside the five components that would
create most of the intermediates anyway.

**And the strongest argument is upstream's own practice at four times our
scale.** Hydrogent's **22-task** USD renderer — the most complex frame anywhere
in the tree — is a **hand-ordered `std::vector`** walked linearly
(`HnTaskManager.cpp:517-522`, order fixed by registration order at
`HnTaskManager.hpp:382`), with the read/write matrix a frame graph would
*compute* written **by hand in a doc comment** (`HnTaskManager.hpp:92-155`).
Its pass culling is a per-task boolean (`HnTask::IsActive`), which is exactly
what `IRenderLayer::enabled` already is. **No topological sort of passes exists
anywhere in DiligentCore, DiligentFX, DiligentTools, DiligentSamples or this
engine.** Hydrogent is also not built here — it needs `DILIGENT_USD_PATH`,
which is never set.

**Diligent's `IRenderPass` / `IFramebuffer` were also checked and are not it.**
*"Render pass has no methods"* (`RenderPass.h:503`); *"Framebuffer has no
methods"* (`Framebuffer.h:102`). Every field mirrors Vulkan, subpass
dependencies are hand-authored (`Tutorial19_RenderPasses.cpp:447-454`) and
`RenderPassBase` only validates what you wrote. **DiligentFX uses them zero
times**, and neither does this engine; everything real uses `SetRenderTargets`.

### The correction to how this ticket was sequenced — read this before reopening

This ticket was put ahead of **T0094** on the belief that it *decides T0094's
shape*. **It does not.** The seam a game needs — insert a pass before or after
engine passes, read engine targets, write its own, feed a texture back into a
material — is already expressible today:

- ordering: `IRenderLayer::order` + `stable_sort` (`RenderStack.cpp:32-40`);
- reading engine targets: `RenderPassContext::targets` →
  `FrameTargets::shaderResource(name)`, and every target carries
  `BIND_SHADER_RESOURCE` (`FrameTargets.cpp:110`);
- writing its own: a layer may bind its own target inside `onRenderLayer`;
- feeding a material: `SceneRenderLayer::setGameTexture` (T0147/D37);
- and a module can implement `IRenderLayer` with **no Diligent library linked**,
  because the interfaces are pure-virtual (D22).

What is missing is plumbing, and every item belongs to T0094:

1. **No shipping app instantiates a `RenderStack`** — so the seam has never been
   crossed once by a real app;
2. **`ModuleServices` does not expose it** (`ModuleHost.hpp:62-81` hands a module
   scene, assets, device and context — no `RenderStack*`), so 94.2 and 94.3 are
   literally unreachable from a module today;
3. **module-unload lifetime** — a layer's vtable points into a library that can
   be unloaded.

**A frame graph makes item 3 worse**, because the graph would then own resource
declarations belonging to a dead module in addition to the dangling vtable.
That is the sharpest form of the argument for closing this ticket "no" rather
than deferring it.

### What is *not* claimed

- **This is not "a pass layer would never help".** It is "at three passes, with
  barriers free and aliasing unreachable, it does not help yet". D41's triggers
  are the conditions under which that changes, and they are numeric so that the
  next reader does not have to re-argue it.
- **The measurements are of today's frame.** Shadows, a tonemap pass, five post
  effects and a sky are all still unbuilt; the optimistic future is around a
  dozen passes, which is still under the trigger. Re-count when T0148 lands.

**The case against building one now, which is the current position:**

- Its biggest benefit — correct barriers — is already handled by Diligent's
  automatic resource state transitions.
- Its second benefit — pass orchestration — is partly provided by DiligentFX,
  which ships Bloom, DepthOfField, SSAO, SSR, TAA and EpipolarLightScattering as
  self-contained passes.
- `RenderStack` (T0027) already gives ordered, composited passes, which covers
  the practical need.
- Its remaining value scales with pass count and platform count. Frostbite and
  Unreal need one because they have dozens of passes across many platforms. This
  is one game on two backends.

A frame graph is a well-known way to spend months building flexibility that is
never exercised. The cost is not just writing it — every pass afterwards is
written against a more abstract API, and debugging gets harder.

**Revisit if any of these become true:**
- more than roughly 15-20 distinct passes in a frame
- render-target memory becomes a real constraint
- passes need to be enabled/reordered dynamically per-scene or per-quality-level
- a third backend or platform with different barrier semantics appears

---

## Evaluated: Granite RenderGraph (2026-08-03) — REJECTED

Proposal: adopt Themaister's RenderGraph from Granite as a lightweight frame
graph and map its passes onto Diligent, getting flexibility without writing one.

**Rejected — it is not a standalone library.** Checked
`Themaister/Granite/renderer/render_graph.hpp` directly. It includes
`device.hpp`, `vulkan_headers.hpp`, `thread_group.hpp`, `stack_allocator.hpp`
and `quirks.hpp`, and its API is expressed in Granite's own Vulkan abstraction:

```cpp
void build_render_pass(Vulkan::CommandBuffer &cmd);
Vulkan::ImageView &get_physical_texture_resource(unsigned index);
VkPipelineStageFlags2 stages; VkAccessFlags2 access;
```

Adopting it means porting `Vulkan::Device`, `Vulkan::CommandBuffer`,
`Vulkan::ImageView`, `Vulkan::Buffer` and `Vulkan::Semaphore` onto Diligent —
stacking one RHI abstraction on another, both solving the same problem.

Three further blockers:

1. **Vulkan-only.** Raw `VkPipelineStageFlags2`/`VkAccessFlags2` throughout.
   *(Weakened by D29/T0144, 2026-08-06: the engine is Vulkan-only now too,
   so "it would mean two renderers" no longer applies — the blockers below
   still do.)*
2. **Barriers collide.** Diligent already inserts them automatically, and that
   automatic path is explicitly not thread-safe. A frame graph wants to own
   barriers; you cannot have both without switching Diligent to manual mode and
   driving native Vulkan handles.
3. **Aliasing is unreachable.** Memory aliasing needs control over image memory
   allocation. Diligent allocates internally and exposes no aliasing API — so the
   single biggest benefit is unavailable regardless of the port.

The general pattern: a frame graph's value is in owning barriers and memory, and
those are precisely what Diligent has taken ownership of. Any off-the-shelf frame
graph will collide the same way — this is not specific to Granite.

## Revised direction: build a small declarative pass layer instead

Not a full frame graph, and not nothing:

- passes declare the named resources they **read** and **write**
- passes are topologically sorted from those declarations
- render targets are **pooled and reused by lifetime** (reuse, not aliasing)
- Diligent keeps doing barriers
- unused passes can be culled from the declarations

A few hundred lines, backend-agnostic across Vulkan and OpenGL, no fork of
another engine's internals. It deliberately omits memory aliasing, which is not
reachable through Diligent anyway.

Do this **after** T0027 (RenderStack) and T0046 (frame targets) are real, so it
is built against actual passes rather than imagined ones.
