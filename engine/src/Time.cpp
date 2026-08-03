#include <hp/Time.hpp>

#include <hp/Profiling.hpp>

#include <algorithm>
#include <chrono>

namespace hp {
namespace {

std::uint64_t nowNanos() {
    // steady_clock, not system_clock: the frame delta must not jump when the
    // machine's wall clock is corrected by NTP or the user changes timezone.
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

} // namespace

Clock::Clock() : lastTickNanos_(nowNanos()) {}

void Clock::tick() {
    const std::uint64_t now = nowNanos();
    const std::uint64_t elapsedNanos = now - lastTickNanos_;
    lastTickNanos_ = now;
    advance(static_cast<double>(elapsedNanos) * 1e-9);
}

void Clock::advance(double rawDeltaSeconds) {
    HP_PROFILE_ZONE();

    // A negative raw delta should be impossible from a steady clock, but
    // advance() is public and takes whatever it is given.
    rawDeltaSeconds = std::max(0.0, rawDeltaSeconds);

    unscaledDelta_ = std::min(rawDeltaSeconds, maxDelta_);
    delta_ = paused_ ? 0.0 : unscaledDelta_ * timeScale_;

    unscaledElapsed_ += unscaledDelta_;
    elapsed_ += delta_;
    ++frame_;

    accumulator_ += delta_;
    stepsThisFrame_ = 0;
}

void Clock::setTimeScale(double scale) {
    timeScale_ = std::max(0.0, scale);
}

void Clock::setPaused(bool paused) {
    paused_ = paused;
}

void Clock::setMaxDelta(double seconds) {
    maxDelta_ = std::max(0.0, seconds);
}

void Clock::setFixedStep(double seconds) {
    // A zero or negative fixed step would make consumeFixedStep loop forever.
    if (seconds > 0.0) {
        fixedStep_ = seconds;
    }
}

void Clock::setMaxFixedStepsPerFrame(int steps) {
    maxFixedSteps_ = std::max(1, steps);
}

bool Clock::consumeFixedStep() {
    if (accumulator_ < fixedStep_) {
        return false;
    }
    if (stepsThisFrame_ >= maxFixedSteps_) {
        // Out of budget. Drop the outstanding debt rather than carrying it into
        // the next frame -- carrying it is what makes the spiral self-sustaining.
        // The game runs slow; it does not fall over.
        accumulator_ = 0.0;
        return false;
    }
    accumulator_ -= fixedStep_;
    ++stepsThisFrame_;
    return true;
}

double Clock::interpolationAlpha() const {
    return fixedStep_ > 0.0 ? accumulator_ / fixedStep_ : 0.0;
}

} // namespace hp
