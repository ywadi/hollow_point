#include "EditorPanels.hpp"

#include "EditorLayer.hpp"

#include <hp/Scene.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace hped {

void ViewportPanel::draw(EditorContext& context) {
    // No padding: the image is the panel, and a border of window background
    // around a rendered frame reads as a letterbox nobody asked for.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    const bool visible = ImGui::Begin(std::string(title()).c_str(), openFlag());
    ImGui::PopStyleVar();

    if (!visible) {
        ImGui::End();
        return;
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const int wantWidth = static_cast<int>(std::max(available.x, 1.0F));
    const int wantHeight = static_cast<int>(std::max(available.y, 1.0F));

    // **Ask for the panel's size, do not scale the image to it.** Drawing a
    // 1280x720 target into a 400x300 panel is a resample: the picture is soft,
    // the aspect is wrong unless the panel happens to match, and every pixel
    // measurement taken off it — a future click-to-pick, a gizmo hit test — is
    // taken off the wrong grid. Rendering at the panel's size costs a target
    // reallocation when it changes and nothing at all when it does not.
    context.requestedWidth = wantWidth;
    context.requestedHeight = wantHeight;

    // **Hovered, not focused.** A viewport that demands a click before it will
    // turn is the thing every 3D tool is criticised for; hover is what an
    // artist expects. `ImGui::IsWindowHovered` is also what correctly returns
    // false when a floating panel is on top of this one, which a rectangle test
    // against the panel bounds would get wrong.
    context.viewportFocused = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)
                              || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

    if (context.sceneColour != nullptr && context.sceneWidth > 0 && context.sceneHeight > 0) {
        // The texture view is Diligent's, reinterpreted — which is exactly how
        // `ImGuiDiligentRenderer` reads it back out at draw time
        // (`reinterpret_cast<ITextureView*>(pCmd->GetTexID())`). Any other
        // encoding would be a second convention for the same pointer.
        ImGui::Image(reinterpret_cast<ImTextureID>(context.sceneColour),
                     ImVec2(static_cast<float>(context.sceneWidth),
                            static_cast<float>(context.sceneHeight)));
    } else {
        // Said plainly rather than left blank: a black panel and a broken panel
        // look identical, which is the same reasoning behind the editor's
        // deliberately non-black clear colour.
        ImGui::TextUnformatted("No frame yet — the scene has not published one.");
    }

    ImGui::End();
}

void StatsPanel::draw(EditorContext& context) {
    if (!ImGui::Begin(std::string(title()).c_str(), openFlag())) {
        ImGui::End();
        return;
    }

    ImGui::Text("Frame %llu", static_cast<unsigned long long>(context.frame));
    const double ms = context.deltaSeconds * 1000.0;
    ImGui::Text("%.2f ms (%.0f fps)", ms, ms > 0.0 ? 1000.0 / ms : 0.0);
    ImGui::Separator();

    ImGui::Text("Scene target %dx%d", context.sceneWidth, context.sceneHeight);
    if (context.scene != nullptr) {
        ImGui::Text("Entities %zu", context.scene->size());
    }

    if (context.camera != nullptr) {
        ImGui::Separator();
        const hp::float3 position = context.camera->position();
        const hp::float3 forward = context.camera->forward();
        ImGui::Text("Camera %.2f %.2f %.2f", position.x, position.y, position.z);
        ImGui::Text("Facing %.3f %.3f %.3f", forward.x, forward.y, forward.z);
        ImGui::Text("Fly speed %.2f m/s", context.camera->moveSpeed());
    }

    ImGui::End();
}

} // namespace hped
