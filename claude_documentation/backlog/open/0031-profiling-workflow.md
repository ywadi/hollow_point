# T0031 — Profiling workflow, budgets and documentation

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Low |
| **Complexity** | Simple |
| **Phase** | 5 — Profiling |
| **Order** | 580 |
| **Created** | 2026-08-02 |

## Why

A profiler nobody uses is dead weight. This turns the capability from T0029/T0030
into a habit: a documented workflow, a build configuration that makes profiling
one flag away, and agreed frame budgets so "is this fast enough" has an answer
rather than an opinion.

## Done when

- [ ] `-Dprofile` (or a config) produces a profiling build in one command
- [ ] `BUILDING.md` explains capturing a trace and reading it
- [ ] Frame budgets written down — target frame time and a rough split
- [ ] Instrumentation conventions documented so zones are named consistently
- [ ] Allocation tracking evaluated and a decision recorded

## Subtasks

- [ ] 31.1 Profiling build configuration in `build.zig`
- [ ] 31.2 Document capture and viewer workflow
- [ ] 31.3 Agree and record frame budgets
- [ ] 31.4 Zone naming conventions — consistent names make traces comparable
- [ ] 31.5 Evaluate Tracy's allocation tracking; it is powerful but costly, so
      decide deliberately rather than enabling it by reflex
- [ ] 31.6 Consider capturing a baseline trace to compare against later
- [ ] 31.7 Memory budgets alongside the frame-time ones (see the 2026-08-03
      note)

## Notes / findings

Frame budgets are worth setting before there is anything to measure, because they
change design decisions. Without a number, every feature is "probably fine".

Tracy can also track locks and allocations. Both are genuinely useful and both
add overhead — enabling everything by default makes the profiler itself the
bottleneck and produces misleading traces.

A baseline trace committed as a reference is unusual but valuable: it makes
"this got slower" detectable rather than a matter of memory.

### Note (2026-08-03) -- budgets means memory too

The design-gap survey (`documentation/07-design-gaps.md`, item 16) found
frame-*time* budgets owned here and memory budgets owned nowhere: `memory
budget`, `texture streaming`, `residency` -- zero hits. What exists is
per-system (D15's fixed particle buffer, T0107.5's effect cap, T0046.6's
render-target memory reporting) with no total and no per-category split. 31.7
adds memory to the same budget-setting exercise: a GPU total and an asset-RAM
total with a rough category split, sized against T0044's content-scale answer
("does anything ever stream?" is now on its question list). For a
confined-scene desktop game the numbers may be generous and never binding --
that is fine; the point is that outgrowing RAM becomes a measured event
rather than a quiet one. No eviction machinery is proposed beyond T0058.2's
release policy unless a budget is actually exceeded.
