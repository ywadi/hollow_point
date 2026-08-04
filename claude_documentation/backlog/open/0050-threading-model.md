# T0050 — Threading model and enkiTS workload map

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 520 |
| **Created** | 2026-08-03 |
| **Refs** | T0045, T0026, T0049, T0029 |

## Why

"Use enkiTS" is not a plan. Diligent imposes real constraints on what may run
where, and getting this wrong produces intermittent corruption that is
extraordinarily hard to debug. The model needs deciding once, written down, and
enforced.

## Done when

- [ ] The threading model is documented in `documentation/03-build-harness.md`
      or a new architecture doc
- [ ] Thread ownership rules are explicit — what may touch what
- [ ] Parallel workloads are identified and implemented where they pay
- [ ] Threads are named and visible in Tracy (T0029)
- [ ] Debug assertions catch violations of the ownership rules
- [ ] Speedup is measured, not assumed

## Subtasks

- [ ] 50.1 Write the ownership rules down
- [ ] 50.2 Assert thread ownership in debug builds — a cheap assert now saves
      days later
- [ ] 50.3 Parallel animation sampling (T0049) — the highest-value workload here
- [ ] 50.4 Parallel frustum culling — **this ticket owns it outright**; T0045
      deliberately ships culling single-threaded. See the 2026-08-04 note
- [ ] 50.5 Parallel asset import and LOD generation (T0038/T0039)
- [ ] 50.6 Enable Diligent's `AsyncShaderCompilation` device feature
- [ ] 50.7 Evaluate moving the window pump to its own thread
- [ ] 50.8 Measure each before and after

## Notes / findings

**The binding constraint, from Diligent's own headers:** resource state
transitions are **not thread-safe** — `DeviceContext.h` says so repeatedly. Since
we rely on automatic transitions (T0046), only one thread may touch resource
state. That single fact determines the whole model.

Ownership:

| Thread | Owns |
|---|---|
| Main | Immediate context, resource state transitions, ImGui, submission |
| Window pump | OS message loop — ideally its own thread (T0015 notes why) |
| enkiTS workers | Culling, animation sampling, asset import, LOD, cooking |
| Diligent internal | Async shader/PSO compilation |

**The rule: jobs compute, the main thread submits.** Parallel *preparation*,
single-threaded submission. Nearly all the available win is in preparation.

**Deliberately deferred: deferred contexts.** Diligent supports multithreaded
command recording via `NumDeferredContexts`, but because automatic state
transitions are not thread-safe, parallel recording forces manual barrier
management (`RESOURCE_STATE_TRANSITION_MODE_VERIFY` plus explicit transitions).
That is a large complexity increase for an unmeasured win. Revisit only if
profiling shows submission is the bottleneck — which it usually is not until
draw-call counts are high.

**The entt registry is not safe for concurrent mutation.** Parallel reads are
fine; parallel writes are not. Jobs should read components and write results into
their own output buffers, which the main thread then applies.

### Ordering fix (2026-08-04) — 50.4 is this ticket's alone, and 50.1/50.2 gate it

T0124's sweep found parallel culling claimed twice: 45.6 in T0045 and 50.4
here. Worse, T0045 sits at **Order 440** and this ticket at **520**, so the
board's own order would have had parallel culling written 80 points before the
ownership rules (50.1) and asserts (50.2) that make it safe.

**T0045 gives up the work; this ticket keeps it.** T0045 now ships culling
single-threaded, shaped so parallelising it here is a change of driver rather
than a redesign — a pure (frustum, bounds) → visibility function, results into
an output buffer the main thread applies, no registry writes and no resource
state touched from the pass. That constraint is written into T0045 as its 45.6.

Two consequences for this ticket:

- **50.4 must verify the shape actually held** before parallelising, rather
  than assuming it. If T0045 shipped an in-place `registry.emplace<Visible>`
  loop, parallelising it is a rewrite and that should be said out loud here
  rather than absorbed silently.
- **50.1 and 50.2 are prerequisites of 50.3–50.5, not peers.** They are cheap —
  the ownership table and the rules are already written in the notes above, so
  50.1 is largely transcription — and the whole point of an assert is that it
  exists before the code it guards. Do them first within this ticket.

Deliberately *not* done: splitting 50.1/50.2 into their own earlier ticket.
That was the obvious alternative and it buys nothing once T0045 stops doing
parallel work — there is then no consumer of the rules that precedes this
ticket. It would also strand a fragment of a coherent design across two files.

`AsyncShaderCompilation` is a Diligent device feature — enabling it is far
cheaper than threading PSO creation ourselves, and removes a common hitching
source at load.
