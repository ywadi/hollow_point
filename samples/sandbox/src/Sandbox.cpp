// The sandbox gameplay module (T0013, stub).
//
// The fourth artifact, and the one easiest to forget: not the engine, not an
// app, but a *shared* library that both apps load at runtime and that T0048
// will reload while they run. It exists now, empty, because retrofitting a
// shared-library boundary means revisiting every gameplay type's linkage.
//
// This is the engine repo's own sample module, not a game. Real games are
// separate projects that build their own module against an installed engine --
// this one exists so the boundary is exercised by the engine's own CI.
//
// Gameplay is C++ against the engine's real headers -- not a C ABI (D12).
// Lockstep is what makes that safe: engine and module are always built by the
// same pinned toolchain with the same flags, so rich C++ crosses the boundary
// and no binding layer is needed. T0104 adds the build id that turns a
// mismatched module from silent corruption into a refusal at load.
#include <hp/Engine.hpp>

#if defined(_WIN32)
#define HP_SANDBOX_EXPORT __declspec(dllexport)
#else
#define HP_SANDBOX_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

/// Called by the host after loading the module. T0048 defines the real
/// lifecycle; this proves the module can reach engine state.
HP_SANDBOX_EXPORT void hpSandboxAttach() {
    hp::engineRegisterConsumer("sandbox");
}

/// Reports the engine's consumer count as the module sees it. The host compares
/// this against its own view: one shared engine means they agree.
HP_SANDBOX_EXPORT unsigned hpSandboxConsumerCount() {
    return hp::engineConsumerCount();
}
}
