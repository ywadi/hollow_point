# T0057 — Time system

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 2 — Engine skeleton |
| **Order** | 70 |
| **Created** | 2026-08-03 |

## Why

T0014 gives a raw delta time and nothing else. Several systems need more, and one
of them — physics — imposes structure on the main loop that is far cheaper to
build in now than to retrofit.

## Done when

- [x] Unscaled and scaled delta time, both available
- [x] Time scale supports slow motion and **pause**, with pause distinct from a zero scale
- [x] **Fixed-timestep accumulator with an interpolation alpha exposed**, and bounded per frame
- [x] Total elapsed time (scaled and unscaled) and frame count
- [x] A maximum delta clamp so a debugger breakpoint does not explode simulation
- [x] Editor and game time are separate — `Clock` is instantiable, so the two are independent instances rather than modes of one global

## Subtasks

- [x] 57.1 Clock: unscaled delta, scaled delta, elapsed, frame index
- [x] 57.2 Time scale, including zero for pause
- [x] 57.3 Fixed-step accumulator, with the leftover fraction exposed as alpha
- [x] 57.4 Delta clamping
- [x] 57.5 Separate editor and game clocks
- [ ] 57.6 Surface it in the profiler overlay — **not done, and not blocked by this ticket**: there is no overlay. It belongs with T0029/T0031, and the clock exposes everything an overlay would read

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


## Findings

**Pause is separate from a zero time scale**, and that is the whole reason play
mode is usable. If pause were `setTimeScale(0)`, unpausing would have to guess
what the scale had been — and slow motion plus pause would fight each other. A
test covers unpausing restoring the scale that was set.

**The fixed step is bounded per frame, and the debt is dropped rather than
carried.** A machine that cannot simulate as fast as real time otherwise
accumulates more debt each frame and runs more steps to pay it, making the next
frame slower still — the spiral of death. The bound converts that into the game
running slow, which is survivable and visible. Carrying the outstanding debt
into the next frame is what sustains the spiral, so `consumeFixedStep` discards
it when the budget is exhausted.

**`advance(double)` exists separately from `tick()` so time is testable.** Every
case drives an explicit delta; a test that had to sleep to exercise the
accumulator would be slow and flaky, and the accumulator is the part most worth
testing.

**One of my tests was wrong, and the failure was worth keeping.** It asserted
that five frames of `0.06` give three steps of `0.1`. Five additions of `0.06`
give `0.29999999999999993`, which is genuinely *two* whole steps — the
accumulator was right and the expectation was not. Rewritten with exact binary
fractions (`0.125` and `0.0625`), with a comment saying why: a test asserting an
exact step count across a boundary is otherwise asserting the behaviour of
floating point rather than the behaviour of the clock.

**`steady_clock`, not `system_clock`** — the frame delta must not jump when NTP
corrects the wall clock or the user changes timezone.

## Evidence

```
$ zig build test -Dtest=fast          # 12 clock cases among them
[doctest] test cases:     24 |     24 passed | 0 failed | 0 skipped
[doctest] assertions: 213091 | 213091 passed | 0 failed |      (x2 -- both targets)
```

Covered: scaled versus unscaled delta, pause not stopping unscaled time,
unpausing restoring the scale, the delta clamp, time never running backwards,
step counts, partial steps carrying, the interpolation alpha, the per-frame
bound and its debt being dropped, two clocks being independent, and `tick()`
being wired to the wall clock at all.
