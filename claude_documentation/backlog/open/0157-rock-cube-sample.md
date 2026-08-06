# T0157 — The rock cube: a sample that is also the authoring path's first real test

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 463 |
| **Created** | 2026-08-06 |
| **Blocked by** | [0156-parallax-under-triplanar.md](../inprogress/0156-parallax-under-triplanar.md) — triplanar and POM together is the thing being shown. **In `inprogress/` as of 2026-08-06; this link moves to `../completed/` when it closes** — `check_backlog.py` will say so |
| **Refs** | [../completed/0022-scene-serialization.md](../completed/0022-scene-serialization.md) — **this is the first non-test consumer of its loader**; [../completed/0139-hand-authored-scenes.md](../completed/0139-hand-authored-scenes.md) — hand-authoring is the claim being tested; [../completed/0060-material-system.md](../completed/0060-material-system.md) — the material as an asset file; [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) 141.7/141.8 — POM and triplanar; [0062-entity-behaviours.md](0062-entity-behaviours.md) — **157.6 may be a finding for it**; T0109 — a sample is not a game; **D12** (gameplay in lockstep), **D13** (VFS), T0104 (the build-id handshake) ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)) |

## Why

**The owner wants something to look at:** a rotating cube, a light, and the rock
material with parallax occlusion and triplanar — *"just to stare at it."*

That is a good enough reason on its own, and there is a better one underneath it.

**Nothing in this project can be opened and looked at.** The editor's viewport
shows a programmatically-built quad. Every visual claim the engine makes lives in
a pixel assertion — which is right, and which cannot catch what a person catches
in a second. A meaningful share of the real bugs found on 2026-08-06 were found
**by eye**: the test light raking from behind, the quad 2.3× oversized for its
frame, the washed-out normal map, the inverted winding. A permanent demo scene is
a regression detector of a kind the gpu suite structurally cannot replicate.

**And the scene format has never been used outside its own tests.**
`loadSceneFromString` and `loadSceneFromCooked` exist and work (T0022), and
**every caller is in `tests/fast/scene_serialize_test.cpp`** — measured
2026-08-06. Neither the editor nor the runtime loads a scene file; the editor
builds its demo in C++.

A format exercised only by round-trip tests is proven **self-consistent**, not
proven **usable**. Those are different claims, and T0139 exists because the
difference matters. This sample makes the second claim for the first time — and
if hand-authoring turns out to be awkward, that is the most valuable output here,
not a side effect.

## Done when

- [ ] A **new sample module** renders a rotating cube with the rock material,
      **POM and triplanar together**, visible in the editor
- [ ] **The scene is a hand-authored file** loaded through the scene loader — not
      constructed in C++. If any part cannot be expressed in the format, that is
      recorded rather than worked around in code
- [ ] **The material is an asset file**, not built in code
- [ ] Textures resolve **through the VFS** (D13), not by absolute path
- [ ] The sample is a **gameplay module** across the real boundary — D12's
      lockstep and T0104's build id, exercised rather than assumed
- [ ] **What the formats made awkward is written down**, with the ticket that
      should fix each thing. This is a deliverable, not a footnote

## Subtasks

- [ ] 157.1 The sample skeleton — `samples/rockcube/`, plus one
      `add_subdirectory` line beside `samples/sandbox` at `CMakeLists.txt:450`.
      Confirm a second sample needs nothing else; if it does, that is finding one
- [ ] 157.2 **The cube mesh — decide, do not default.** Two options and they are
      not equivalent: an authored glTF asset (smallest now), or a **primitive
      generator** (cube/plane/sphere) which the editor will want anyway the first
      time somebody wants *Add ▸ Cube*, and which every comparable engine ships.
      Record the choice and the reasoning
- [ ] 157.3 **The hand-authored `.hpscene`** — cube, light, camera. The first
      non-test consumer of `loadSceneFromString`. Author it **by hand from
      `10-scene-file-format.md`**, not by saving one out of code — saving and
      reloading proves round-tripping, which is already tested; *writing* one
      proves the format is authorable, which is not
- [ ] 157.4 The `.hpmat` — rock base colour, normal, ORM and height, with POM and
      triplanar enabled
- [ ] 157.5 **Where sample content lives.** The rock set is in
      `test_assets/derived/`, whose name says it is for tests. Mount it, copy it,
      or give samples their own content root — decide, because "the sample reads
      the test assets" is the kind of coupling that is fine until it is not
- [ ] 157.6 **Rotation, and verify the hook before designing around it.** What
      per-frame update a gameplay module actually gets is unconfirmed, and
      **T0062 (entity behaviours) is open** — so the answer may be less than
      hoped. If the module cannot cleanly move a transform per frame, **that is a
      finding for T0062**, recorded there with a two-way reference, not a
      workaround here
- [ ] 157.7 **The awkwardness log** — everything the scene format, the material
      format or the module boundary made harder than it should have been, each
      pointed at its owning ticket

## Not in scope

- **This is not "the game."** T0109 is explicit: real games are separate
  projects, and a sample exists to exercise the engine. It should stay small
  enough to read in one sitting.
- **A feature showcase.** One cube, one light, one material. The moment it grows
  a menu it stops being a test of the authoring path and becomes a thing to
  maintain.
- **Editor scene loading.** The editor building its viewport content
  programmatically is a separate question (T0032/T0033 territory); this sample
  loads its own scene.

## Notes / findings

### Measured 2026-08-06 — what exists, so 157.1 starts from fact

- `samples/` contains only `sandbox`, and there is no `samples/CMakeLists.txt`
  doing registration — `CMakeLists.txt:450` adds it directly, so a second sample
  is one line beside it.
- The scene **read** path is real: `loadSceneFromString`, `loadSceneFromCooked`,
  and a `SceneLoadResult` status type carrying a staleness check.
- **Callers outside `tests/`: none.** That is the gap this ticket closes, and it
  is why 157.3 insists the scene is written by hand rather than saved out of
  code.
- The editor's current demo writes glTF to a temp directory at runtime and loads
  `models/quad.gltf` — the pattern to learn from, and the one this replaces for
  the sample's own content.
