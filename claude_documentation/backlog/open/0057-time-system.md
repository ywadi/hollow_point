# T0057 — Time system

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 2 — Engine skeleton |
| **Created** | 2026-08-03 |

## Why

T0014 gives a raw delta time and nothing else. Several systems need more, and one
of them — physics — imposes structure on the main loop that is far cheaper to
build in now than to retrofit.

## Done when

- [ ] Unscaled and scaled delta time, both available
- [ ] Time scale supports slow motion and **pause** (pause is play-mode critical)
- [ ] **Fixed-timestep accumulator with an interpolation alpha exposed**
- [ ] Total elapsed time and frame count
- [ ] A maximum delta clamp so a debugger breakpoint does not explode simulation
- [ ] Editor and game time are separate — the editor keeps running while paused

## Subtasks

- [ ] 57.1 Clock: unscaled delta, scaled delta, elapsed, frame index
- [ ] 57.2 Time scale, including zero for pause
- [ ] 57.3 Fixed-step accumulator, with the leftover fraction exposed as alpha
- [ ] 57.4 Delta clamping
- [ ] 57.5 Separate editor and game clocks
- [ ] 57.6 Surface it in the profiler overlay

## Notes / findings

**The fixed-step accumulator is the point of doing this in Phase 2.** Physics
(T0051) must step at a fixed rate while rendering runs at whatever rate it can:

```
accumulator += dt
while (accumulator >= fixedStep) { Step(fixedStep); accumulator -= fixedStep; }
alpha = accumulator / fixedStep      // renderer interpolates with this
```

That `alpha` is what the renderer uses to interpolate between the previous and
current physics transforms. Building the loop without it means restructuring the
main loop and the transform component in Phase 9 — exactly the retrofit to avoid.

**Clamp the delta.** Without it, a breakpoint or a stalled frame produces a huge
dt, the accumulator runs dozens of physics steps to catch up, and everything
tunnels through walls. Clamping is one line and prevents a genuinely confusing
class of bug.

### Architecture review (2026-08-03) — priority raised Medium → High

T0062 (High, same phase) requires `OnFixedUpdate` driven by this ticket's
accumulator, and play mode (T0037) requires pause. A Medium-priority ticket
that gates two High-priority ones is mis-marked — the accumulator is the whole
reason this lives in Phase 2, as its own notes say.
