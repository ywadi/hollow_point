#include <chrono>
#include <hp/Application.hpp>
#include <thread>

#include <hp/Event.hpp>
#include <hp/Log.hpp>
#include <hp/Profiling.hpp>
#include <hp/Scene.hpp>

namespace hp {
namespace {
const LogCategory kLog("app");
} // namespace

Application::Application(ApplicationConfig config) : config_(std::move(config)) {}

Application::~Application() = default;

void Application::dispatch(Event& event) {
    HP_PROFILE_ZONE();
    layers_.dispatch(event);
    if (!event.isConsumed()) {
        onEvent(event);
    }
}

void Application::setServices(const ModuleServices& services) {
    // Stored on the module host rather than here. Two copies of the same four
    // pointers is two things to keep in step, and the one that would drift is
    // whichever the frame loop does not read.
    modules_.setServices(services);
}

const ModuleServices& Application::services() const {
    return modules_.services();
}

void Application::requestExit(int exitCode) {
    // Only records intent. The loop finishes the frame it is in, so a hook can
    // call this without the ground disappearing underneath it.
    exitCode_ = exitCode;
    running_ = false;
}

namespace {

/// Waits until `deadline`, accurately enough to be worth doing.
///
/// Sleep-then-spin, and the split is the point (T0110.2). A plain
/// `sleep_until` has millisecond-scale jitter on a general-purpose OS -- it
/// wakes up *at least* that late, never early -- so a naive cap produces frame
/// times that alternate long and short, which reads as worse pacing than no cap
/// at all. Sleeping to just short of the deadline and busy-waiting the last
/// slice gives back the accuracy while keeping almost all of the CPU saving.
///
/// The slack is deliberately generous. Undershooting costs a little spin;
/// overshooting costs a missed frame, and a missed frame is visible.
void waitUntil(std::chrono::steady_clock::time_point deadline) {
    constexpr auto kSpinSlack = std::chrono::microseconds(1500);
    const auto sleepUntil = deadline - kSpinSlack;
    if (std::chrono::steady_clock::now() < sleepUntil) {
        std::this_thread::sleep_until(sleepUntil);
    }
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
}

} // namespace

int Application::run() {
    HP_PROFILE_ZONE_NAMED("Application::run");
    HP_PROFILE_THREAD("main");

    HP_LOG_INFO(kLog, "starting {}", config_.name);

    if (!config_.headless) {
        WindowConfig windowConfig = config_.window;
        if (windowConfig.title.empty()) {
            windowConfig.title = config_.name;
        }
        window_ = Window::create(windowConfig);
        if (!window_) {
            HP_LOG_ERROR(kLog, "could not create a window; aborting startup");
            return 1;
        }
    }

    running_ = true;
    exitCode_ = 0;

    onStartup();

    while (running_) {
        HP_PROFILE_ZONE_NAMED("frame");

        // Time first, so everything in this frame sees one delta. A system that
        // reads the clock itself mid-frame gets a different answer from the one
        // before it, and the two drift apart -- see T0057.
        clock_.tick();
        const double delta = clock_.delta();

        {
            HP_PROFILE_ZONE_NAMED("poll");
            if (window_) {
                const WindowEvents events = window_->pumpEvents([this](Event& e) {
                    // The action layer sees raw input before the layers do, and
                    // consumes what a context claims (T0068). Edges are
                    // accumulated here rather than sampled at a frame boundary,
                    // which is what makes a key pressed and released inside one
                    // frame still register.
                    // Focus is observed here rather than in a layer: the cap
                    // and the input reset are the application's business, and a
                    // layer that happened to consume the event would take them
                    // away (T0110.3).
                    if (e.type() == EventType::WindowFocusGained ||
                        e.type() == EventType::WindowFocusLost) {
                        focused_ = (e.type() == EventType::WindowFocusGained);
                        if (!focused_) {
                            // A window that loses focus while a key is held
                            // never receives the key-up, so without this the
                            // action stays held forever and the character walks
                            // into a wall while the player is in another
                            // application. This is the hook T0068 left for
                            // exactly this decision.
                            input_.reset();
                        }
                        HP_LOG_DEBUG(kLog, "focus {}", focused_ ? "gained" : "lost");
                    }

                    if (input_.onEvent(e)) {
                        return;
                    }
                    dispatch(e);
                });
                if (events.resized) {
                    // Delivered as an event so layers can react, and as a hook
                    // so an application need not add a layer just to resize.
                    WindowResizeEvent resize(events.width, events.height);
                    dispatch(resize);
                    onResize(events.width, events.height);
                }
                if (events.closeRequested) {
                    WindowCloseEvent close;
                    dispatch(close);
                    if (!close.isConsumed()) {
                        // Unconsumed means nobody objected -- a layer wanting
                        // "really quit?" consumes it and asks.
                        HP_LOG_INFO(kLog, "window close requested");
                        requestExit(0);
                    }
                }
            }
        }

        // Phase 3 -- fixed update. Runs 0..n times; the accumulator and its
        // per-frame cap live in Clock (T0057). Anything that must be
        // reproducible goes here, because a variable delta gives a different
        // answer on a faster machine.
        {
            HP_PROFILE_ZONE_NAMED("fixed update");
            const double step = clock_.fixedStep();
            while (clock_.consumeFixedStep()) {
                HP_PROFILE_ZONE_NAMED("fixed step");

                // 3a. Input snapshot (T0068). Each step must see one unchanging
                // view of input; sampling live mid-block would make two steps in
                // the same frame disagree.
                input_.snapshot();

                // 3b. Simulation.
                layers_.fixedUpdate(step);
                onFixedUpdate(step);

                // 3c. Physics step, and 3d. post-physics resolution (T0051).
                // Both sit inside this loop on purpose: physics must advance
                // once per fixed step, not once per frame.
            }
        }

        {
            HP_PROFILE_ZONE_NAMED("update");
            // Bottom-up: the world simulates before the interface over it.
            layers_.update(delta);
            // Phase 4 proper -- gameplay (T0062.11). Between the layers that
            // host it and the application's own hook, so an app observing its
            // world in onUpdate sees this frame's gameplay rather than last
            // frame's. Costs one null check per loaded module when none
            // implements the hook.
            modules_.update(delta);
            onUpdate(delta);
        }

        {
            // Phase 5 -- structural apply (T0021). Entity creation and
            // destruction queued during update takes effect here, before
            // transforms and before rendering. Deliberately *not* deferred to
            // the end-of-frame safe point below: a destroyed entity must not
            // survive to be drawn one last time.
            HP_PROFILE_ZONE_NAMED("structural apply");
        }

        {
            // Phase 6 -- deferred queues drain (T0072 signals, T0075 message
            // bus). Drain-until-empty with the iteration cap T0075 describes,
            // since a handler may post more work. Draining here rather than
            // ad hoc is what lets the safe point below assert the queues are
            // empty before a module reload swaps the types their payloads use.
            HP_PROFILE_ZONE_NAMED("deferred drain");
        }

        {
            // Phase 7 -- transform propagation (T0101), first of two. Everything
            // that moved during update gets its world transform before anything
            // reads one.
            //
            // Empty until T0157: the loop had no scene to propagate, because
            // nothing told an `Application` which scene it was running. It does
            // now (`setServices`), and without this a module moving a transform
            // at phase 4 changes a `Transform` that no renderer ever reads --
            // the entity sits still and nothing anywhere reports a problem.
            HP_PROFILE_ZONE_NAMED("transform propagate");
            if (Scene* scene = services().scene; scene != nullptr) {
                scene->propagateTransforms();
            }
        }

        // Phase 8 -- late update. Followers read *final* transforms here. This
        // phase exists because the alternative -- cameras updating in onUpdate
        // alongside what they follow -- reads this frame's or last frame's
        // position depending on registration order, which is jitter that
        // profiles as nothing.
        {
            HP_PROFILE_ZONE_NAMED("late update");
            layers_.lateUpdate(delta);
            onLateUpdate(delta);
        }

        {
            // Phase 9 -- transform propagation again (T0101), for what late
            // update moved. A camera that re-parents or moves itself here would
            // otherwise render one frame stale, which is the same bug phase 8
            // exists to prevent, moved one step later.
            //
            // A clean scene walks the hierarchy and writes nothing, so the
            // second pass is not a second cost in the ordinary frame where
            // late update moved nothing.
            HP_PROFILE_ZONE_NAMED("transform propagate (late)");
            if (Scene* scene = services().scene; scene != nullptr) {
                scene->propagateTransforms();
            }
        }

        {
            HP_PROFILE_ZONE_NAMED("render");
            // Reads Clock::interpolationAlpha() once T0025 lands: rendering at a
            // different rate from the fixed step stutters visibly without it.
            layers_.render();
            onRender();
        }

        {
            // Phase 11 -- present. T0025 submits, T0110 decides how; present
            // mode and pacing are an unowned default today (Diligent picks
            // IMMEDIATE), which is exactly what that ticket exists to fix.
            HP_PROFILE_ZONE_NAMED("present");
        }

        {
            // Phase 12 -- the end-of-frame safe point. Nothing is iterating and
            // nothing is mid-draw, which is the one moment structural change to
            // the *world itself* is safe: gameplay module reload (T0048), asset
            // reload swap (T0058) and scene transitions (T0077) all apply here
            // and nowhere else.
            //
            // Three tickets independently need this same point, which is why it
            // is one point rather than three. When they land it must assert the
            // phase-6 queues are drained and no jobs are in flight (T0026)
            // before doing anything -- a reload with a module-typed payload
            // still queued is the hazard T0075's review note describes.
            HP_PROFILE_ZONE_NAMED("frame safe point");

            // Gameplay module reload (T0048). Here and nowhere else: nothing is
            // iterating and nothing is mid-draw, so swapping code out from
            // under the process is safe. Costs one stat() per loaded module in
            // the overwhelmingly common case where nothing changed.
            //
            // Phase 6 is empty today, so there is nothing to assert drained
            // yet. When T0072/T0075 fill it, the assertion goes here, before
            // the reload -- a queued module-typed payload outliving the module
            // is a use-after-free in a type that no longer exists.
            modules_.reloadChanged();
        }

        HP_PROFILE_FRAME();

        // Phase 13 tail: hold the frame rate down (T0110.2).
        //
        // After everything else, including present -- capping earlier would
        // measure the wait as part of the frame and feed a wrong delta to the
        // next one. The cap is *independent of vsync*: vsync does nothing for a
        // hidden window, and this does nothing about tearing.
        {
            const std::uint32_t cap =
                focused_ ? config_.frameRateCap
                         : (config_.backgroundFrameRateCap != 0 ? config_.backgroundFrameRateCap
                                                                : config_.frameRateCap);
            if (cap != 0) {
                const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(1.0 / static_cast<double>(cap)));
                // From the previous deadline rather than from "now", so the cap
                // holds an average rate instead of drifting slower every frame
                // by however long the wait overshot.
                const auto nowNs = static_cast<std::uint64_t>(
                    std::chrono::steady_clock::now().time_since_epoch().count());
                const auto periodNs = static_cast<std::uint64_t>(period.count());
                if (frameDeadlineNs_ == 0 || frameDeadlineNs_ + periodNs < nowNs) {
                    frameDeadlineNs_ = nowNs; // first frame, or we fell far behind
                }
                frameDeadlineNs_ += periodNs;
                waitUntil(std::chrono::steady_clock::time_point{
                    std::chrono::steady_clock::duration{frameDeadlineNs_}});
            } else {
                frameDeadlineNs_ = 0;
            }
        }

        if (config_.exitAfterFrames && clock_.frame() >= *config_.exitAfterFrames) {
            HP_LOG_DEBUG(kLog, "frame budget reached ({}), stopping", *config_.exitAfterFrames);
            running_ = false;
        }
    }

    // Before anything is torn down: the app still holds everything it created.
    onShutdown();

    // Then layers, top-down, while the window still exists -- a layer holding a
    // GPU resource must release it before the surface it was made against
    // (T0025).
    layers_.clear();

    // And only then the window. Layers detach in onShutdown, so anything
    // holding a swap chain or a GPU resource is gone before the surface it was
    // created against -- the ordering T0025 will depend on.
    window_.reset();

    HP_LOG_INFO(kLog, "{} ran {} frame(s) in {:.3f}s, exit {}", config_.name, clock_.frame(),
                clock_.unscaledElapsed(), exitCode_);
    logFlush();

    return exitCode_;
}

} // namespace hp
