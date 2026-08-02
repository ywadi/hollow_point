# T0008 — Remove the `ImGuiKey_Mod*` compile-definition shim

| | |
|---|---|
| **Status** | ⏸ BLOCKED on DiligentEngine upstream |
| **Priority** | Low |
| **Created** | 2026-08-02 |
| **Refs** | [../../documentation/04-cross-compile-gotchas.md](../../documentation/04-cross-compile-gotchas.md) G7 |

## Why

The root `CMakeLists.txt` carries compile definitions mapping
`ImGuiKey_ModCtrl/ModShift/ModAlt/ModSuper` → `ImGuiMod_*` on `Diligent-Imgui`.

ImGui renamed these in **1.89** and kept obsolete aliases; the aliases were still
live in the 1.92.1 Diligent bundles but are **removed by 1.92.9**, which is what
`third_party/imgui` now pins. Diligent's Linux and Emscripten ImGui impls still
use the old spellings.

It is a deliberate, minimal, documented workaround — but it is still a
compatibility shim papering over upstream source, and it will silently stop being
necessary without anyone noticing.

## Done when

- [ ] DiligentEngine's `ImGuiImplLinuxX11.cpp`, `ImGuiImplLinuxXCB.cpp` and
      `ImGuiImplEmscripten.cpp` use the modern names
- [ ] The `target_compile_definitions(Diligent-Imgui ...)` block is deleted
- [ ] Linux builds clean without it
- [ ] G7 marked resolved

## Subtasks

- [ ] 8.1 On each DiligentEngine update, delete the block and try a build — that
      is the whole test
- [ ] 8.2 If it still fails, restore it and move on
- [ ] 8.3 Consider reporting it upstream; the fix is a three-file rename and they
      will hit it themselves the moment they bump ImGui

## Notes / findings

- Affects `Diligent-Imgui` only. The Win32 impl comes from ImGui's own current
  backend and was never affected — which is exactly why the Windows build stayed
  green while Linux broke.
