// Time (T0057).
//
// Every system that moves needs the same answer to "how much time passed", and
// they must all get the *same* answer -- a system that calls the clock itself
// mid-frame sees a different delta from the one before it, which produces
// physics and animation that disagree by a fraction of a millisecond and drift.
// So the frame reads time once and hands it down.
//
// Three ideas the naive version gets wrong:
//
//   Scaled vs unscaled   Slow motion and pause scale *game* time. UI animation,
//                        the editor and the profiler must keep running, so both
//                        deltas are available and the caller chooses.
//   The delta clamp      A breakpoint, a stalled shader compile or a laptop
//                        waking up produces a multi-second delta. Unclamped,
//                        that teleports everything through walls in one step.
//   Fixed step           Physics needs a constant dt or it is not reproducible.
//                        The accumulator runs it 0..n times per frame, and the
//                        leftover fraction is exposed so rendering can
//                        interpolate rather than stutter.
#pragma once

#include <hp/Api.hpp>

#include <cstdint>

namespace hp {

/// A source of time for one timeline.
///
/// The engine runs more than one. The editor keeps ticking while the game is
/// paused -- otherwise the editor UI freezes with it, which is the bug that
/// makes play-mode pause unusable -- so editor and game each own a Clock and
/// only the game's is scaled by the player's pause.
class HP_API Clock {
public:
    Clock();

    /// Advances by the wall-clock time since the previous tick. Call once per
    /// frame, before anything reads the clock.
    void tick();

    /// Advances by an explicit raw delta, in seconds.
    ///
    /// The real work; `tick()` is this plus a clock read. Separated so time
    /// behaviour can be tested deterministically -- a test that had to sleep to
    /// exercise the accumulator would be slow and flaky.
    void advance(double rawDeltaSeconds);

    /// Seconds since the last tick, scaled and clamped. Zero while paused.
    double delta() const { return delta_; }

    /// Seconds since the last tick, clamped but not scaled. Keeps running while
    /// paused -- this is what UI and the editor use.
    double unscaledDelta() const { return unscaledDelta_; }

    double elapsed() const { return elapsed_; }

    double unscaledElapsed() const { return unscaledElapsed_; }

    std::uint64_t frame() const { return frame_; }

    /// 1.0 is normal, 0.5 half speed. Negative values are rejected: running time
    /// backwards is not supported and silently accepting it produces animation
    /// and physics that disagree about which way time goes.
    void setTimeScale(double scale);

    double timeScale() const { return timeScale_; }

    /// Pause is separate from a zero time scale so that unpausing restores the
    /// scale that was set, rather than snapping to 1.0.
    void setPaused(bool paused);

    bool paused() const { return paused_; }

    /// Longest raw delta accepted, in seconds. Anything larger is treated as
    /// this -- time is *lost*, deliberately, because the alternative is a
    /// simulation step so large that objects pass through each other.
    void setMaxDelta(double seconds);

    double maxDelta() const { return maxDelta_; }

    // --- fixed step ---------------------------------------------------------

    void setFixedStep(double seconds);

    double fixedStep() const { return fixedStep_; }

    /// Consumes one fixed step if enough time has accumulated.
    ///
    ///     while (clock.consumeFixedStep()) { simulate(clock.fixedStep()); }
    ///
    /// Bounded by `setMaxFixedStepsPerFrame`. Without that bound, a machine
    /// that cannot simulate as fast as real time accumulates more debt each
    /// frame and runs more steps to pay it, which makes the next frame slower
    /// still -- the spiral of death. The bound converts that into the game
    /// running slow, which is survivable and visible.
    bool consumeFixedStep();

    void setMaxFixedStepsPerFrame(int steps);

    int maxFixedStepsPerFrame() const { return maxFixedSteps_; }

    /// How far between fixed steps the current frame sits, in [0, 1). Render
    /// interpolation uses this; without it, rendering at a different rate from
    /// the fixed step visibly stutters even at high frame rates.
    double interpolationAlpha() const;

private:
    double delta_ = 0.0;
    double unscaledDelta_ = 0.0;
    double elapsed_ = 0.0;
    double unscaledElapsed_ = 0.0;
    std::uint64_t frame_ = 0;

    double timeScale_ = 1.0;
    bool paused_ = false;
    double maxDelta_ = 0.25;

    double fixedStep_ = 1.0 / 60.0;
    double accumulator_ = 0.0;
    int maxFixedSteps_ = 8;
    int stepsThisFrame_ = 0;

    std::uint64_t lastTickNanos_ = 0;
};

} // namespace hp
