#include <hp/Light.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>

#include <algorithm>
#include <limits>
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

namespace hp {

void selectLightsFor(const LightList& lights, const float3& objectPosition,
                     LayerMask objectLayers, std::size_t maxLights, LightList& out) {
    HP_PROFILE_ZONE();

    out.clear();
    maxLights = std::min(maxLights, kMaxLights);
    if (maxLights == 0) {
        return;
    }

    // Distance is squared throughout: the ordering is identical and it avoids a
    // square root per light per object, in a loop that runs for every drawable
    // thing in the scene.
    struct Candidate {
        const ResolvedLight* light;
        float distanceSq;

        // **The tiebreak, and it exists because the absence of one was a bug**
        // (T0145, from T0158/T0159's finding). Every directional light carries
        // distance 0, `std::sort` and `std::partial_sort` are *unstable*, so
        // which of two suns landed at index 0 was unspecified -- and measured,
        // on this machine it was the dim fill. A shader that spent its one
        // expensive march on `Lights[0]` therefore spent it on the wrong light,
        // silently, and the rock cube sample had to rank by intensity itself to
        // work around it.
        //
        // Rec. 709 luminance of colour times intensity: the same weighting the
        // sample used, promoted to the engine so every consumer agrees. The
        // order it produces is now a **contract** -- `HpLight::Index == 0` is
        // the dominant light -- documented in `HpMaterial.slang` and honoured
        // by the lighting stage's per-light method.
        float luminance;

        // Last resort, so the order is *total* rather than merely
        // better-defined: two identical lamps still sort the same way on every
        // run, which is what keeps a frame reproducible and a byte-identical
        // guard meaningful.
        std::uint32_t entity;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(lights.size());

    // Nearest, then brightest, then fixed. A strict weak ordering, shared by
    // both sort paths below so the partial and the full sort cannot disagree
    // about what "first" means.
    const auto dominates = [](const Candidate& a, const Candidate& b) {
        if (a.distanceSq != b.distanceSq) {
            return a.distanceSq < b.distanceSq;
        }
        if (a.luminance != b.luminance) {
            return a.luminance > b.luminance;
        }
        return a.entity < b.entity;
    };

    for (const ResolvedLight& light : lights) {
        // **The layer test first** (T0085.4), because it is one AND and rejects
        // outright, where the distance below is arithmetic on a light that may
        // then be discarded anyway.
        if (!light.light.layers.intersects(objectLayers)) {
            continue;
        }

        // A directional light has no position to be far from -- it is the sun.
        // Ranking it by distance would let a nearby lamp displace it, which is
        // the one selection mistake a player notices immediately: the world
        // stops being lit when they walk past a candle.
        float distanceSq = 0.0F;
        if (light.light.type != LightType::Directional) {
            const float dx = light.position.x - objectPosition.x;
            const float dy = light.position.y - objectPosition.y;
            const float dz = light.position.z - objectPosition.z;
            distanceSq = dx * dx + dy * dy + dz * dz;

            // Past its own range a light contributes nothing at all, so it must
            // not occupy a slot a reaching light could use. Without this a
            // distant lamp still crowds out a nearby one and the object goes
            // dark for no visible reason.
            const float range = light.light.range;
            if (range > 0.0F && distanceSq > range * range) {
                continue;
            }
        }
        // Rec. 709, the same weighting a shader would use, so "brightest" means
        // the same thing on both sides of the buffer.
        const float3& colour = light.light.colour;
        const float luminance =
            (colour.x * 0.2126F + colour.y * 0.7152F + colour.z * 0.0722F) *
            light.light.intensity;
        candidates.push_back(Candidate{&light, distanceSq, luminance,
                                       static_cast<std::uint32_t>(
                                           entt::to_integral(light.entity))});
    }

    if (candidates.size() > maxLights) {
        // Partial sort: only the first `maxLights` need to be in order, and the
        // rest are discarded. A full sort would order a tail nothing reads.
        std::partial_sort(candidates.begin(),
                          candidates.begin() + static_cast<std::ptrdiff_t>(maxLights),
                          candidates.end(), dominates);
        candidates.resize(maxLights);
    } else {
        std::sort(candidates.begin(), candidates.end(), dominates);
    }

    out.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        out.push_back(*candidate.light);
    }
}

} // namespace hp
