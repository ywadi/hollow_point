# T0005 — Actually call enkiTS / meshoptimizer / ozz

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Phase** | 3 — Content & libraries |
| **Created** | 2026-08-02 |

## Why

enkiTS, meshoptimizer and ozz-animation **compile and link for both targets**,
and that is the entire extent of what is known. No code calls a single function
in any of them.

Compiling proves headers parse and symbols resolve. It does not prove the
libraries behave — and ozz in particular was built with most of its options off
(D8), so whole subsystems are absent rather than merely unused.

## Done when

- [ ] Each library has at least one real call exercised at runtime on Linux
- [ ] The same code cross-compiles for Windows
- [ ] Anything that turns out to need a disabled ozz option is written down here

## Subtasks

- [ ] 5.1 **enkiTS** — create a `TaskScheduler`, run a parallel-for, join. Watch
      for thread-creation issues under the pinned glibc 2.28.
- [ ] 5.2 **meshoptimizer** — run `meshopt_optimizeVertexCache` and
      `meshopt_simplify` over a real mesh; confirm index counts change sensibly.
- [ ] 5.3 **ozz** — load a skeleton + animation and sample it. **Blocked in
      practice**: ozz's importers are off (`ozz_build_fbx`, `ozz_build_gltf`), so
      there is no way to *produce* the `.ozz` runtime files yet. Resolve that
      first — see notes.
- [ ] 5.4 Decide where this lives: throwaway probe, or the real app (T0006)

## Notes / findings

- **The ozz importer problem is the real content here.** `ozz_build_tools` and
  `ozz_build_gltf` were disabled because the samples need GLFW/OpenGL and the FBX
  pipeline needs the proprietary FBX SDK. But the glTF importer (`ozz_build_gltf`)
  does *not* need the FBX SDK — it was disabled together with the rest of the
  tools, which may have been too broad. Options:
  - re-enable `ozz_build_gltf` + `ozz_build_tools` for **host** builds only, and
    run the converter offline as an asset-pipeline step
  - or convert assets elsewhere and commit `.ozz` files
  The first is probably right, since the tools only ever need to run on the dev
  machine, never on the target.
- Related: `ufbx` (T0009) exists in the tree and may be the intended FBX route
  instead of ozz's FBX pipeline.
