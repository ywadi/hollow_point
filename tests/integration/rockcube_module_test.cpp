// The rock cube sample's behaviour, across the real module boundary (T0157.6).
//
// **What is under test is not the cube — it is that a gameplay module can move
// something at all.** Before T0157, `ModuleContext` carried a generation and a
// name, `ModuleApi` had no per-frame hook, and nothing handed a module a scene.
// A module could register reflected types and nothing else, which is why T0062
// records 62.11 as blocking every other subtask in it.
//
// So this drives the chain end to end, with the real `libhp_rockcube` opened at
// run time:
//
//   1. the module registers `RockCubeSpin` into the **shared** meta context, so
//      a scene loaded afterwards by the *engine* materialises a component the
//      engine has never seen — D23's "no separate path for gameplay types",
//      proven across a `dlopen` boundary rather than within one image;
//   2. `ModuleHost::update` reaches the module's phase-4 hook;
//   3. the hook moves a `Transform` through `setLocalTransform`, so the change
//      is visible to propagation rather than silently lost.
//
// **No device is needed and none is lent**, which is also worth exercising: the
// sample's own content load bails out cleanly on a null device and says so
// rather than crashing. The scene here is a two-line document written in the
// test, not the sample's, because what is being measured is the hook and not
// the content.
//
// Bucket: integration. Opens a real shared library at run time.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Log.hpp>
#include <hp/ModuleHost.hpp>
#include <hp/Reflect.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneSerialize.hpp>

#include <cmath>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace {

#if defined(_WIN32)
constexpr const char* PATH_SEP = "\\";
#else
constexpr const char* PATH_SEP = "/";
#endif

std::string exe_dir() {
#if defined(_WIN32)
    char buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    std::string path(buffer, length);
#else
    char buffer[PATH_MAX] = {};
    const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof buffer - 1);
    std::string path(buffer, length > 0 ? static_cast<std::size_t>(length) : 0);
#endif
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

/// A relative path, never a copy beside the test binary. A POST_BUILD copy only
/// runs when *this* target relinks, so editing only the module leaves a stale
/// copy — a guard that guards nothing. The sandbox test records the same trap.
std::string rockcube_path() {
    return exe_dir() + PATH_SEP + ".." + PATH_SEP + "samples" + PATH_SEP + "rockcube" + PATH_SEP +
           HP_ROCKCUBE_MODULE_NAME;
}

/// One entity carrying a component only the gameplay module knows about.
///
/// `z = -5` mirrors the sample scene: the camera looks down its own -Z since
/// T0165, so that is where "in front" is. Nothing here renders, so the value is
/// documentation rather than data — which is exactly why it is worth keeping
/// honest, since a fixture is what the next reader copies.
constexpr const char* kSpinnerScene = R"(version: 1
entities:
  - name: Spinner
    components:
      Transform:
        position: [0, 0, -5]
        rotation: [0, 0, 0, 1]
        scale: [1, 1, 1]
      RockCubeSpin:
        axis: [0, 1, 0]
        degreesPerSecond: 90
)";

} // namespace

TEST_CASE("a gameplay module moves a transform at frame phase 4" *
          doctest::test_suite("module")) {
    hp::AssetPool pool;
    hp::ModuleHost host;

    // **The scene is scoped inside the module's lifetime, and that is a
    // correctness requirement rather than tidiness.**
    //
    // A `RockCubeSpin` pool inside the registry holds type-erased operations
    // that live in the *module's* image. Unload the module first and
    // `~basic_registry` calls into memory that is no longer mapped —
    // reproduced here as a SIGSEGV in `~basic_registry` while writing this
    // test, which is the same hazard the decision log records for T0062:
    // "genuinely dangles module-side vtables and pools". `forgetType` does not
    // help; it removes the *reflection*, and entt's storage is a separate
    // thing that nothing removes.
    //
    // `Application::run` already gets this right — it calls `layers_.clear()`
    // before `modules_` is destroyed, so the scene dies while the module is
    // still mapped. That ordering is currently justified by GPU-resource
    // lifetime and happens to be what this needs too. See T0157's findings.
    {
        hp::Scene scene;

        hp::ModuleServices services;
        services.scene = &scene;
        services.assets = &pool;
        // Deliberately no device. The sample's content load must decline
        // cleanly and say so, which is what a headless host looks like.
        host.setServices(services);

        const hp::ModuleLoadResult loadResult = host.load(rockcube_path());
        REQUIRE_MESSAGE(loadResult.ok, loadResult.message);
        REQUIRE(host.names().size() == 1);
        CHECK(host.names()[0] == "rockcube");

        // The module registered its component type into the engine's meta
        // context. Without `adoptMetaContext` this resolves to nothing, with no
        // crash and no diagnostic (T0053) — so the scene below would load with
        // the component preserved as text and the entity would never move.
        CHECK(static_cast<bool>(hp::resolveType("RockCubeSpin")));

        const hp::SceneLoadResult loaded = hp::loadSceneFromString(scene, kSpinnerScene, "<test>");
        REQUIRE(loaded.status == hp::SceneLoadStatus::Ok);
        CHECK(loaded.entities == 1);
        // Zero is the assertion that matters: the engine materialised a
        // component whose C++ definition lives in a shared library it was not
        // built against.
        CHECK(loaded.unknownComponents == 0);

        REQUIRE(scene.roots().size() == 1);
        const hp::Entity spinner = scene.roots().front();
        CHECK(scene.registry().get<hp::Transform>(spinner.raw()).rotation.q.w ==
              doctest::Approx(1.0F));

        // Half a second at 90 degrees per second is 45 degrees about +Y, which
        // is (0, sin(22.5deg), 0, cos(22.5deg)). Split into two steps so the
        // *composition* is what is measured — a hook that assigned an absolute
        // angle from the delta would pass a single-step check and fail this one.
        host.update(0.25);
        host.update(0.25);

        const hp::Quaternion after = scene.registry().get<hp::Transform>(spinner.raw()).rotation;
        const float halfAngle = 22.5F * 3.14159265358979F / 180.0F;
        CHECK(after.q.x == doctest::Approx(0.0F).epsilon(0.001));
        CHECK(after.q.y == doctest::Approx(std::sin(halfAngle)).epsilon(0.001));
        CHECK(after.q.z == doctest::Approx(0.0F).epsilon(0.001));
        CHECK(after.q.w == doctest::Approx(std::cos(halfAngle)).epsilon(0.001));

        // Still a rotation. The hook composes with itself every frame forever,
        // and a quaternion that drifts off the unit sphere scales the mesh
        // instead of turning it.
        const float length = std::sqrt(after.q.x * after.q.x + after.q.y * after.q.y +
                                       after.q.z * after.q.z + after.q.w * after.q.w);
        CHECK(length == doctest::Approx(1.0F).epsilon(0.0001));

        // The write went through `setLocalTransform`, so propagation sees it. A
        // raw write through `get<Transform>()` would leave this at zero and the
        // cube would sit still while every value in the inspector changed.
        CHECK(scene.propagateTransforms() > 0);
    }

    // The scene is gone, so nothing points into the module's image any more.
    // Clear the borrowed pointers before they dangle for the unload's own
    // entry-point call.
    host.setServices({});
    host.unloadAll();

    // Deregistered on unload: the type's function pointers and name literal
    // live in an image that has just been unmapped.
    CHECK_FALSE(static_cast<bool>(hp::resolveType("RockCubeSpin")));
}

TEST_CASE("a module lent no scene does nothing, quietly" * doctest::test_suite("module")) {
    // The ordinary state of a host that has not built a world yet — the runtime
    // today, and the editor for the whole of its startup. It must not be an
    // error and must not crash, because "no scene yet" is not a failure.
    hp::ModuleHost host;
    const hp::ModuleLoadResult result = host.load(rockcube_path());
    REQUIRE_MESSAGE(result.ok, result.message);

    for (int frame = 0; frame < 3; ++frame) {
        host.update(1.0 / 60.0);
    }
    CHECK(host.size() == 1);
}
