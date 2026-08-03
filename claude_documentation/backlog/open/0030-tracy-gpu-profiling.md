# T0030 — Tracy GPU zones through Diligent

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Very Complex |
| **Phase** | 5 — Profiling |
| **Order** | 570 |
| **Created** | 2026-08-02 |

## Why

CPU timing alone cannot tell you whether a frame is CPU-bound or GPU-bound, which
is the first question worth asking about any frame. GPU zones give per-pass timing
on the actual device.

This is the part that needs real thought: Tracy's GPU support wants native API
handles (Vulkan device/queue/command buffer, or a GL context), and we work through
Diligent's RHI abstraction. Diligent does expose native handles, but the
interaction with its internal command-buffer management is where the difficulty
lives.

## Done when

- [ ] GPU zones appear in the Tracy viewer, aligned with CPU zones on the timeline
- [ ] Each `IRenderLayer` (T0027) shows as its own GPU zone
- [ ] Works on the Vulkan backend at minimum; OpenGL is a bonus
- [ ] No measurable overhead when profiling is disabled
- [ ] Calibration is right — GPU and CPU timelines line up rather than drift

## Subtasks

- [ ] 30.1 Establish how to reach native handles: `IRenderDeviceVk::GetVkDevice`,
      `GetVkPhysicalDevice`, `IDeviceContextVk::GetVkCommandBuffer` and the queue
- [ ] 30.2 Understand Diligent's command buffer lifetime before instrumenting —
      Tracy needs the *same* command buffer for begin and end of a zone
- [ ] 30.3 Initialise Tracy's Vulkan context with a query pool
- [ ] 30.4 Implement `HP_PROFILE_GPU_ZONE` for the Vulkan backend
- [ ] 30.5 Per-render-layer GPU zones
- [ ] 30.6 Verify calibration against a deliberately GPU-heavy frame
- [ ] 30.7 Decide whether OpenGL is worth supporting, or Vulkan-only is enough

## Notes / findings

**The hard part is command-buffer lifetime, not the API calls.** Tracy's Vulkan
zones write timestamp queries into a command buffer and require begin and end to
land in the same one. Diligent manages command buffers internally and may split
or submit them at points we do not control. Investigate this *before* writing the
implementation — it determines whether this is straightforward or needs a
different approach entirely.

**Do not patch `third_party/DiligentEngine`** to make this work. If the native
handles genuinely are not sufficient, record that finding and propose an
alternative (e.g. Diligent's own duration queries, `IQuery` with
`QUERY_TYPE_DURATION`) rather than forking the engine — that constraint has held
through every other problem in this project and is worth keeping.

Diligent's built-in `IQuery` duration queries are the fallback if Tracy's Vulkan
integration proves impractical: less rich, but backend-agnostic and fully
supported.

### Architecture review (2026-08-03)

The fallback is even cheaper than the ticket implies: Diligent ships ready-made
helpers for it — `DurationQueryHelper.hpp` and `ScopedQueryHelper.hpp` in
`DiligentCore/Graphics/GraphicsTools/interface/`, which manage the query
double-buffering that makes `IQuery` fiddly to use directly. If 30.2 concludes
Tracy's same-command-buffer requirement cannot be met through the RHI, the
fallback path is genuinely small.
