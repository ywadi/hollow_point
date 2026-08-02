# ImGuiProbe

A copy of `DiligentSamples/Tutorials/Tutorial10_DataStreaming`, kept as a
build-and-run smoke test for the build harness and for the swapped-in **docking**
build of Dear ImGui.

Tutorial10 was picked because it drives ImGui hard and calls
`ImGui::InputInt(..., ImGuiInputTextFlags_EnterReturnsTrue)`, which routes into
`InputScalar` — the path guarded by the assert that DiligentGraphics' ImGui fork
used to comment out.

## What it proves

- The whole engine compiles and links into a runnable executable through `zig cc`
- Dear ImGui resolves to `third_party/imgui` (ocornut `docking`), not the 1.92.1
  submodule inside DiligentEngine — `ImGuiConfigFlags_DockingEnable` and
  `DockSpaceOverViewport()` exist only on the docking branch, so the docking
  probe fails to compile against the bundled copy
- Assets, shaders and the Diligent ImGui renderer backend all work at runtime

## Running

```sh
zig build linux
./build/linux-x86_64-release/apps/imgui_probe/ImGuiProbe
```

Set `HP_PROBE_EXIT_FRAMES=N` to render N frames, print a one-line verdict and
exit — which is how it is checked on a headless display:

```sh
HP_PROBE_EXIT_FRAMES=120 xvfb-run -s "-screen 0 1280x720x24" \
    ./build/linux-x86_64-release/apps/imgui_probe/ImGuiProbe --mode gl
```

## Source changes

Exactly one edit to the copied tutorial, marked `HP_DOCKING_PROBE`: a
`HP_DockingProbe()` call at the top of `UpdateUI()` that enables docking, creates
a dockspace over the main viewport, adds a second dockable window, and handles
`HP_PROBE_EXIT_FRAMES`.

Delete this whole directory once a real app exists.
