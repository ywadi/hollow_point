#include <hp/DrawSubmission.hpp>

#include <hp/Profiling.hpp>

namespace hp {

DrawList parseScene(const Scene& scene, LayerMask cullingMask, DrawParseStats* stats) {
    HP_PROFILE_ZONE();

    DrawParseStats counted;
    DrawList list;

    const auto& registry = scene.registry();
    auto view = registry.view<const WorldTransform, const MeshRenderer>();

    // Reserving on the view's size rather than growing: the common case is that
    // most drawable entities have a mesh, so this is one allocation instead of
    // log(n) reallocations, and the overshoot when many lack a mesh is bounded
    // by the number of entities that already carry both components.
    list.reserve(view.size_hint());

    for (auto [entity, world, renderer] : view.each()) {
        ++counted.considered;

        // **One AND, and it is first** (T0085). An object the camera does not
        // render costs nothing else -- no GUID check, no list entry, and above
        // all no draw call. Filtering here rather than in a shader is the whole
        // point: a per-pixel mask test wastes the entire cost of drawing the
        // object it then discards.
        if (!renderer.layers.intersects(cullingMask)) {
            ++counted.culledByLayer;
            continue;
        }

        if (!renderer.mesh.isValid()) {
            // A legitimate state, not an error -- see `DrawParseStats`.
            ++counted.withoutMesh;
            continue;
        }

        list.push_back(DrawItem{entity, world.current, renderer.mesh, renderer.material});
        ++counted.drawn;
    }

    if (stats != nullptr) {
        *stats = counted;
    }
    return list;
}

} // namespace hp
