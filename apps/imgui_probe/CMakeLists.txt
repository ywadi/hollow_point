cmake_minimum_required (VERSION 3.10)

project(ImGuiProbe CXX)

# A verbatim copy of DiligentSamples/Tutorials/Tutorial10_DataStreaming, kept as
# a build-and-run smoke test for the toolchain and, in particular, for the
# swapped-in docking build of Dear ImGui (see DILIGENT_DEAR_IMGUI_PATH in the
# root CMakeLists.txt).
#
# Tutorial10 was chosen because it drives ImGui hard and calls
# ImGui::InputInt(..., ImGuiInputTextFlags_EnterReturnsTrue), which routes into
# InputScalar -- exactly the path guarded by the assert that DiligentGraphics'
# ImGui fork used to comment out.
#
# The only edit to the copied source is a docking exercise, marked with
# HP_DOCKING_PROBE, plus an optional frame-limited exit so it can be verified
# headlessly. Delete this whole directory once a real app exists.

add_sample_app(ImGuiProbe
    IDE_FOLDER
        HollowPoint
    SOURCES
        src/Tutorial10_DataStreaming.cpp
    INCLUDES
        src/Tutorial10_DataStreaming.hpp
    SHADERS
        assets/polygon.vsh
        assets/polygon.psh
        assets/polygon_batch.vsh
        assets/polygon_batch.psh
    ASSETS
        assets/DGLogo0.png
        assets/DGLogo1.png
        assets/DGLogo2.png
        assets/DGLogo3.png
)

if(PLATFORM_LINUX)
    target_link_libraries(ImGuiProbe PRIVATE pthread)
endif()
