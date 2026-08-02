# T0010 — Make `configure` work offline

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Low |
| **Created** | 2026-08-02 |

## Why

`cmake configure` currently **requires network access**: DiligentFX fetches EnTT
(v3.16.0) and DiligentTools fetches rapidjson via CMake `FetchContent`. Builds
are offline once configured, but a fresh build tree is not.

That undercuts the reproducibility the rest of the harness works hard for — the
toolchain is pinned and checksum-verified, the sysroot is vendored, and then
configure reaches out to GitHub and takes whatever the tag points at today. It
also means no air-gapped or CI-cache-only build, and a configure that fails for
reasons unrelated to the code.

## Done when

- [ ] A fresh `zig build configure` succeeds with networking disabled
- [ ] The EnTT version is pinned by content, not by tag resolution at configure
      time
- [ ] `BUILDING.md`'s "needs network" caveat is removed or narrowed

## Subtasks

- [ ] 10.1 Enumerate every `FetchContent` that is actually reached in this
      configuration — EnTT and rapidjson are known; confirm there are no others
      (many live under Tests/Tutorials, which are not built)
- [ ] 10.2 Vendor them as submodules under `third_party/`
- [ ] 10.3 Point `FETCHCONTENT_SOURCE_DIR_ENTT` (and the rapidjson equivalent) at
      the local checkouts from the root `CMakeLists.txt`
- [ ] 10.4 Verify with networking off, from a clean build tree

## Notes / findings

- `FETCHCONTENT_SOURCE_DIR_<NAME>` is the supported override and needs no
  DiligentEngine patch — the same approach already used for ImGui via
  `DILIGENT_DEAR_IMGUI_PATH` (D6).
- Do **not** add EnTT with `add_subdirectory`: it would collide with DiligentFX's
  `EnTT::EnTT` target. See D7.
- `FETCHCONTENT_FULLY_DISCONNECTED=ON` is a useful way to *test* this, but is not
  the fix on its own.
