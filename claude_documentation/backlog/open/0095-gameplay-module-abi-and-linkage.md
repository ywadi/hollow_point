# T0095 — Gameplay module ABI: engine linkage, one engine state, entt across the boundary

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 2 — Engine skeleton |
| **Created** | 2026-08-03 |
| **Blocks** | T0048, T0062 |
| **Refs** | T0013, T0055, T0056, T0076, T0094 |

## Why

T0013 makes the engine a **static** library. T0048 makes gameplay a **shared**
library loaded by the editor and the runtime. Nobody has decided how the
gameplay module links against the engine, and the default answer — statically
link the engine into the module too — is silently catastrophic: **two copies of
every engine global**, one in the executable and one in the game DLL. The
logger, the autoload registry, the asset pool's statics, and entt's type-index
counters all exist twice, and each side sees only its own.

The entt case is the sharpest. Verified against the vendored copy
(`third_party/entt/src/entt/core/type_info.hpp`): type identity is *partly*
name-hash based (stable across modules on one compiler) but `type_info::index()`
comes from `internal::type_index::next()` — a **static sequential counter**,
marked `ENTT_API` precisely because it must be a single instance across
boundaries. With the engine linked into both sides, the exe's registry and the
module's component registrations can disagree about type indices. The failure
is not a crash at the boundary; it is components quietly landing in the wrong
pools, which presents as impossible bugs in gameplay code.

The platform asymmetry makes this a real design decision rather than a flag:

- **Linux:** a `dlopen`'d module can resolve engine symbols from the executable
  if the exe is linked `--export-dynamic`. Cheap, conventional.
- **Windows:** PE has no such mechanism. A DLL imports from a *named* module —
  so either the engine becomes `engine.dll` that both the exe and the game
  module import, or the exe itself exports engine symbols and the module links
  against the exe's import library (possible under MinGW, unusual, fragile).

If this is discovered *after* T0048/T0062 are built, the fix is relinking the
world — exactly the "hacking the engine" outcome this backlog exists to avoid.

## Done when

- [ ] The linkage model is decided and recorded in the decision log: engine
      static vs shared, per configuration (dev with hot reload vs shipped
      static, per T0048.7), and how the module resolves engine symbols on
      **both** platforms
- [ ] Exactly one instance of every engine global exists at runtime — proven by
      a test (e.g. exe and module both log the address of the same engine
      static and they match)
- [ ] entt type identity holds across the boundary: a component type used from
      the module is visible and iterable from engine code and vice versa, on
      both targets — tested, not assumed
- [ ] `dynamic_cast`/`typeid` on types crossing the boundary either works
      (typeinfo symbols unified) or is banned by convention (T0055) — decided,
      not left to chance
- [ ] Dev and shipped configurations build from the same source with no
      `#ifdef` spread through gameplay code
- [ ] The rules are written into the conventions doc (T0055)

## Subtasks

- [ ] 95.1 Decide: engine as shared library in dev builds (one copy of state,
      conventional on both platforms) vs exe-exports (Linux-easy,
      Windows-fragile). Prototype the Windows side under the zig/MinGW
      toolchain **before** committing — this project's history (G2, G3, G4) is
      that Windows linking surprises are real
- [ ] 95.2 Configure entt for cross-boundary use — `ENTT_API_EXPORT` /
      `ENTT_API_IMPORT` on the right sides — and confirm name-based
      `type_hash` behaves identically for both modules under zig's clang
- [ ] 95.3 Boundary test: two component types (one engine-defined, one
      module-defined), created on one side, queried on the other, surviving a
      hot reload
- [ ] 95.4 Symbol visibility policy for shared types (vtables and typeinfo
      need default visibility; `-fvisibility=hidden` everywhere else is still
      fine)
- [ ] 95.5 Exception and allocation rules at the boundary, folded into T0055
      (Diligent throws internally; the module boundary should not)
- [ ] 95.6 Re-verify T0048's mechanics against the chosen model: the *game*
      DLL is copied-before-load and reloaded; the *engine* DLL (if that is the
      choice) is loaded once and never reloaded
- [ ] 95.7 Check `dist`/export staging carries the engine shared library
      correctly on both platforms (T0043; Windows needs it beside the exe)

## Notes / findings

- **Why this is Phase 2:** T0013 (the split) and T0048 (the module) are both
  Phase 2, and this decision changes what both of them build. Doing T0013 with
  "engine = static lib" as an unexamined default is the trap.
- entt arrives via DiligentFX at Diligent's pinned ref (D7) — the
  `ENTT_API` configuration has to be injected as compile definitions from our
  root `CMakeLists.txt`, never by patching the fetched copy. Confirm the
  definitions reach every target that includes an entt header, including the
  gameplay module, or the setup is worse than none.
- The same one-instance argument applies to anything with static state that
  both sides touch: the reflection registry (T0053), the autoload registry
  (T0076), the log sinks (T0054). Designing those as instance objects handed
  across the boundary (context pointer), rather than as globals reached by
  symbol, sidesteps most of the problem and is worth adopting as the
  convention regardless of the linkage choice.
- T0056's "use Diligent math types in gameplay" is compatible with all of
  this — header-only value types have no linkage. The hazard is *stateful*
  engine code and *RHI interface pointers* (T0094's concern), not PODs.
- If the engine becomes a shared library, symbol export lists / visibility
  macros (`HP_API`) need deciding at T0013 time — retrofitting export macros
  onto hundreds of headers is exactly the kind of mechanical churn worth
  avoiding by deciding now.
