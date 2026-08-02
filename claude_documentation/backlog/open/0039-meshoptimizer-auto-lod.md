# T0039 — Automatic LOD generation with meshoptimizer

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
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
