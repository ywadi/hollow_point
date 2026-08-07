# T0167 — A model from Sketchfab, rendered and measured: the first asset chosen by somebody other than us

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 463 |
| **Created** | 2026-08-08 |
| **Blocked by** | [0166-tangent-frames-and-real-assets.md](0166-tangent-frames-and-real-assets.md) — **not a soft ordering.** A production model has several things wrong at once; run it against a known-clean frame or the wrong cause gets debugged |
| **Refs** | [0166-tangent-frames-and-real-assets.md](0166-tangent-frames-and-real-assets.md) — supplies the controlled cases this ticket deliberately is not; [../completed/0165-right-handed-engine.md](../completed/0165-right-handed-engine.md) — **this is the observation D33's amendment was argued from and never made**; [../completed/0157-rock-cube-sample.md](../completed/0157-rock-cube-sample.md) — the authoring path a real asset re-walks; [../completed/0142-slang-shader-language.md](../completed/0142-slang-shader-language.md), [../completed/0143-extended-material-features.md](../completed/0143-extended-material-features.md) — the features a real material will actually exercise; [0087-environment-lighting.md](0087-environment-lighting.md) — a real PBR asset will look wrong without IBL, and that is **not** a defect this ticket may report; [../../documentation/11-material-format.md](../../documentation/11-material-format.md); **D13**, **D33** |

## Why

**The engine has never been shown an asset it did not help make.** The owner is supplying one from Sketchfab — chosen for what it is, not for what it tests — and that is the point: it carries whatever a real artist's export carries, including the things nobody here would think to write.

**It is also the observation D33's amendment was argued from.** T0165 made the engine right-handed on the reasoning that *a Blender artist's model arrives as authored*. That claim is currently **derived, not observed**. One model settles it.

## Done when

- [ ] A Sketchfab model renders in the editor, and the result is judged **both by eye and by number** — a screenshot alongside measurements, not one standing in for the other
- [ ] Orientation, scale and handedness are confirmed against the source: **what the artist authored is what appears**, with the axis and units stated
- [ ] Every defect found is either fixed, ticketed, or written down as accepted — with which of the three, and why
- [ ] What the asset **did not** exercise is listed, because that is the next asset's brief

## Subtasks

- [ ] 167.1 **Take the asset as it comes.** No re-export, no axis fix-up, no re-bake, no hand-edited glTF. The moment it is adjusted to suit the engine it stops being the test — and "we had to fix the asset first" is itself the finding
- [ ] 167.2 **Import and report what the loader saw**: meshes, primitives, materials, attributes present and absent (`TANGENT` especially), texture count and colour spaces, extensions, units and bounds. Much of this is already logged; what is missing is reading it deliberately
- [ ] 167.3 **Render and look**, in the editor, from several angles, with the light moved. Capture frames. **The rock cube's black top face is unexplained and this is where a second data point comes from**
- [ ] 167.4 **Measure, do not only look.** A gpu case that renders it offscreen and asserts something falsifiable — coverage, the magenta and checkerboard guards, luminance response to a moved light, and orientation against the source. **A screenshot alone has misled this project twice**: a magenta checkerboard was read as a working render because the summary statistics passed
- [ ] 167.5 **Test the handedness claim explicitly.** Front is front, left is left, up is up, one unit is one unit — against the source in Blender, which is available in this session over MCP
- [ ] 167.6 **Separate defects from absences.** A real PBR asset will look flat and dark without IBL, and that is T0087's, not a bug. So will anything expecting shadows (T0086). **Say which bucket each finding is in** — a missing system reported as a defect is how a clean engine gets "fixed" into a broken one
- [ ] 167.7 **Decide whether the asset joins the repository**, and on what terms. Licence, size, and whether it becomes a CI fixture or a one-off measurement. Sketchfab licences vary and a redistributable one is a requirement, not a detail — **if it cannot be committed, the measurement still counts and the ticket says so**

## Not in scope

- **Fixing whatever it finds**, beyond what is small and obviously right. The value is the list; a large finding earns its own ticket rather than dragging this one open.
- **A performance claim.** One model on one machine measures nothing about the renderer's speed. T0045 and T0050 own that.
- **Anything about how good it looks.** Missing IBL, missing shadows and untuned exposure are systems that do not exist yet, and 167.6 exists to stop them being reported as regressions.

## Notes / findings

### Two traps that have already cost this project time, and both apply here

- **A failed shader renders magenta (~127, 0, 127) and passes coverage assertions.** Every asserted frame needs the magenta guard, and the checkerboard guard for a missing material. Both were read as working renders in a single session.
- **The gpu suite keeps its own asset import list, separate from a module's.** A file added to one and not the other imports in the editor and is silently absent in the test, or the reverse.

### Why this is second rather than first

Not caution — sequencing. A production model that renders wrong says *something* is wrong, not *which* thing, and T0166 has one confirmed defect (the tangent frame's dropped determinant sign) plus several unchecked conventions in flight. Landing this first means the first real asset renders wrong for two reasons at once, and the trap is that the visible one gets blamed. T0157 lost a day to exactly that shape.
