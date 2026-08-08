#include "EditorLayer.hpp"

#include "EditorPanels.hpp"

#include <hp/Application.hpp>
#include <hp/Event.hpp>
#include <hp/Log.hpp>
#include <hp/Render.hpp>

#include <imgui.h>

#include <cstdlib>
#include <filesystem>

namespace hped {
namespace {

const hp::LogCategory kLog("editor.ui");

/// The dockspace's identity. ImGui keys the saved layout on it, so it is a
/// stable id and not a label — changing it discards everyone's layout.
constexpr const char* kDockspaceId = "HollowPointDockspace";

} // namespace

std::string editorLayoutPath() {
    std::error_code ec;
    std::filesystem::path base;

#if defined(_WIN32)
    if (const char* appData = std::getenv("APPDATA"); appData != nullptr) {
        base = std::filesystem::path(appData);
    }
#else
    // XDG first, then the documented fallback. Not `HOME` alone: a user who has
    // set XDG_CONFIG_HOME has said where configuration goes, and ignoring it is
    // how a tool ends up scattering dotfiles in a home directory somebody
    // deliberately keeps clean.
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
        base = std::filesystem::path(xdg);
    } else if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        base = std::filesystem::path(home) / ".config";
    }
#endif

    if (base.empty()) {
        return {};
    }
    const std::filesystem::path directory = base / "hollowpoint";
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return {};
    }
    return (directory / "editor-layout.ini").string();
}

EditorLayer::EditorLayer(hp::Application& app, hp::RenderLayer& render)
    : hp::ILayer("editor"), app_(app), render_(render) {}

EditorLayer::~EditorLayer() = default;

void EditorLayer::setSubject(hp::Scene* scene, hp::AssetPool* assets, EditorCamera* camera) {
    context_.scene = scene;
    context_.assets = assets;
    context_.camera = camera;
}

void EditorLayer::onAttach() {
    if (!render_.uiReady()) {
        HP_LOG_WARN(kLog, "no ui context; the editor will render no panels");
        return;
    }

    // **Bind this binary's ImGui to the engine's context** (T0032.2).
    //
    // Both modules link Dear ImGui, so both have their own `GImGui` and their
    // own allocator globals. Pointing this copy at the engine's context and its
    // allocators is Dear ImGui's documented arrangement for exactly this, and
    // it is sound because both copies are compiled from the same source with the
    // same `IMGUI_USER_CONFIG` — so there is no structure they can disagree
    // about. Get it wrong and the symptom is not a link error: it is a second,
    // empty context in which every panel draws to nothing.
    const hp::UiBinding binding = render_.uiBinding();
    if (!binding.valid()) {
        HP_LOG_ERROR(kLog, "the ui context could not be bound; no panels will draw");
        return;
    }
    ImGui::SetAllocatorFunctions(binding.alloc, binding.release, binding.userData);
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(binding.context));
    bound_ = true;

    // **After binding, never before**: `IMGUI_CHECKVERSION` compares this
    // copy's compiled-in version and struct sizes against the running context's.
    // It is the check that catches the failure this whole arrangement risks —
    // two ImGui builds that disagree about `ImGuiIO`'s layout — and it catches
    // it here rather than as corruption somewhere in a draw list.
    IMGUI_CHECKVERSION();

    const std::string layout = editorLayoutPath();
    if (layout.empty()) {
        HP_LOG_WARN(kLog, "no writable config directory; the dock layout will not persist");
    } else {
        render_.setUiLayoutPath(layout);
        layoutPathSet_ = true;
        HP_LOG_INFO(kLog, "dock layout at {}", layout);
    }

    // The panels this build knows about. Registration order is menu order and
    // nothing else — the saved layout decides everything visual.
    panels_.add(std::make_unique<ViewportPanel>());
    panels_.add(std::make_unique<StatsPanel>());

    HP_LOG_INFO(kLog, "editor layer up: {} panel(s), docking {}", panels_.panels().size(),
                (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) != 0 ? "on" : "off");
}

void EditorLayer::onUpdate(double deltaSeconds) {
    context_.deltaSeconds = deltaSeconds;
    context_.frame = app_.frame();
}

void EditorLayer::onEvent(hp::Event& event) {
    // The scene texture, taken for this frame only. Deliberately not consumed:
    // the event is a broadcast and the dev present path still reads it.
    hp::dispatchEvent<hp::FrameRenderedEvent>(event, [this](const hp::FrameRenderedEvent& frame) {
        context_.sceneColour = frame.colour();
        context_.sceneWidth = frame.width();
        context_.sceneHeight = frame.height();
        return false;
    });
}

void EditorLayer::drawDockspace() {
    // **A borderless, immovable host window under everything.** The dockspace
    // needs a window to live in, and that window must not itself be dockable,
    // draggable or decorated — otherwise the thing panels dock *into* is one of
    // the things being docked. `NoDocking` plus the viewport-sized geometry is
    // what makes it read as "the application background" rather than as a panel.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar
                             | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
                             | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::Begin("##hp_dockspace_host", nullptr, flags);
    ImGui::PopStyleVar(3);

    // `PassthruCentralNode` leaves the middle of the dockspace transparent when
    // nothing is docked there, so the frame behind shows through instead of a
    // flat grey slab. It costs nothing and it is the difference between an empty
    // editor looking empty and looking broken.
    ImGui::DockSpace(ImGui::GetID(kDockspaceId), ImVec2(0.0F, 0.0F),
                     ImGuiDockNodeFlags_PassthruCentralNode);

    drawMenuBar();
    ImGui::End();
}

void EditorLayer::drawMenuBar() {
    if (!ImGui::BeginMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("View")) {
        for (const std::unique_ptr<IEditorPanel>& panel : panels_.panels()) {
            const std::string title(panel->title());
            ImGui::MenuItem(title.c_str(), nullptr, panel->openFlag());
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Layout")) {
        // No "save": ImGui writes the ini itself on a timer and at shutdown.
        // Offering a save button as well would imply the rest of the time it is
        // not saving, which is the opposite of true.
        if (ImGui::MenuItem("Reset docking", nullptr, false, layoutPathSet_)) {
            HP_LOG_INFO(kLog, "delete {} and restart to reset the layout", editorLayoutPath());
        }
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void EditorLayer::onRender() {
    if (!bound_ || !render_.uiReady()) {
        return;
    }
    // The engine opened the frame at late update, before any layer rendered.
    // Nothing here begins or ends one.
    drawDockspace();

    // Cleared before the panels run so that a panel is the only thing that can
    // set them, and a panel that stopped asking stops being asked for.
    context_.requestedWidth = 0;
    context_.requestedHeight = 0;
    context_.viewportFocused = false;

    for (const std::unique_ptr<IEditorPanel>& panel : panels_.panels()) {
        if (panel->open()) {
            panel->draw(context_);
        }
    }

    // The texture is valid for the frame that published it and no longer. Drop
    // it here rather than next frame, so a frame in which the scene published
    // nothing cannot draw the previous one's target after a resize freed it.
    context_.sceneColour = nullptr;
}

} // namespace hped
