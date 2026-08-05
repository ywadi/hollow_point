// Turning a draw list into draw calls (T0028).
//
// **Nothing about DiligentFX appears here.** The renderer is held behind an
// `Impl`, and `engine/CMakeLists.txt` links DiligentFX PRIVATE, so the choice
// made on T0134 is confined to one translation unit and replacing it touches
// that file. That is the modularity, and it is enforced by the linker rather
// than by an interface -- **D22** rules out an abstraction with one
// implementation until there is a concrete second target.
//
// Why this drives `PBR_Renderer` and not `GLTF_PBR_Renderer`, which exists for
// exactly this job: `GLTF_PBR_Renderer` builds its pipeline state in its own
// constructor and leaves the depth comparison at Diligent's default of
// `COMPARISON_FUNC_LESS`, with the PSO cache private and no depth field in its
// `CreateInfo`. Under the engine's reverse-Z convention (T0130) that draws
// **nothing at all** -- depth clears to 0, the near plane is 1, and `LESS` lets
// through only fragments below 0. The base class takes the caller's pipeline
// desc, so it can be told `COMPARISON_FUNC_GREATER_EQUAL`. T0134 carries the
// full argument.
#pragma once

#include <hp/Api.hpp>
#include <hp/CameraSystem.hpp>
#include <hp/DrawSubmission.hpp>
#include <hp/FrameTargets.hpp>

#include <cstddef>
#include <memory>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
} // namespace Diligent

namespace hp {

class AssetPool;

/// What one submit pass did, for logging and for tests.
struct DrawSubmitStats {
    /// Items whose mesh resolved and were drawn.
    std::size_t submitted = 0;

    /// Items whose mesh GUID is not in the pool.
    ///
    /// **These draw nothing and get no placeholder**, deliberately. A missing
    /// *texture* has an obvious stand-in; a missing *mesh* does not, and
    /// inventing a cube puts geometry in the world that no artist authored --
    /// which is worse than an empty space and a loud error. A visible marker for
    /// this belongs in T0061's debug draw, not here. Counted so the absence is
    /// reportable rather than silent.
    std::size_t missingMesh = 0;
};

/// Draws a `DrawList` through a resolved camera.
///
/// Owns the GPU-side renderer, its pipeline states and its per-model resource
/// bindings, so it is created once against a device and reused. Not tied to a
/// scene, a camera or a target: everything that varies per frame is a parameter,
/// which is what keeps this callable for portals, reflection probes and
/// thumbnails rather than only for "the" viewport (T0120.2).
class HP_API SceneRenderer {
public:
    /// Constructs an empty renderer that holds no device resources.
    SceneRenderer();

    /// Releases the renderer and everything it created.
    ~SceneRenderer();

    /// Not copyable: it owns GPU objects, and two owners would double-release.
    SceneRenderer(const SceneRenderer&) = delete;

    /// Not copyable; see the copy constructor.
    /// @returns nothing -- deleted.
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    /// Moves the renderer.
    /// @param other the renderer to move from.
    SceneRenderer(SceneRenderer&& other) noexcept;

    /// Moves the renderer, releasing whatever this one held.
    /// @param other the renderer to move from.
    /// @returns this renderer.
    SceneRenderer& operator=(SceneRenderer&& other) noexcept;

    /// Creates the pipeline states and shaders.
    ///
    /// The target formats are baked into the pipeline state, so a renderer
    /// created for one pair cannot draw into another -- recreate it if the
    /// targets change format. Size may change freely; only format is baked.
    /// @param device the device to create against. Must not be null.
    /// @param context the immediate context, needed for one-off setup uploads.
    ///        Must not be null.
    /// @param colour the colour target's format.
    /// @param depth the depth target's format.
    /// @returns whether creation succeeded. False leaves this renderer empty and
    ///          logs why; it is never a partial state.
    bool create(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                TargetFormat colour, TargetFormat depth);

    /// Releases every device resource. Safe to call more than once.
    /// @returns nothing.
    void release();

    /// @returns whether `create` succeeded and this renderer can draw.
    [[nodiscard]] bool valid() const;

    /// Draws a list through a view.
    ///
    /// Targets must already be bound by the caller -- this issues draws and does
    /// not set render targets, because the layer that owns the target is the one
    /// that knows what else shares it (T0027).
    /// @param context the immediate context to record into. Must not be null.
    /// @param list what to draw, from `parseScene`.
    /// @param view the resolved camera, from `buildView`.
    /// @param pool where mesh GUIDs are resolved. Items whose mesh is absent are
    ///        skipped and counted.
    /// @param stats optional; filled with what the pass did. Null to ignore.
    /// @returns how many items were drawn.
    std::size_t render(Diligent::IDeviceContext* context, const DrawList& list,
                       const ResolvedView& view, const AssetPool& pool,
                       DrawSubmitStats* stats = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hp
