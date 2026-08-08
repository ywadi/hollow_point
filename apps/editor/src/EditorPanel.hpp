// The editor's panel interface and the collection that owns them (T0032.4).
//
// **The whole point is that `EditorLayer` does not grow.** The editor is one
// layer on the engine's `LayerStack` (T0032.1), which is what keeps the engine
// ignorant that an editor exists — but "one layer" is an architectural
// statement, not a file-size one, and without something like this the layer
// becomes the dumping ground the ticket exists to prevent. A panel is
// registered, drawn, named and toggled; the layer knows nothing else about it.
//
// **Deliberately not a plugin system.** Panels are compiled into the editor and
// registered by name in one place. A registry keyed on a string, with discovery
// and load order, is the thing that gets built when panels come from outside the
// binary — and none does, nor will while the editor is a single executable.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hped {

class EditorContext;

/// One dockable window in the editor.
///
/// **`draw` is called inside an open ImGui frame and inside nothing else** — the
/// panel opens its own window. That is what lets a panel be docked, floated,
/// closed and reopened without the layer arranging anything: ImGui's dockspace
/// owns the layout, and the panel owns its contents.
class IEditorPanel {
public:
    virtual ~IEditorPanel() = default;

    IEditorPanel(const IEditorPanel&) = delete;
    IEditorPanel& operator=(const IEditorPanel&) = delete;

    /// @returns the window title, which is also its **identity** to ImGui.
    ///
    /// ImGui keys a docked window's saved position on this string, so changing
    /// it silently discards the user's layout for that panel. Treat it as a
    /// stable id that happens to be readable, not as a label.
    [[nodiscard]] virtual std::string_view title() const = 0;

    /// Draws the panel's contents. Called once per frame while it is open.
    /// @param context what the editor is currently editing.
    /// @returns nothing.
    virtual void draw(EditorContext& context) = 0;

    /// @returns whether this panel opens with the editor the first time it runs,
    ///          before there is a saved layout to say otherwise.
    [[nodiscard]] virtual bool openByDefault() const { return true; }

    /// @returns whether the panel is currently open.
    [[nodiscard]] bool open() const { return open_; }

    /// @returns a mutable handle on the open flag, for the menu checkbox and for
    ///          ImGui's own close button, which both write it directly.
    [[nodiscard]] bool* openFlag() { return &open_; }

    /// Opens or closes the panel.
    /// @param open the new state.
    /// @returns nothing.
    void setOpen(bool open) { open_ = open; }

protected:
    IEditorPanel() = default;

private:
    bool open_ = true;
};

/// The panels the editor owns, in registration order.
///
/// Order is registration order and nothing else — no priority, no sorting. It
/// decides only the order of the View menu, because ImGui's layout decides
/// everything else, and a second ordering concept would be a second thing to
/// disagree with the saved layout.
class PanelCollection {
public:
    /// Adds a panel and returns a borrowed pointer to it.
    /// @param panel the panel; ownership is taken.
    /// @returns the panel, valid for the collection's lifetime.
    template <typename Panel>
    Panel* add(std::unique_ptr<Panel> panel) {
        Panel* borrowed = panel.get();
        borrowed->setOpen(borrowed->openByDefault());
        panels_.push_back(std::move(panel));
        return borrowed;
    }

    /// @returns the panels, for drawing and for the View menu.
    [[nodiscard]] const std::vector<std::unique_ptr<IEditorPanel>>& panels() const {
        return panels_;
    }

    /// @returns the panels, mutably.
    [[nodiscard]] std::vector<std::unique_ptr<IEditorPanel>>& panels() { return panels_; }

private:
    std::vector<std::unique_ptr<IEditorPanel>> panels_;
};

} // namespace hped
