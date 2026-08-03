# T0019 — Profiling macro surface (Tracy-ready, no-op for now)

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Trivial |
| **Phase** | 2 — Engine skeleton |
| **Order** | 20 |
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


### Architecture amendment (2026-08-03) — the macros must work from the gameplay module

Written before D12 existed, so this ticket assumes one binary. Game logic is
C++ in a **separate shared module** that links the engine, and profiling has to
cover it — game code is the code most worth profiling.

Three requirements that follow:

- **Game code uses these same macros**, from an engine header. That is the
  point of the macro surface and the reason this ticket sits so early: zones
  get written as code is written. A second, module-local profiling API would be
  two vocabularies for one job.
- **The macros must resolve to a single Tracy client instance**, living in the
  engine shared library (see T0029's amendment). This is the D12 rule applied to
  one more piece of engine state: two Tracy clients means the module's zones
  never appear in the engine's capture.
- **Disabled must mean absent, not skipped.** With profiling compiled out the
  macros expand to nothing — no branch, no symbol, no argument evaluation. A
  macro that degrades into `if (profiling_enabled)` leaves the cost in the
  shipped build, which is the outcome this is meant to prevent. Note that
  arguments must not be evaluated at all: `HP_PROFILE_ZONE(expensive())` must
  not call `expensive()`.

**Two switches, deliberately not the same mechanism.** "Off in the shipped
build" is compile-time (`TRACY_ENABLE` undefined) and is the one that preserves
performance. "Off right now in the editor" is a *capture* toggle — with the
client compiled in, `TRACY_ON_DEMAND` collects nothing until a profiler
connects. An editor checkbox controls the second and cannot deliver the first;
say so in the UI, or someone will expect a toggle to recover shipped-build
performance.

**This makes profiling a build-configuration axis**, which is T0104's problem
too: enabling it changes the engine's symbol surface, so a module built with
profiling and an engine built without must not load. See that ticket.
