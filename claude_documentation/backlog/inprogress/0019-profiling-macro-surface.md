# T0019 — Profiling macro surface (Tracy-ready, no-op for now)

| | |
|---|---|
| **Status** | ⏸ BLOCKED on T0014/T0017 for 19.3 |
| **Priority** | Medium |
| **Complexity** | Trivial |
| **Phase** | 2 — Engine skeleton |
| **Order** | 35 |
| **Created** | 2026-08-02 |

## Why

Tracy lands in Phase 5, but instrumentation should be written *as* code is
written, not retrofitted across an existing codebase — which is miserable and
always ends up patchy.

Defining the macro surface now costs almost nothing: the macros compile to
nothing until Tracy is wired in, and every zone added during Phases 2-4 lights up
for free the moment it is.

## Done when

- [x] `HP_PROFILE_ZONE`, `HP_PROFILE_ZONE_NAMED`, `HP_PROFILE_FRAME`,
      `HP_PROFILE_GPU_ZONE` exist and compile to nothing by default
- [x] Enabled/disabled by one build option (`HP_PROFILING`, default OFF)
- [x] Zero measurable cost when disabled — **verified by comparing emitted assembly**, not assumed
- [ ] The frame loop and layer updates are already instrumented — **blocked: neither exists yet** (T0014, T0017)

## Subtasks

- [x] 19.1 `engine/include/hp/Profiling.hpp` with the macro surface
- [x] 19.2 `HP_PROFILING` build option, wired through the toolchain
- [ ] 19.3 Instrument the frame loop and `LayerStack::OnUpdate` — **blocked on T0014 and T0017**
- [x] 19.4 Confirm disabled builds emit no calls (inspect the object, do not
      assume — an unused RAII object can still cost if not fully inlined)
- [x] 19.5 Document the convention so instrumentation is added by habit

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


### Ordering correction (2026-08-03) — this cannot precede T0013

Placed at order 20, ahead of T0013, on the argument that the profiling macros
should exist before engine code is written so instrumentation is added by habit
rather than retrofitted. The argument is right and the placement was impossible:
19.1 puts the header at `engine/include/hp/Profiling.hpp`, which does not exist
until T0013 creates the engine library, and 19.3 instruments the frame loop and
`LayerStack::OnUpdate`, which need T0014 and T0017.

Moved to 35, immediately after T0013. **The ticket completes in two parts**: the
macro surface (19.1, 19.2, 19.4, 19.5) can land as soon as `engine/` exists, and
19.3 waits for the frame loop. Doing the first part early still buys what the
original placement was reaching for — the macros are available before there is
much to instrument.

## Evidence

**Zero code emitted when disabled**, which the ticket rightly insisted be
inspected rather than assumed. An instrumented function compiled at `-O2`
against the same function with the macro lines stripped:

```
$ grep -v "HP_PROFILE_" with.cpp > without.cpp
$ zig c++ -std=c++20 -O2 -S ... | grep -vE '^\s*\.(file|loc|cfi)'
  assembly IDENTICAL
```

The object files differ by 8 bytes even at `-g0`, which is the embedded source
filename (`with.cpp` versus `without.cpp`) — the emitted instructions are the
same. **A first attempt at this measurement was wrong** and worth recording: the
`sed` that stripped macro lines also deleted a loop body that shared a line with
one, so the objects "differed" because real code had been removed. Putting each
macro on its own line fixed the comparison. A verification that is itself buggy
is worse than none, because it produces a confident wrong answer.

**Arguments are never evaluated.** Proven twice. At link time, a translation
unit passing an undefined function to the macros links successfully — if the
argument were evaluated the symbol would be required:

```
$ cat noeval.cpp
const char* neverDefined();          // declared, never defined
HP_PROFILE_ZONE_NAMED(neverDefined());
$ zig c++ ... -o noeval
  LINKED -- argument never evaluated
```

And at run time by `tests/fast/profiling_macros_test.cpp`, which passes
side-effecting calls to every macro and asserts the counter stays at zero. That
test is the regression guard; the link-time check was the one-off proof.

```
$ zig build test -Dtest=fast
[doctest] test cases:     5 |     5 passed | 0 failed | 0 skipped
[doctest] assertions: 10031 | 10031 passed | 0 failed |     (x2 -- both targets)
```

**`HP_PROFILING` is PUBLIC on `hp_engine`**, deliberately. The macros live in a
public header, so consumers must compile against the same setting as the engine
— and since enabling it changes the engine's symbol surface, a mismatch is
exactly what T0104 refuses at load. PRIVATE would let that happen silently and
is the easiest way to get this wrong.

**Enabling it fails loudly today.** With no backend wired, `HP_PROFILING=ON`
produces `#error "HP_PROFILING is enabled but no profiler backend is wired up
yet -- see T0029"`. Better than silently building instrumentation that records
nothing.

## Not done

**19.3 is blocked, not skipped.** "Instrument the frame loop and
`LayerStack::OnUpdate`" needs T0014 and T0017, neither of which exists. This is
the half of the ticket that has to wait, and it is why the ordering correction
above moved T0019 after T0013 but ahead of the rest — the macros are available
before there is much to instrument, which is the point.

The only instrumented call site today is `engineRegisterConsumer`, which exists
to prove the macro compiles in real engine code rather than only in a test.
