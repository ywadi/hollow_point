# T0019 — Profiling macro surface (Tracy-ready, no-op for now)

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Phase** | 2 — Engine skeleton |
| **Created** | 2026-08-02 |

## Why

Tracy lands in Phase 5, but instrumentation should be written *as* code is
written, not retrofitted across an existing codebase — which is miserable and
always ends up patchy.

Defining the macro surface now costs almost nothing: the macros compile to
nothing until Tracy is wired in, and every zone added during Phases 2-4 lights up
for free the moment it is.

## Done when

- [ ] `HP_PROFILE_ZONE`, `HP_PROFILE_ZONE_NAMED`, `HP_PROFILE_FRAME`,
      `HP_PROFILE_GPU_ZONE` exist and compile to nothing by default
- [ ] Enabled/disabled by one build option, off in shipping builds
- [ ] Zero measurable cost when disabled — verify the generated code
- [ ] The frame loop and layer updates are already instrumented

## Subtasks

- [ ] 19.1 `engine/include/hp/Profiling.hpp` with the macro surface
- [ ] 19.2 `HP_PROFILING` build option, wired through the toolchain
- [ ] 19.3 Instrument the frame loop and `LayerStack::OnUpdate`
- [ ] 19.4 Confirm disabled builds emit no calls (inspect the object, do not
      assume — an unused RAII object can still cost if not fully inlined)
- [ ] 19.5 Document the convention so instrumentation is added by habit

## Notes / findings

Design the macro names to match Tracy's semantics (scoped zone, named zone,
frame mark, GPU zone) so T0029 is a mapping exercise rather than a redesign.

This deliberately replaces Laura's hand-rolled `ScopeTimer` + profiler panel.
Tracy does all of it better, adds GPU timing his design never had, and comes with
a mature external UI — building our own would be effort spent to end up behind.

Keep the macros in a header with **no** engine dependencies, so they can be used
anywhere including low-level utilities.
