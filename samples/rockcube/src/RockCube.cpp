// The rock cube sample (T0157).
//
// **A sample, not a game.** HollowPoint powers several games and a game is a
// separate project (T0109); what lives here is a gameplay module the engine's
// own repository builds, so that the authoring path is exercised by this
// project's CI rather than by whichever game happens to be open.
//
// It is deliberately the smallest thing that is still *whole*: one cube, one
// light, one camera, one material, and a rotation. Every part of it crosses a
// boundary that had never been crossed before:
//
//   * the scene is a **hand-authored `.hpscene`** loaded through
//     `loadSceneFromString`, which until now had no caller outside `tests/`;
//   * the material is a **`.hpmat` asset file**, not a `hp::Material` built in
//     code;
//   * every byte of content is read **through the VFS** (D13) from a mounted
//     content root — no `std::filesystem`, no absolute paths;
//   * the rotation runs in a **gameplay module** across the real boundary, with
//     D12's lockstep and T0104's build id doing their jobs.
//
// ## What this module does not do, and must not start doing
//
// **It holds no state.** Everything persistent lives in the ECS, owned by the
// engine, because statics in this image are destroyed on unload — so anything
// kept here vanishes on a hot reload. The spin rate lives in a component; this
// file has one `const LogCategory` and nothing else.
//
// **It does not own the scene.** `ModuleContext::services` lends it one. A null
// scene is an ordinary state (a host that has not built one), not an error.

#include <hp/Api.hpp>
#include <hp/Assets.hpp>
#include <hp/Log.hpp>
#include <hp/Material.hpp>
#include <hp/Math.hpp>
#include <hp/ModuleHost.hpp>
#include <hp/Paths.hpp>
#include <hp/Reflect.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneSerialize.hpp>
#include <hp/ShaderCook.hpp>
#include <hp/Vfs.hpp>

#include <cmath>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#define HP_PATH_SEP "\\"
#else
#define HP_PATH_SEP "/"
#endif

namespace {

const hp::LogCategory kLog("rockcube");

/// Spins an entity about an axis, for as long as it exists.
///
/// **A component, so it lives in the scene file** — `RockCubeSpin` appears in
/// `rockcube.hpscene` beside `Transform` and `MeshRenderer`, with no separate
/// path for a gameplay type (D23). The engine has never heard of this struct;
/// what it knows is the name, which is the identity everywhere (T0095).
///
/// It carries the rate rather than the accumulated angle, and the rotation is
/// applied **incrementally** to whatever the transform already holds. That
/// keeps an authored rest rotation meaningful — a cube tilted in the file
/// spins about the world axis from where it was placed, rather than snapping to
/// an angle this component invented. The renormalisation each frame is what
/// makes that safe to do forever.
struct RockCubeSpin {
    /// World-space axis to spin about. Normalised on use; a zero axis is
    /// ignored rather than producing a NaN quaternion.
    hp::float3 axis{0.0F, 1.0F, 0.0F};

    /// Degrees per second. Signed — negative spins the other way.
    float degreesPerSecond = 24.0F;
};

/// Every file the sample reads, relative to its content root.
constexpr const char* kScenePath = "scenes/rockcube.hpscene";
constexpr const char* kAssets[] = {
    // Textures first is not required — a material holds GUIDs and they resolve
    // at draw time — but it keeps the log readable when one is missing.
    "textures/rock_basecolour.png",
    "textures/rock_orm.png",
    "textures/rock_height.png",
    "textures/rock_normal.png",
    // The shader before the material that names it, so a failure reads in
    // the order a person would look for it.
    "shaders/rock_pom.slang",
    "materials/rock.hpmat",
    "models/cube.gltf",
};

/// Finds the staged content root.
///
/// The layouts this module can find itself in, in order: beside the host
/// executable (a co-located export), one directory up, and the build tree,
/// where CMake stages `content/` next to the module itself.
///
/// Composed at run time rather than baked by CMake, for the reason the two apps
/// already record: a configure-time path is a *host* path, and a Windows binary
/// cannot open one.
std::string findContentRoot() {
    const std::string dir = hp::executableDirectory();
    const std::string candidates[] = {
        dir + HP_PATH_SEP "content",
        dir + HP_PATH_SEP ".." HP_PATH_SEP "content",
        dir + HP_PATH_SEP ".." HP_PATH_SEP ".." HP_PATH_SEP "samples" HP_PATH_SEP
              "rockcube" HP_PATH_SEP "content",
    };
    for (const std::string& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(candidate) / "scenes", ec)) {
            return candidate;
        }
    }
    return {};
}

/// Mounts the content root, imports every asset and loads the scene.
///
/// @param ctx what the host lent this module.
/// @returns whether the scene is on screen. False is reported and survivable:
///          the host keeps running and shows whatever else it has.
bool buildScene(hp::ModuleContext& ctx) {
    hp::Scene* scene = ctx.services.scene;
    hp::AssetPool* assets = ctx.services.assets;
    if (scene == nullptr || assets == nullptr || ctx.services.device == nullptr) {
        HP_LOG_INFO(kLog,
                    "host lent no scene, asset pool or device; the sample has nothing to build "
                    "into and is doing nothing");
        return false;
    }

    const std::string root = findContentRoot();
    if (root.empty()) {
        HP_LOG_ERROR(kLog, "could not find the sample's content root beside {}",
                     hp::executableDirectory());
        return false;
    }

    // `init` is safe to call twice and the second call is a no-op, so a module
    // does not have to know whether its host got there first.
    if (!hp::Vfs::init(nullptr) || !hp::Vfs::mount(root)) {
        HP_LOG_ERROR(kLog, "could not mount '{}'", root);
        return false;
    }
    HP_LOG_INFO(kLog, "mounted {}", root);

    // **Whoever mounts content is who loads its cooked shaders** (T0142.7).
    // There is no mount notification to hang it on. This sample authors no
    // shader of its own, so in a build with a compiler it finds nothing and
    // carries on; in a shipped runtime with no compiler it is what makes the
    // surface pipeline exist at all.
    (void)hp::loadCookedShaders();

    for (const char* path : kAssets) {
        const hp::ImportResult result =
            hp::importAsset(ctx.services.device, ctx.services.deviceContext, *assets, path);
        if (!result.loaded) {
            // Not fatal, and deliberately so: a missing texture renders the
            // magenta placeholder and a missing material renders the
            // checkerboard, both of which are more useful than refusing to
            // start. `importAsset` has already said which file and why.
            HP_LOG_WARN(kLog, "'{}' did not import; the sample will render its failure", path);
        }
    }

    const auto text = hp::Vfs::readText(kScenePath);
    if (!text) {
        HP_LOG_ERROR(kLog, "'{}' is not in the mount tree", kScenePath);
        return false;
    }

    // **The first caller of this outside `tests/`.** Everything above exists so
    // that this line can be the whole of "load the sample".
    const hp::SceneLoadResult loaded = hp::loadSceneFromString(*scene, *text, kScenePath);
    if (loaded.status != hp::SceneLoadStatus::Ok) {
        HP_LOG_ERROR(kLog, "'{}' did not load", kScenePath);
        return false;
    }
    if (loaded.unknownComponents > 0) {
        // Would mean `RockCubeSpin` was not registered before the load — the
        // cube would render and sit perfectly still, which is a failure that
        // looks like a missing feature rather than a bug.
        HP_LOG_WARN(kLog, "{} component(s) in '{}' had no registered type; the cube will not spin",
                    loaded.unknownComponents, kScenePath);
    }

    HP_LOG_INFO(kLog, "rock cube scene ready: {} entities from '{}'", loaded.entities, kScenePath);
    return true;
}

void onLoad(hp::ModuleContext& ctx) {
    // Without this the engine and this module see different meta contexts, and
    // every reflected lookup silently finds nothing (T0053).
    hp::adoptMetaContext();

    // `registerComponent` rather than `reflect`: it is what puts the type in the
    // component registry, which is what the scene loader walks. `reflect` alone
    // would make the type inspectable and leave the scene file's `RockCubeSpin`
    // subtree preserved-but-inert.
    hp::registerComponent<RockCubeSpin>("RockCubeSpin")
        .property<&RockCubeSpin::axis>("axis")
        .property<&RockCubeSpin::degreesPerSecond>("degreesPerSecond")
        .meta({.min = -720.0, .max = 720.0, .tooltip = "degrees per second"});

    // **After registration, never before.** The loader materialises a component
    // whose type exists and preserves one whose type does not, so the order of
    // these two statements is the difference between a spinning cube and a
    // still one.
    const bool ready = buildScene(ctx);
    HP_LOG_INFO(kLog, "rock cube module loaded, generation {}{}", ctx.generation,
                ready ? "" : " (with nothing on screen)");
}

void onUnload(hp::ModuleContext& ctx) {
    // Not tidiness. `entt::meta` holds function pointers and unowned name
    // literals that live in this image, so leaving the type registered after
    // the image is unmapped is a dangling call waiting to happen (T0053).
    hp::forgetType("RockCubeSpin");
    HP_LOG_INFO(kLog, "rock cube module unloading, generation {}", ctx.generation);
}

void onUpdate(hp::ModuleContext& ctx, double deltaSeconds) {
    hp::Scene* scene = ctx.services.scene;
    if (scene == nullptr) {
        return;
    }

    constexpr float kDegreesToRadians = 3.14159265358979F / 180.0F;
    auto view = scene->registry().view<RockCubeSpin, hp::Transform>();
    for (const entt::entity entity : view) {
        const RockCubeSpin& spin = view.get<RockCubeSpin>(entity);
        // Found by ADL on `hp::float3`, which is `Diligent::float3` — the
        // engine re-exports the type, not the free functions over it.
        if (dot(spin.axis, spin.axis) <= 0.0F) {
            continue;
        }
        const float radians =
            spin.degreesPerSecond * kDegreesToRadians * static_cast<float>(deltaSeconds);

        hp::Transform next = view.get<hp::Transform>(entity);
        // Pre-multiplied, so the axis is a **world** axis: `a * b` applies `b`
        // first. Post-multiplying would spin about the cube's own axis, which
        // for a cube already turning looks similar and is not the same thing.
        //
        // Renormalised because this composes with itself sixty times a second
        // forever, and a quaternion that drifts off the unit sphere scales the
        // mesh rather than rotating it.
        next.rotation = normalize(
            hp::Quaternion::RotationFromAxisAngle(spin.axis, radians) * next.rotation);

        // **`setLocalTransform`, not a write through `get<Transform>()`.** A raw
        // write is invisible to propagation until something marks the entity
        // dirty, and the symptom is a cube that does not move while every value
        // in the inspector changes.
        scene->setLocalTransform(hp::Entity(*scene, entity), next);
    }

    // Propagation is the *frame's* job, at phases 7 and 9, and `Application`
    // does it for the scene it was handed (T0062.11). A module calling
    // `propagateTransforms` here would do it a second time and, worse, would
    // decide *when* — which D17 places outside the scene deliberately.
}

} // namespace

HP_GAMEPLAY_MODULE("rockcube", onLoad, onUnload, onUpdate)
