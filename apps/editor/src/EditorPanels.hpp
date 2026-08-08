// The panels this build registers (T0032.4, and T0033's first slice).
//
// **Two, deliberately.** T0032 is the shell — the owner's framing was *"we don't
// want to add any major functionality yet"* — so what is here is the minimum
// that proves the mechanism works: one panel that is the reason a dockspace
// exists at all, and one that is trivial, so that "the collection draws more
// than one thing" is demonstrated rather than assumed. The hierarchy and
// inspector are T0035; the material inspector is T0174.
#pragma once

#include "EditorPanel.hpp"

namespace hped {

/// The scene, as a panel rather than as the whole window (T0033).
///
/// **A dockspace with nothing docked in it is not a dockspace**, which is why
/// this much of T0033 lands with T0032 rather than after it. It is genuinely
/// T0033's substance and is ticked there.
///
/// Three things follow from the viewport being a panel, and each one was a
/// latent bug while it was the window:
///
/// - the scene must be **rendered at the panel's size**, not the swap chain's,
///   or it is stretched by whatever fraction of the window the panel occupies;
/// - the camera must take input **only when this panel has it**, or dragging
///   inside an inspector flies the view — the gap T0172 recorded and this
///   closes;
/// - the image is drawn by ImGui, so the engine's dev present blit has to stop,
///   or the scene appears twice with the UI over one of them.
class ViewportPanel final : public IEditorPanel {
public:
    /// @returns the window title, which is also its ImGui identity.
    [[nodiscard]] std::string_view title() const override { return "Viewport"; }

    /// Draws last frame's scene texture and reports the panel's size back.
    /// @param context what the editor is editing.
    /// @returns nothing.
    void draw(EditorContext& context) override;
};

/// Frame and scene numbers.
///
/// Exists to be a second panel — see the file comment — and because "is anything
/// actually in the scene" is the question an empty viewport raises first, and
/// answering it from a log line is worse than answering it on screen.
class StatsPanel final : public IEditorPanel {
public:
    /// @returns the window title, which is also its ImGui identity.
    [[nodiscard]] std::string_view title() const override { return "Statistics"; }

    /// Draws the frame counter, the camera pose and the scene size.
    /// @param context what the editor is editing.
    /// @returns nothing.
    void draw(EditorContext& context) override;
};

} // namespace hped
