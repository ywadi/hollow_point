# T0039 — Automatic LOD generation with meshoptimizer

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 7 — Content pipeline |
| **Created** | 2026-08-02 |

## Why

Automatic LOD generation on import — the way Godot does it — means artists do not
hand-author LOD chains, and distant geometry stops costing full triangle counts.
This is one of the highest-leverage performance features available, and
meshoptimizer is already vendored.

Generation happens **offline**, at import/convert time, not at runtime: it is far
too expensive per frame, and doing it once produces deterministic results.

## Done when

- [ ] Import generates an LOD chain per mesh at configurable reduction ratios
- [ ] Vertex cache, overdraw and vertex fetch optimisation applied to each level
- [ ] LOD levels stored in the asset, with the error metric each was built at
- [ ] Generation is optional and tunable per asset, not forced
- [ ] Triangle counts and generation time are reported so the effect is visible
- [ ] A mesh with LODs renders identically at LOD0 to one without

## Subtasks

- [ ] 39.1 Integrate `meshopt_simplify` into the import/convert pipeline
- [ ] 39.2 Configurable ratios (e.g. 100/50/25/12%) with a target error bound
- [ ] 39.3 `meshopt_optimizeVertexCache`, `optimizeOverdraw`, `optimizeVertexFetch`
      per level — order matters, follow meshoptimizer's documented sequence
- [ ] 39.4 Fall back to `meshopt_simplifySloppy` when the error bound cannot be
      met, and record that it happened
- [ ] 39.5 Extend the asset format to carry the LOD chain and its error metrics
- [ ] 39.6 Per-asset overrides in the metafile
- [ ] 39.7 Parallelise across meshes via the job system (T0026)

## Notes / findings

**Simplification breaks on non-manifold and seam-heavy meshes**, which is most
real game art. meshoptimizer handles this by refusing to collapse across
attribute seams, so results are safe but reduction targets are often not met.
Report the achieved ratio rather than assuming the requested one — silently
shipping a "LOD3" that is 90% of the original is worse than no LOD.

`meshopt_simplify` preserves the vertex layout, so LODs can share the original
vertex buffer with only index buffers differing. That is a significant memory
saving and worth designing the asset format around from the start.

Store the **error metric** alongside each level. Runtime selection (T0040) should
choose by screen-space error, not by raw distance, and it needs that number.

### Architecture review (2026-08-03) — 39.5's "asset format" does not exist yet

"Extend the asset format to carry the LOD chain" presumes an engine mesh
format, but the pipeline's source of truth is glTF (T0038) and glTF core has
no LOD concept. The options are real and nobody owns the choice yet:
(a) the `MSFT_lod` glTF extension — stays in the interchange format, but
Diligent's loader must be checked for whether it surfaces it; (b) an
engine-side **cooked mesh** container produced at import (which is also where
T0045's import-time bounding volumes and the shared-vertex-buffer layout this
ticket describes naturally live); or (c) sidecar files per LOD, which is the
option that rots. This decision shapes T0023's pool, T0040's selection and
T0043's export — make it deliberately at the start of this ticket, and record
it.
