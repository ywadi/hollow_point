// The editor, as one layer on the engine's stack (T0032.1).
//
// **One layer is the architectural claim, and it is the whole ticket.** The
// engine's `LayerStack` (T0017) takes an `ILayer`; the editor is one of them,
// and that is the entire surface the engine sees. `grep -ri editor engine/`
// returns nothing (T0032.6) and this file is why: everything the editor does —
// the dockspace, the panels, the menu — happens inside `onRender`, above an
// engine that has no idea any of it exists.
//
// **What the engine does provide is a UI context, and that is not the same
// thing.** `RenderConfig::ui` creates a Dear ImGui context and draws it over the
// presented frame. That is generic: ImGui is already linked into every binary
// here (**D6**), a shipped game may want a debug panel, and nothing about it
// names an editor. The reason it lives engine-side rather than here is a link
// constraint written up on `hp::UiBinding` — SDL3 is static and private in the
// engine, so the vendored SDL3 backend has to run there.
//
// So this layer *binds* to that context rather than creating one. Two copies of
// ImGui exist in the process and share one context, which is Dear ImGui's own
// documented arrangement for exactly this.
#pragma once

#include "EditorCamera.hpp"
#include "EditorPanel.hpp"

#include <hp/Layer.hpp>
#include <hp/Math.hpp>

#include <cstdint>
#include <string>

namespace Diligent {
struct ITextureView;
} // namespace Diligent

namespace hp {
class Application;
class AssetPool;
class RenderLayer;
class Scene;
} // namespace hp

namespace hped {

/// What the editor is currently editing, handed to every panel.
///
/// **A view, not a store.** Nothing here is owned by the editor layer; it is a
/// bundle of references assembled per frame so a panel does not have to reach
/// back through the layer to find the scene. The alternative — panels holding a
/// pointer to the layer — makes every panel a friend of the layer and is how the
/// dumping ground reappears one indirection later.
struct EditorContext {
    /// The scene being edited.
    hp::Scene* scene = nullptr;

    /// Where the scene's GUIDs resolve.
    hp::AssetPool* assets = nullptr;

    /// The viewport camera, so a panel can report or reset it.
    EditorCamera* camera = nullptr;

    /// Last frame's rendered scene, for the viewport panel to draw.
    ///
    /// **Valid for this frame only.** The renderer recreates it on a resize, so
    /// a panel that keeps the pointer has a use-after-free that appears only
    /// when somebody drags a window edge — the hazard `FrameRenderedEvent`
    /// already warns about, inherited here because this is the same pointer.
    Diligent::ITextureView* sceneColour = nullptr;

    /// The scene target's width in pixels.
    int sceneWidth = 0;

    /// The scene target's height in pixels.
    int sceneHeight = 0;

    /// Set by the viewport panel: the size it wants the scene rendered at, in
    /// pixels. Zero means "unchanged".
    int requestedWidth = 0;

    /// See `requestedWidth`.
    int requestedHeight = 0;

    /// Set by the viewport panel: whether the camera should be taking input.
    ///
    /// **This is the answer to T0172's recorded gap.** The camera read the whole
    /// window because the viewport *was* the whole window; now that it is a
    /// panel, dragging inside an inspector must not fly the view.
    bool viewportFocused = false;

    /// Frames rendered so far, for a stats panel.
    std::uint64_t frame = 0;

    /// Seconds the last frame took.
    double deltaSeconds = 0.0;
};

/// The editor's single layer: a dockspace, a menu, and a collection of panels.
class EditorLayer final : public hp::ILayer {
public:
    /// @param app the application, for the frame counter.
    /// @param render the render layer that owns the UI context. Must outlive
    ///        this layer, which the application guarantees by owning both.
    EditorLayer(hp::Application& app, hp::RenderLayer& render);

    ~EditorLayer() override;

    /// Binds this binary's ImGui to the engine's context, and registers panels.
    void onAttach() override;

    /// Draws the dockspace, the menu bar and every open panel.
    void onRender() override;

    /// Records the frame delta, which a stats panel reports.
    /// @param deltaSeconds the scaled frame delta.
    void onUpdate(double deltaSeconds) override;

    /// Takes the rendered scene out of `FrameRenderedEvent` for the viewport
    /// panel, and never consumes anything.
    /// @param event the event.
    void onEvent(hp::Event& event) override;

    /// @returns the panel collection, so startup can register more.
    [[nodiscard]] PanelCollection& panels() { return panels_; }

    /// @returns the context the panels last saw, so the owning layer can read
    ///          back what the viewport asked for.
    [[nodiscard]] const EditorContext& context() const { return context_; }

    /// Points the editor at what it is editing.
    /// @param scene the scene being edited.
    /// @param assets where its GUIDs resolve.
    /// @param camera the viewport camera.
    /// @returns nothing.
    void setSubject(hp::Scene* scene, hp::AssetPool* assets, EditorCamera* camera);

private:
    void drawDockspace();
    void drawMenuBar();

    hp::Application& app_;
    hp::RenderLayer& render_;
    PanelCollection panels_;
    EditorContext context_;
    bool bound_ = false;
    bool layoutPathSet_ = false;
};

/// @returns the host path the editor's ImGui layout is written to (T0032.5),
///          with its directory created. Empty when no location could be found,
///          in which case the layout simply is not persisted.
///
/// **Per user, not per project**, which is the choice T0032 predicted and the
/// reason is churn: a dock layout changes every time somebody drags a splitter,
/// and a per-project file would put that in everyone's diff. A layout is a
/// property of the person, not of the work. When T0024's project exists, a
/// project-specific override is additive.
[[nodiscard]] std::string editorLayoutPath();

} // namespace hped
