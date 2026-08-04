// A module that is hostile to being unloaded, for T0105.1's regression test.
//
// The `static std::string` is the whole point: it needs destruction, so the
// module registers a destructor with __cxa_atexit at load. If nothing retires
// that registration when the module is unloaded, the C runtime walks into
// unmapped memory at process exit and the process dies — long after the
// dlclose, with a stack that points at nothing.
//
// This fixture is compiled twice, into two modules that differ only by whether
// the finalizer is present, so the test can prove the fix does something rather
// than merely observing a pass. See tests/CMakeLists.txt.
//
// It deliberately contains no engine code at all. T0095 reduced the original
// crash to exactly this, and keeping the fixture minimal keeps the test's
// failure unambiguous: if it crashes, it is the unload lifecycle, not us.

#include <string>

static std::string g_needs_destruction = "hello";

#if defined(_WIN32)
#define HP_FIXTURE_EXPORT __declspec(dllexport)
#else
#define HP_FIXTURE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" HP_FIXTURE_EXPORT const char* hp_unload_fixture_value() {
    return g_needs_destruction.c_str();
}

// The module under test gets the real finalizer compiled in by
// hp_add_gameplay_module(); the control copy is built without it. Nothing is
// conditional inside this file, so the two builds share identical source.
