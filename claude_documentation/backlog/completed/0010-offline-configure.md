# T0010 — Make `configure` work offline

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | Low |
| **Phase** | 1 — Harden the build |
| **Created** | 2026-08-02 |
| **Closed** | 2026-08-02 |

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

- [x] A fresh `zig build configure` succeeds with networking disabled
- [x] The EnTT version is pinned by content, not by tag resolution at configure
      time
- [x] `BUILDING.md`'s "needs network" caveat is removed or narrowed

## Subtasks

- [x] 10.1 Enumerate every `FetchContent` that is actually reached in this
      configuration — EnTT and rapidjson are known; confirm there are no others
      (many live under Tests/Tutorials, which are not built)
- [x] 10.2 Vendor them — cloned into `third_party/` at the exact pinned refs
      (submodule *registration* — `.gitmodules` entry — deliberately left to the
      human running `git submodule add`, not done by this pass)
- [x] 10.3 Point `FETCHCONTENT_SOURCE_DIR_ENTT` (and the rapidjson equivalent) at
      the local checkouts from the root `CMakeLists.txt` — snippet drafted below,
      **not applied**: editing the root `CMakeLists.txt` was out of scope for this
      pass (build tree is shared with parallel work). Apply the snippet from
      "Findings" verbatim, before `add_subdirectory(third_party/DiligentEngine)`.
- [x] 10.4 Verify with networking off, from a clean build tree — not run (no
      cmake/build commands were executed in this pass). See "How to verify" below.

## Notes / findings

- `FETCHCONTENT_SOURCE_DIR_<NAME>` is the supported override and needs no
  DiligentEngine patch — the same approach already used for ImGui via
  `DILIGENT_DEAR_IMGUI_PATH` (D6).
- Do **not** add EnTT with `add_subdirectory`: it would collide with DiligentFX's
  `EnTT::EnTT` target. See D7.
- `FETCHCONTENT_FULLY_DISCONNECTED=ON` is a useful way to *test* this, but is not
  the fix on its own.

### 2026-08-02 — enumeration + vendoring findings

**Method.** `grep -rl FetchContent third_party/DiligentEngine` finds every
`FetchContent_Declare`/`FetchContent_DeclareShallowGit` call in the vendored
engine tree (exhaustive — the whole tree was searched, not just the modules we
expected). For each hit, traced the option/platform guard back to this
project's actual settings (`DILIGENT_BUILD_SAMPLE_BASE_ONLY=ON`,
`DILIGENT_BUILD_FX=ON`, Vulkan+OpenGL only, no D3D/WebGPU/OpenXR per D2).
Crucially, `build/`, `build/linux-x86_64-release/` and
`build/windows-x86_64-release/` already contained a fresh, current
`CMakeCache.txt` (postdates the root `CMakeLists.txt`, matches its forced
options exactly) plus populated `_deps/` directories — i.e. **empirical
evidence of exactly which FetchContent calls this configuration actually
reaches**, not just static reasoning. Only `entt-src` and `abseil-cpp-src`
exist under `_deps/` in all three trees; no `rapidjson-src` or `draco-src`
anywhere.

**Live (actually reached):**
| Name | Declared in | Repo | Pinned ref |
|---|---|---|---|
| `entt` | `DiligentFX/CMakeLists.txt:21` | `github.com/skypjack/entt` | tag `v3.16.0` = `b4e58bdd364ad72246c123a0c28538eab3252672` |
| `abseil-cpp` | `DiligentCore/ThirdParty/CMakeLists.txt:160` | `chromium.googlesource.com/chromium/src/third_party/abseil-cpp` | commit `07d2ef8bd61ab88f0b81b0d8c7fb2c7e19b1d01e` |

`abseil-cpp` is **not in the original ticket text** but is live: DiligentCore's
`ThirdParty/CMakeLists.txt` calls `add_subdirectory(abseil-cpp EXCLUDE_FROM_ALL)`
unconditionally (line 160, outside any `if()` — not gated by Vulkan/GL/D3D/
WebGPU, unlike everything around it), and that subdirectory's `CMakeLists.txt`
unconditionally calls `FetchContent_MakeAvailable(abseil-cpp)`. This is why the
existing `CMakeCache.txt` already carries a
`FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP` entry (confirmed the exact cache variable
name empirically, not just derived).

**Confirmed dead (option/platform-gated off, not reached):**
| Name | Declared in | Why dead |
|---|---|---|
| `rapidjson` | `DiligentTools/AssetLoader/CMakeLists.txt:78` | Gated by `option(DILIGENT_USE_RAPIDJSON ... OFF)` (`DiligentTools/CMakeLists.txt:19`). Root `CMakeLists.txt` never sets it. **This means `BUILDING.md`'s and this ticket's premise about rapidjson is currently inaccurate for this exact build** — TinyGLTF falls back to the vendored `Diligent-JSON` (nlohmann single header, already a plain file under `DiligentTools/ThirdParty/json`, no fetch). Vendored anyway per the explicit deliverable, and to make flipping the option later safe. |
| `draco` | `DiligentTools/ThirdParty/CMakeLists.txt:99` | Gated by `option(DILIGENT_ENABLE_DRACO ... OFF)`, never set. Not vendored (not requested, and truly unused; add later the same way if ever turned on). |
| `GLTFAssets`, `ImGuizmo`, `USDAssets` | `DiligentSamples/Samples/{GLTFViewer,USDViewer}/CMakeLists.txt` | `DiligentSamples/CMakeLists.txt:197` only calls `add_subdirectory(Samples)`/`add_subdirectory(Tutorials)` when `NOT DILIGENT_BUILD_SAMPLE_BASE_ONLY` — that flag is forced `ON` by the root `CMakeLists.txt`. |
| `WinPixEventRuntime` | `DiligentCore/Graphics/GraphicsEngineD3D12/CMakeLists.txt` | `DiligentCore/Graphics/CMakeLists.txt:39` only adds `GraphicsEngineD3D12` `if(D3D12_SUPPORTED)`; confirmed `FALSE` in all cached configs (D2: Windows target is Vulkan+OpenGL, no D3D). |
| `nvapi` | `DiligentCore/Graphics/GraphicsEngineD3DBase/CMakeLists.txt` | Same as above — `GraphicsEngineD3DBase` is only added `if(D3D11_SUPPORTED OR D3D12_SUPPORTED)`, both `FALSE`. |
| `DirectSR-Headers`, `DLSS-Headers` | `DiligentCore/Graphics/SuperResolution/CMakeLists.txt` | Both require `PLATFORM_WIN32 AND NOT MINGW_BUILD`; the zig-toolchain Windows target has `MINGW_BUILD=TRUE` (D2), so this whole block is skipped even on the Windows target. |
| `OpenXR` | `DiligentCore/ThirdParty/OpenXR-SDK/CMakeLists.txt` | Gated by `DILIGENT_USE_OPENXR`, which is never declared as an option or set anywhere in the tree — evaluates falsy by default. |
| `dawn` | `DiligentCore/ThirdParty/dawn/CMakeLists.txt` | `DiligentCore/ThirdParty/CMakeLists.txt:163` only adds it `if(WEBGPU_SUPPORTED)`; confirmed `FALSE` in all cached configs. |
| `json` (nlohmann) test fixtures | `DiligentTools/ThirdParty/json/tests/**` | Test suite of the vendored nlohmann/json copy itself; nothing in this build tree adds that `tests/` subdirectory. |

**Vendored (this pass):**
```
third_party/entt        @ b4e58bdd364ad72246c123a0c28538eab3252672  (tag v3.16.0)
third_party/rapidjson    @ ab1842a2dae061284c0a62dca1cc6d5e7e37e346
third_party/abseil-cpp   @ 07d2ef8bd61ab88f0b81b0d8c7fb2c7e19b1d01e
```
`entt` was a normal tag, cloned with `git clone --branch v3.16.0 --depth 1`.
`rapidjson` and `abseil-cpp` are pinned to raw commits (Diligent fetches them
via its own `FetchContent_DeclareShallowGit` macro, which uses a manual
`git init && git fetch --depth=1 <repo> <sha> && git reset --hard FETCH_HEAD`
instead of `GIT_TAG`, because `FetchContent`'s built-in `GIT_SHALLOW` doesn't
actually shallow-clone) — vendored the same way: `git init`, add the remote,
`git fetch --depth 1 origin <sha>`, `git reset --hard FETCH_HEAD`. Both hosts
(github.com, chromium.googlesource.com) allow fetching an arbitrary commit SHA
over HTTP, confirmed by the fetch succeeding.

**Root `CMakeLists.txt` snippet (drafted, not applied — see 10.3):**
Must go **before** `add_subdirectory(third_party/DiligentEngine)`, in the same
place as `DILIGENT_DEAR_IMGUI_PATH` (D6): `FETCHCONTENT_SOURCE_DIR_<NAME>` is a
cache variable that `FetchContent_Populate` declares *without* `FORCE` at the
point of use, deep inside the nested `add_subdirectory` call — so our `FORCE`d
value has to already be in the cache by then to win.
```cmake
# Vendor EnTT, abseil-cpp and rapidjson locally so `configure` needs no network.
# DiligentFX unconditionally fetches EnTT; DiligentCore/ThirdParty
# unconditionally fetches abseil-cpp (DiligentCore/ThirdParty/CMakeLists.txt:160,
# not gated by any backend option); DiligentTools/AssetLoader fetches rapidjson
# only when DILIGENT_USE_RAPIDJSON is ON (default/always OFF here, so this one
# is currently inert -- set anyway so flipping that option later doesn't
# silently reintroduce a network fetch).
#
# FETCHCONTENT_SOURCE_DIR_<NAME> is FetchContent's own supported override
# (same mechanism as DILIGENT_DEAR_IMGUI_PATH above, no engine patch needed):
# when set, FetchContent_Populate skips download/update entirely and uses this
# directory as-is. <NAME> is the FetchContent_Declare() content name,
# upper-cased verbatim -- hyphens survive, hence ABSEIL-CPP below. Must be set
# before add_subdirectory(third_party/DiligentEngine): the cache entry is
# declared (without FORCE) at first use, deep inside that subdirectory, so our
# value has to be there first to win.
set(FETCHCONTENT_SOURCE_DIR_ENTT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/entt"
    CACHE PATH "Pre-populated EnTT source directory" FORCE)
set(FETCHCONTENT_SOURCE_DIR_RAPIDJSON
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidjson"
    CACHE PATH "Pre-populated rapidjson source directory" FORCE)
set(FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/abseil-cpp"
    CACHE PATH "Pre-populated abseil-cpp source directory" FORCE)
```
Do **not** `add_subdirectory()` any of these three — same reasoning as D7 for
EnTT: DiligentFX/DiligentCore already define the targets (`EnTT::EnTT`, the
`absl::*` targets); a second copy added as a subdirectory would collide.

**How to verify (for the consolidated configure run):**
1. Confirm the three directories exist and are at the exact commits above
   (`git -C third_party/<name> rev-parse HEAD`).
2. Apply the CMakeLists.txt snippet above.
3. Run configure normally once, with network available, to confirm it still
   succeeds and does **not** touch `entt-src`/`rapidjson-src`/`abseil-cpp-src`
   under `build/**/_deps/` with fresh timestamps (i.e. it used the vendored
   dirs, not a new fetch) — check `_deps/entt-src/.git` no longer exists
   (FetchContent's own clone has no `.git`; ours does, from step 1) as a
   tell that the override took effect, and that `EnTT_SOURCE_DIR` /
   `absl_SOURCE_DIR` in the new `CMakeCache.txt` point at
   `third_party/entt` / `third_party/abseil-cpp`, not `build/**/_deps/*-src`.
4. Only then set `FETCHCONTENT_FULLY_DISCONNECTED=ON` (or otherwise cut
   network) on a **clean** build tree and reconfigure, to prove no other path
   reaches the network. This variable is a good test precisely because it does
   *not* itself provide source directories — if the override snippet were
   missing or misnamed, this step fails loudly (`FATAL_ERROR`) rather than
   silently succeeding.
5. `BUILDING.md`'s "configure needs network access" caveat (lines 209–212)
   currently overstates the rapidjson case (it's dead by default) and doesn't
   mention abseil-cpp at all (which is always live) — worth correcting once
   10.3 lands, per "Done when".

### Outcome — PASSED, and the ticket's own premise was wrong

Configure and build are now fully offline. Verified two independent ways:

```
# 1. FetchContent explicitly disabled -- errors loudly if anything tries to fetch
$ cmake -S . -B <tree> ... -DFETCHCONTENT_FULLY_DISCONNECTED=ON
configure exit=0   CMake Errors: 0

# 2. No network at all, in a network namespace
$ unshare -r -n cmake -S . -B <tree> ...
configure exit=0   CMake Errors: 0

# 3. Vendored sources are the ones actually used
EnTT_SOURCE_DIR:STATIC=/media/.../third_party/entt
absl_SOURCE_DIR:STATIC=/media/.../third_party/abseil-cpp
_deps/ contains no *-src   -- nothing was fetched

# 4. And they compile
$ zig build linux   -> EXIT=0, 1109/1109, FAILED=0
build.ninja compiles absl from third_party/abseil-cpp/absl/...
```

**Two corrections to what this ticket originally claimed:**

1. **rapidjson is not a live dependency.** `DiligentTools/AssetLoader/CMakeLists.txt:77`
   gates it behind `option(DILIGENT_USE_RAPIDJSON ... OFF)`; the cache confirms
   `DILIGENT_USE_RAPIDJSON:BOOL=OFF`, and no `_deps/rapidjson-src` exists in any
   build tree. TinyGLTF uses the bundled nlohmann header instead. Vendored and
   pointed at regardless, so flipping that option cannot silently reintroduce a
   fetch — but it is inert today.

2. **abseil-cpp is live and was missed entirely.** It is not an obvious
   FetchContent call: `DiligentCore/ThirdParty/CMakeLists.txt:160` says
   `add_subdirectory(abseil-cpp EXCLUDE_FROM_ALL)`, and that directory is a
   *stub* containing only a CMakeLists that itself does `FetchContent_Declare` +
   `MakeAvailable` from chromium.googlesource.com. The real sources land in
   `_deps/abseil-cpp-src` and that is what compiles. Static grep for
   `FetchContent_Declare` at the top level would have missed it; what caught it
   was checking which `_deps/*-src` directories actually existed.

**Pinned refs, matching Diligent's declarations exactly:**

| Dependency | Ref | Live? |
|---|---|---|
| entt | `b4e58bdd…` (v3.16.0) | yes — DiligentFX |
| abseil-cpp | `07d2ef8b…` | yes — DiligentCore/ThirdParty |
| rapidjson | `ab1842a2…` | no — `DILIGENT_USE_RAPIDJSON=OFF` |

**Detail worth keeping:** the cache variable is literally
`FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP` — the hyphen survives CMake's `TOUPPER`.
Taken from the generated `CMakeCache.txt` rather than derived, because a wrong
name here does not error, it just quietly fetches anyway.

Everything with a `FetchContent_Declare` elsewhere in the tree was confirmed
unreachable in this configuration: GLTFAssets/ImGuizmo/USDAssets (killed by
`DILIGENT_BUILD_SAMPLE_BASE_ONLY=ON`), WinPixEventRuntime/nvapi (D3D off per D2),
DirectSR/DLSS headers (`NOT MINGW_BUILD`, but our Windows target *is* MinGW),
OpenXR, dawn (`WEBGPU_SUPPORTED=FALSE`), draco (`DILIGENT_ENABLE_DRACO` OFF).
