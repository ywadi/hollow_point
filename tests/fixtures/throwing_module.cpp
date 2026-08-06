// A gameplay module whose entry point throws (T0048, T0127).
//
// The guard in HP_GAMEPLAY_MODULE is the only thing standing between this and
// an exception unwinding into the loader, which is undefined behaviour rather
// than a stack trace. That guard is easy to write and easy to believe in
// without evidence, so this fixture exists to make it fail if it is ever
// removed.
//
// It throws a `std::` type on purpose. That is the case T0127 measured as
// *unable* to be caught by type on ELF -- so `catch (...)` is not a fallback
// here, it is the only handler that can possibly run. A guard written as
// `catch (const std::exception&)` would compile, look correct, pass on Windows,
// and let the exception escape on Linux.
#include <hp/ModuleHost.hpp>

#include <stdexcept>

namespace {

/// Records that onUnload still ran after onLoad threw. If the guard swallowed
/// the exception properly the module is live and unloadable; if it did not, the
/// process is already gone.
bool g_unloaded = false;

void onLoad(hp::ModuleContext&) {
    throw std::runtime_error("a gameplay module entry point threw on purpose");
}

void onUnload(hp::ModuleContext&) {
    g_unloaded = true;
}

/// The per-frame hook throws too (T0062.11's guard).
///
/// It is a separate `invokeGuarded` overload — a different signature means a
/// different function, and "the other one is guarded" is not evidence about
/// this one. It also throws *every* frame, which is what makes the host's
/// behaviour worth asserting: the guard must swallow rather than accumulate.
void onUpdate(hp::ModuleContext&, double) {
    throw std::runtime_error("a gameplay module update hook threw on purpose");
}

} // namespace

extern "C" {

/// So the test can confirm the module remained usable after its onLoad threw.
HP_EXPORT bool hp_throwing_module_unloaded() {
    return g_unloaded;
}
}

HP_GAMEPLAY_MODULE("throwing", onLoad, onUnload, onUpdate)
