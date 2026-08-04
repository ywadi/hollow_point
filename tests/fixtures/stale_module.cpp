// A module built against a different engine, for T0104.5.
//
// This is what the ticket is actually about: not a corrupt file or a missing
// symbol, but a perfectly well-formed module that loads, resolves everything,
// and is wrong. It exports the stamp symbol with a plausible-looking id that is
// simply not this engine's, which is exactly what a module compiled before a
// header change looks like.
//
// Deliberately *not* built through hp_add_gameplay_module(), because that helper
// stamps the correct id — the whole point of it. Producing a genuinely stale
// module means bypassing the mechanism that makes staleness impossible.
//
// It also exports an entry point that records having been called. The test
// asserts that flag is never set: refusing after running the module's code
// would be a diagnostic, not a guard, since by then it has already read fields
// at the wrong offsets.

#if defined(_WIN32)
#define HP_STALE_EXPORT __declspec(dllexport)
#else
#define HP_STALE_EXPORT __attribute__((visibility("default")))
#endif

namespace {
bool g_entry_was_called = false;
}

extern "C" {

/// The stamp, with an id belonging to no build of this engine. Sixteen hex
/// characters so it has the same shape as a real one — a refusal that only
/// triggers on obviously malformed input would prove nothing.
HP_STALE_EXPORT const char* hp_module_build_id() {
    return "dead0000beef0000";
}

/// Stands in for whatever a real module's attach point would be.
HP_STALE_EXPORT void hp_stale_entry() {
    g_entry_was_called = true;
}

/// So the test can assert the entry point was never reached.
HP_STALE_EXPORT bool hp_stale_entry_was_called() {
    return g_entry_was_called;
}

} // extern "C"
