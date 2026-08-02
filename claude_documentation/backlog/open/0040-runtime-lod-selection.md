# T0040 — Runtime LOD selection

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Phase** | 7 — Content pipeline |
| **Created** | 2026-08-02 |

## Why

Generating LODs (T0039) achieves nothing until the renderer picks between them.
This closes the loop: the draw submission chooses a level per mesh per frame.

## Done when

- [ ] Draw submission selects an LOD per instance each frame
- [ ] Selection uses screen-space error, not raw distance (see notes)
- [ ] A debug view shows which LOD each object is using
- [ ] LOD bias is tunable globally and per asset
- [ ] Triangle count drops measurably in a scene with distant geometry
- [ ] Selection cost is negligible — verified in Tracy, not assumed

## Subtasks

- [ ] 40.1 Screen-space error from the stored metric, bounding volume and camera
- [ ] 40.2 Hysteresis so objects near a threshold do not oscillate between levels
- [ ] 40.3 Global and per-asset LOD bias
- [ ] 40.4 Debug visualisation colouring by LOD level
- [ ] 40.5 Profiling zone around selection (T0019)
- [ ] 40.6 Measure the win on a scene built to exercise it

## Notes / findings

**Screen-space error beats distance.** Distance-based selection makes large and
small objects switch at the same range, so big objects visibly pop while small
ones waste triangles. Projected error accounts for size and is barely more work.

**Hysteresis is not optional.** Without it, an object hovering at a switching
threshold flickers between levels every frame, which is far more noticeable than
the pop it was meant to avoid. Different thresholds for switching up and down.

Smooth LOD transitions (dithered cross-fade) are out of scope — worth noting as a
possible follow-up once the basic mechanism is proven, since the pop is the main
complaint people have about discrete LODs.
