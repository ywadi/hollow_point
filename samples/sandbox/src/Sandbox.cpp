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

// --- reflection across the boundary (T0053) ----------------------------------
//
// A gameplay module registers its own component types into the engine's meta
// context, and reads types the engine (or the host) registered. Both directions
// have to work through one shared context, or the inspector and serializer see
// a different set of types depending on which side of the boundary they run.
//
// This is also the demonstration that `hp::adoptMetaContext()` is not optional:
// remove the call in hpSandboxRegisterTypes and every case in the boundary suite
// that touches reflection fails, with no crash and no diagnostic.

#include <hp/Reflect.hpp>

#include <entt/meta/meta.hpp>

using namespace entt::literals;

namespace {

/// A component owned by the gameplay module, not the engine. Its registration
/// lives and dies with the module, which is what makes deregistration on unload
/// necessary rather than tidy.
struct SandboxHealth {
    int current = 50;
    int maximum = 75;
};

} // namespace

extern "C" {

/// Adopts the engine's meta context and registers this module's types into it.
HP_SANDBOX_EXPORT void hpSandboxRegisterTypes() {
    hp::adoptMetaContext();
    hp::reflect<SandboxHealth>("SandboxHealth")
        .property<&SandboxHealth::current>("current")
        .meta({.min = 0.0, .max = 999.0, .tooltip = "module-owned"})
        .property<&SandboxHealth::maximum>("maximum");
}

/// Resolves a type registered on the *other* side of the boundary and reads a
/// property from it, returning the value so the host can check it.
///
/// Takes the value by pointer rather than returning a meta_any, because the
/// point is to prove the reflection worked, not to move entt types through a C
/// entry point.
HP_SANDBOX_EXPORT bool hpSandboxReadHostProperty(const char* type_name,
                                                 const char* property_name,
                                                 float* out_value) {
    const entt::meta_type type = hp::resolveType(type_name);
    if (!type || out_value == nullptr) {
        return false;
    }
    auto data = type.data(entt::hashed_string{property_name});
    if (!data) {
        return false;
    }
    // Construct a default instance through reflection alone -- the module has
    // never seen this type's definition.
    entt::meta_any instance = type.construct();
    if (!instance) {
        return false;
    }
    const entt::meta_any value = data.get(instance);
    if (!value) {
        return false;
    }
    *out_value = value.cast<float>();
    return true;
}

/// Removes this module's types from the shared context.
///
/// Mandatory before unload: entt::meta holds raw function pointers and unowned
/// name literals that live in this module's image, so leaving them registered
/// after the image goes away is the same class of dangling-pointer failure
/// T0105.1 fixed for static destructors.
HP_SANDBOX_EXPORT void hpSandboxForgetTypes() {
    hp::forgetType("SandboxHealth");
}
}
