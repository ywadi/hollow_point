# `<hp/Time.hpp>`

*Generated from `engine/include/hp/Time.hpp` — do not edit.*

```cpp
#include <hp/Time.hpp>
```

21 public declaration(s), 10 documented.

## `Clock`

```cpp
class Clock
```

 A source of time for one timeline.

 The engine runs more than one. The editor keeps ticking while the game is
 paused -- otherwise the editor UI freezes with it, which is the bug that
 makes play-mode pause unusable -- so editor and game each own a Clock and
 only the game's is scaled by the player's pause.

## `Clock::Clock`

```cpp
Clock()
```

*No documentation comment.*

## `Clock::tick`

```cpp
void tick()
```

 Advances by the wall-clock time since the previous tick. Call once per
 frame, before anything reads the clock.

## `Clock::advance`

```cpp
void advance(double rawDeltaSeconds)
```

 Advances by an explicit raw delta, in seconds.

 The real work; `tick()` is this plus a clock read. Separated so time
 behaviour can be tested deterministically -- a test that had to sleep to
 exercise the accumulator would be slow and flaky.

## `Clock::delta`

```cpp
double delta() const
```

 Seconds since the last tick, scaled and clamped. Zero while paused.

## `Clock::unscaledDelta`

```cpp
double unscaledDelta() const
```

 Seconds since the last tick, clamped but not scaled. Keeps running while
 paused -- this is what UI and the editor use.

## `Clock::elapsed`

```cpp
double elapsed() const
```

*No documentation comment.*

## `Clock::unscaledElapsed`

```cpp
double unscaledElapsed() const
```

*No documentation comment.*

## `Clock::frame`

```cpp
std::uint64_t frame() const
```

*No documentation comment.*

## `Clock::setTimeScale`

```cpp
void setTimeScale(double scale)
```

 1.0 is normal, 0.5 half speed. Negative values are rejected: running time
 backwards is not supported and silently accepting it produces animation
 and physics that disagree about which way time goes.

## `Clock::timeScale`

```cpp
double timeScale() const
```

*No documentation comment.*

## `Clock::setPaused`

```cpp
void setPaused(bool paused)
```

 Pause is separate from a zero time scale so that unpausing restores the
 scale that was set, rather than snapping to 1.0.

## `Clock::paused`

```cpp
bool paused() const
```

*No documentation comment.*

## `Clock::setMaxDelta`

```cpp
void setMaxDelta(double seconds)
```

 Longest raw delta accepted, in seconds. Anything larger is treated as
 this -- time is *lost*, deliberately, because the alternative is a
 simulation step so large that objects pass through each other.

## `Clock::maxDelta`

```cpp
double maxDelta() const
```

*No documentation comment.*

## `Clock::setFixedStep`

```cpp
void setFixedStep(double seconds)
```

*No documentation comment.*

## `Clock::fixedStep`

```cpp
double fixedStep() const
```

*No documentation comment.*

## `Clock::consumeFixedStep`

```cpp
bool consumeFixedStep()
```

 Consumes one fixed step if enough time has accumulated.

     while (clock.consumeFixedStep()) { simulate(clock.fixedStep()); }

 Bounded by `setMaxFixedStepsPerFrame`. Without that bound, a machine
 that cannot simulate as fast as real time accumulates more debt each
 frame and runs more steps to pay it, which makes the next frame slower
 still -- the spiral of death. The bound converts that into the game
 running slow, which is survivable and visible.

## `Clock::setMaxFixedStepsPerFrame`

```cpp
void setMaxFixedStepsPerFrame(int steps)
```

*No documentation comment.*

## `Clock::maxFixedStepsPerFrame`

```cpp
int maxFixedStepsPerFrame() const
```

*No documentation comment.*

## `Clock::interpolationAlpha`

```cpp
double interpolationAlpha() const
```

 How far between fixed steps the current frame sits, in [0, 1). Render
 interpolation uses this; without it, rendering at a different rate from
 the fixed step visibly stutters even at high frame rates.
