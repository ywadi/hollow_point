// The profiling macro surface (T0019).
//
// Bucket: fast. Pure compile-time behaviour and a counter.
//
// What is worth testing here is not that the macros exist -- the compiler
// settles that -- but that when profiling is disabled they are *absent* rather
// than merely inert. The distinction is the entire point of the compile-time
// switch: instrumentation that is skipped at run time still costs a branch, an
// argument evaluation and a symbol in a shipped build.
//
// The zero-code half is verified at build time by comparing emitted assembly
// with and without the macros (identical -- see the ticket). That cannot be
// asserted from inside a running test, so this covers the half that can be:
// arguments must never be evaluated.

#include <doctest/doctest.h>

#include <hp/Profiling.hpp>

namespace {

int g_sideEffects = 0;

/// Returns a name *and* records that it was called. If a disabled macro
/// evaluates its arguments, this counter moves.
const char* nameWithSideEffect() {
    ++g_sideEffects;
    return "expensive";
}

int valueWithSideEffect() {
    ++g_sideEffects;
    return 42;
}

} // namespace

TEST_CASE("disabled profiling macros do not evaluate their arguments") {
    g_sideEffects = 0;

    HP_PROFILE_ZONE();
    HP_PROFILE_ZONE_NAMED(nameWithSideEffect());
    HP_PROFILE_VALUE(nameWithSideEffect(), valueWithSideEffect());
    HP_PROFILE_GPU_ZONE(nameWithSideEffect());
    HP_PROFILE_THREAD(nameWithSideEffect());
    HP_PROFILE_FRAME_NAMED(nameWithSideEffect());
    HP_PROFILE_FRAME();

#if defined(HP_PROFILING)
    // With a profiler compiled in, arguments are genuinely used. This suite is
    // built without it (the default), so this branch documents the intent
    // rather than being exercised -- and stops the test being read as "the
    // macros must never evaluate anything, ever".
    WARN_MESSAGE(true, "built with HP_PROFILING: argument evaluation is expected");
#else
    CHECK_MESSAGE(g_sideEffects == 0,
                  "a disabled profiling macro evaluated its argument -- it is skipping "
                  "instrumentation rather than removing it, which leaves the cost in "
                  "shipped builds");
#endif
}

TEST_CASE("profiling macros are usable as statements in ordinary control flow") {
    // Guards against a definition that only compiles at the top of a function,
    // or that swallows a following `else`. An empty macro is fine here; a
    // careless `do {} while(0);` or a stray semicolon is not.
    int taken = 0;

    for (int i = 0; i < 3; ++i) {
        HP_PROFILE_ZONE_NAMED("loop body");
        if (i % 2 == 0) {
            HP_PROFILE_ZONE();
            ++taken;
        } else {
            HP_PROFILE_ZONE();
        }
    }

    CHECK(taken == 2);
}
