// Profiling macro surface (T0019).
//
// Tracy-shaped, but Tracy-free. The names and semantics deliberately mirror
// Tracy's (scoped zone, named zone, frame mark, GPU zone) so that wiring the
// real client in T0029 is a mapping exercise rather than a redesign of every
// call site.
//
// **No engine dependencies, by design.** This header includes nothing from the
// engine, so the lowest-level utility can use it without creating a dependency
// cycle. It does not even include <hp/Api.hpp>: with profiling disabled these
// expand to nothing at all, and with it enabled they will expand to Tracy's own
// macros, neither of which needs an export annotation.
//
// Two switches, and they are not the same mechanism (D14-adjacent, see T0029):
//
//   HP_PROFILING     compile time. Off means the instrumentation does not exist
//                    -- no branch, no symbol, no argument evaluation. This is
//                    the one that preserves shipped-build performance, and it
//                    is off by default.
//   capture state    run time, once Tracy is present. TRACY_ON_DEMAND collects
//                    nothing until a profiler connects, which is what an editor
//                    toggle would control. It cannot recover shipped-build cost,
//                    because the instrumentation is still compiled in.
//
// An editor checkbox controls the second. Say so in the UI, or someone will
// expect it to do the first.
#pragma once

#if defined(HP_PROFILING)

// T0029 replaces this block with Tracy's client. The macros below keep their
// names and meanings, so no call site changes when that happens.
#error "HP_PROFILING is enabled but no profiler backend is wired up yet -- see T0029"

#else

// Disabled: every macro must vanish completely.
//
// Note what is *not* here: no `do {} while(0)`, no `(void)sizeof(...)`, no
// unused-variable trick that mentions the arguments. Referring to the arguments
// at all would evaluate them, and `HP_PROFILE_ZONE_NAMED(expensive())` must not
// call expensive(). That is the difference between instrumentation that is
// absent and instrumentation that is merely skipped, and it is the entire point
// of the compile-time switch.
//
// The cost of this is that an argument used *only* inside a profiling macro
// looks unused to the compiler when profiling is off. That is the correct
// trade -- a warning is cheaper than a call.

/// Scope-lifetime zone named after the enclosing function.
#define HP_PROFILE_ZONE()

/// Scope-lifetime zone with an explicit literal name.
#define HP_PROFILE_ZONE_NAMED(name)

/// Marks the end of a frame. Exactly one per frame, from the main loop.
#define HP_PROFILE_FRAME()

/// Marks the end of a named frame, for a loop that is not *the* frame loop.
#define HP_PROFILE_FRAME_NAMED(name)

/// GPU-timed zone. Needs a command list and a GPU context (T0030), and is
/// reachable from gameplay only through the render extension seam (T0094).
#define HP_PROFILE_GPU_ZONE(name)

/// Tags a value onto the current zone, for correlating a spike with what caused
/// it -- entity counts, draw calls, bytes loaded.
#define HP_PROFILE_VALUE(name, value)

/// Names the current thread in the profiler. Call once, early, per thread.
#define HP_PROFILE_THREAD(name)

#endif
