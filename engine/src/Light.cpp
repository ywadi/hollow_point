#include <hp/Light.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>

#include <algorithm>
#include <cmath>

namespace hp {
namespace {

const LogCategory kLog("render.light");

} // namespace

ResolvedPlacement resolvePlacement(const float4x4& m) {
    ResolvedPlacement placement;

    // Translation is the fourth row: the engine is row-major and multiplies left
    // to right (`hp/Math.hpp`), so a world matrix's position lives in m30..m32
    // rather than in a column.
    placement.position = float3{m.m30, m.m31, m.m32};

    // **Negative Z, per glTF's KHR_lights_punctual.** Row 2 is the local Z axis
    // in world space, and a light points the other way down it. Getting this
    // backwards lights the world from behind, which reads as the light simply
    // not working rather than as a sign error.
    const float3 forward{m.m20, m.m21, m.m22};
    const float lengthSq =
        forward.x * forward.x + forward.y * forward.y + forward.z * forward.z;
    if (lengthSq > 0.0F) {
        placement.direction = -(forward / std::sqrt(lengthSq));
    }
    return placement;
}

LightList gatherLights(const Scene& scene, std::size_t maxLights) {
    HP_PROFILE_ZONE();

    LightList lights;
    if (maxLights == 0) {
        return lights;
    }

    const auto& registry = scene.registry();
    auto view = registry.view<const WorldTransform, const Light>();
    lights.reserve(std::min<std::size_t>(view.size_hint(), maxLights));

    std::size_t skipped = 0;
    for (auto [entity, world, light] : view.each()) {
        if (!light.enabled) {
            continue;
        }
        if (lights.size() >= maxLights) {
            ++skipped;
            continue;
        }

        ResolvedLight resolved;
        resolved.entity = entity;
        resolved.light = light;

        // Shared with T0093's projectors rather than inlined here.
        const ResolvedPlacement placement = resolvePlacement(world.current);
        resolved.position = placement.position;
        resolved.direction = placement.direction;

        lights.push_back(resolved);
    }

    if (skipped > 0) {
        // Registry order is not a stable guarantee, so *which* lights were kept
        // is arbitrary and would change on an unrelated edit. Said out loud
        // rather than left to be noticed as flickering.
        HP_LOG_WARN(kLog,
                    "scene has more than {} lights; {} ignored this frame, and which ones "
                    "is arbitrary until per-object selection exists (T0079.3)",
                    maxLights, skipped);
    }
    return lights;
}

} // namespace hp
