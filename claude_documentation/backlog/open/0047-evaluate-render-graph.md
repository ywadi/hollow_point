# T0047 — Declarative pass layer (and why not an off-the-shelf frame graph)

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Created** | 2026-08-02 |

## Why

Diligent provides no render graph / frame graph — no automatic resource aliasing,
barrier insertion, pass reordering or culling of unused passes. It is a real
absence, and the question of whether to build one will keep resurfacing.

This ticket exists to **answer it deliberately, later, with evidence** — not to
build one. Recording the reasoning now prevents it being built speculatively.

## Done when

- [ ] A decision is recorded in the decision log, either way
- [ ] If yes: scoped into its own tickets with a concrete justification
- [ ] If no: the trigger conditions for revisiting are written down

## Subtasks

- [ ] 47.1 Count the actual passes in the frame once the renderer is real
- [ ] 47.2 Measure whether render-target memory is genuinely under pressure
- [ ] 47.3 Assess how often pass ordering changes in practice
- [ ] 47.4 Decide, and record it

## Notes / findings

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

1. **Vulkan-only.** Raw `VkPipelineStageFlags2`/`VkAccessFlags2` throughout. Our
   OpenGL fallback matters (no D3D on Windows, per D2), so this would mean two
   separate renderers.
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
