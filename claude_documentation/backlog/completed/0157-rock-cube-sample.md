# T0157 — The rock cube: a sample that is also the authoring path's first real test

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 463 |
| **Created** | 2026-08-06 |
| **Blocked by** | [0156-parallax-under-triplanar.md](../completed/0156-parallax-under-triplanar.md) — triplanar and POM together is the thing being shown. **Closed 2026-08-06** — parallax under triplanar landed, so this is buildable now |
| **Refs** | [../completed/0022-scene-serialization.md](../completed/0022-scene-serialization.md) — **this is the first non-test consumer of its loader**; [../completed/0139-hand-authored-scenes.md](../completed/0139-hand-authored-scenes.md) — hand-authoring is the claim being tested; [../completed/0060-material-system.md](../completed/0060-material-system.md) — the material as an asset file; [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) 141.7/141.8 — POM and triplanar; [0062-entity-behaviours.md](../open/0062-entity-behaviours.md) — **157.6 may be a finding for it**; T0109 — a sample is not a game; **D12** (gameplay in lockstep), **D13** (VFS), T0104 (the build-id handshake) ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)) |

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

- [x] A **new sample module** renders a rotating cube with the rock material,
      **POM and triplanar together**, visible in the editor — *delivered, and
      then deliberately changed the same day: the sample is now **UV-mapped**
      POM, not triplanar. Triplanar anchors the texture to the world, so a
      **rotating** cube turns through a stationary rock pattern and the relief
      swims. The capability is unchanged and still covered by
      `tests/gpu/triplanar_test.cpp` and `triplanar_parallax_test.cpp`; what
      changed is which of the two this sample shows. See "The sample moved from
      triplanar to UV-mapped POM" below*
- [x] **The scene is a hand-authored file** loaded through the scene loader — not
      constructed in C++. If any part cannot be expressed in the format, that is
      recorded rather than worked around in code
- [x] **The material is an asset file**, not built in code
- [x] Textures resolve **through the VFS** (D13), not by absolute path
- [x] The sample is a **gameplay module** across the real boundary — D12's
      lockstep and T0104's build id, exercised rather than assumed
- [x] **What the formats made awkward is written down**, with the ticket that
      should fix each thing. This is a deliverable, not a footnote

## Subtasks

- [x] 157.1 The sample skeleton — `samples/rockcube/`, plus one
      `add_subdirectory` line beside `samples/sandbox` at `CMakeLists.txt:450`.
      Confirm a second sample needs nothing else; if it does, that is finding one
- [x] 157.2 **The cube mesh — decide, do not default.** Two options and they are
      not equivalent: an authored glTF asset (smallest now), or a **primitive
      generator** (cube/plane/sphere) which the editor will want anyway the first
      time somebody wants *Add ▸ Cube*, and which every comparable engine ships.
      Record the choice and the reasoning
- [x] 157.3 **The hand-authored `.hpscene`** — cube, light, camera. The first
      non-test consumer of `loadSceneFromString`. Author it **by hand from
      `10-scene-file-format.md`**, not by saving one out of code — saving and
      reloading proves round-tripping, which is already tested; *writing* one
      proves the format is authorable, which is not
- [x] 157.4 The `.hpmat` — rock base colour, normal, ORM and height, with POM and
      triplanar enabled
- [x] 157.5 **Where sample content lives.** The rock set is in
      `test_assets/derived/`, whose name says it is for tests. Mount it, copy it,
      or give samples their own content root — decide, because "the sample reads
      the test assets" is the kind of coupling that is fine until it is not
- [x] 157.6 **Rotation, and verify the hook before designing around it.** What
      per-frame update a gameplay module actually gets is unconfirmed, and
      **T0062 (entity behaviours) is open** — so the answer may be less than
      hoped. If the module cannot cleanly move a transform per frame, **that is a
      finding for T0062**, recorded there with a two-way reference, not a
      workaround here
- [x] 157.7 **The awkwardness log** — everything the scene format, the material
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

---

### 157.1 — what a second sample costs

**One `add_subdirectory` line and its own `CMakeLists.txt`**, exactly as the
ticket guessed, plus one thing it did not: **content staging**, which has a trap
in it that is already written down elsewhere in this repository.

`build.zig` builds *named* targets and never `all`, so
`add_custom_target(... ALL ...)` never runs and
`add_dependencies(hp_rockcube_content hp_rockcube)` is backwards — nothing would
ever ask for the content. The dependency points the other way, and the staging
step's `OUTPUT` is a **stamp file** because `OUTPUT` accepts only a restricted
set of generator expressions and a directory copy has no single output to name.
Both of those are in `CLAUDE.md`'s traps list, and both were needed here.

### 157.2 — the mesh: an authored glTF asset, and the reasoning

**Decision: a committed `.gltf` + `.bin`, generated once by
`tools/make_cube_gltf.py`.** Not a primitive generator.

**What the survey found, because the answer was not obvious.** Diligent already
ships `CreateGeometryPrimitive` — cube and sphere, position/normal/texcoord,
subdivisions — in `DiligentCore/Common/interface/GeometryPrimitives.h`. That is
the *maths* half of a primitive generator, already vendored, and finding it is
exactly what `12-vendored-capabilities.md` exists to make happen.

**It is the other half that is not free.** `hp::MeshAsset` wraps
`Diligent::GLTF::Model`, and the only way to build one of those from raw vertex
data is `ModelBuilder::BuildModel`, which is **templated on a tinygltf-shaped
document type** — it wants something it can ask for nodes, meshes, primitives
and accessors. So a generator is not "call `CreateGeometryPrimitive` and store
it"; it needs an adapter model type, or a second path into `MeshAsset` that does
not go through glTF at all. That is a real design decision about the asset
layer, and it belongs to whichever ticket wants *Add ▸ Cube* — **T0032** — not
to a sample that needs one cube.

The second reason is the one the owner asked for directly: **an authored asset
is checkable.** The winding rule is written down in the generator, asserted
per-triangle inside it, and asserted again against the committed bytes by
`tests/fast/rockcube_mesh_test.cpp`. A generator's winding would be Diligent's
choice, verified by looking at a picture.

**A separate `.bin` rather than a base64 data URI**, which would have kept the
sample to one text file. An external buffer makes Diligent's loader call back
for a second file, and in this engine that callback reads through `hp::Vfs` —
so the sample exercises D13 for its geometry too. A data URI decodes inline and
never touches the mount tree.

### 157.5 — where sample content lives

**Decision: `samples/rockcube/content/`, staged into the build tree by CMake,
with the rock textures copied from `test_assets/derived` at build time.**

The three options and why this one:

| Option | Why not |
|---|---|
| Mount `test_assets/derived` at run time | The sample would read a source-tree path named *test*. It works on the machine that built it and cannot ship — which makes it a worse test of the authoring path than the thing it is imitating |
| Commit a second copy under `samples/` | ~1 MB of CC0 PNG duplicated in git, kept in step by hand, for files that are byte-identical |
| **Stage it, copy the textures at build time** | One declared dependency, in one `CMakeLists.txt`, that T0024 can repoint in a line. The sample reads a content root beside its binary, which is what a game does |

**What this does not solve, and it is worth saying plainly:** the coupling is
still there, it has just moved from run time to build time. The right answer is
a shared content root that both the suite and the samples mount, and that is
**T0024**'s project layout. If the rock set moves, one line here moves with it.

### 157.6 — the per-frame hook did not exist, and neither did phases 7 and 9

**Measured, not assumed.** `ModuleContext` carried `generation` and `name`.
`ModuleApi` had `onLoad` and `onUnload`. Nothing handed a module a `Scene`. A
gameplay module could register reflected types and **nothing else** — it could
not place an entity, load an asset, or move a transform.

That is **T0062's 62.11**, which that ticket already records as "~30 lines, and
it blocks every other subtask". It was landed here rather than worked around,
because the alternative was a sample that fakes gameplay from inside the editor
— which would have made this ticket prove nothing about the module boundary.
The two-way reference is on T0062, with what is done and what is left.

**The finding underneath it is the one nobody predicted.** Handing a module a
scene is not enough: **frame phases 7 and 9 were empty**, so a transform a
module moved never reached a world matrix. The entity sits perfectly still, the
inspector shows the value changing, and nothing anywhere reports a problem.
`Application` now propagates the scene it was given.

### 157.7 — the awkwardness log

Each item is a real cost paid while writing this sample, and each names the
ticket that should remove it.

**1. A scene can only reference an asset by GUID, so hand-authoring one means
hand-authoring an identity for every asset first.** → **T0024**

Five `.hpmeta` files had to be written, each with an invented 16-hex-digit GUID,
*before* a single line of the scene could be typed — because `importAsset` mints
an identity only when it loads a file, and the scene has to name the identity
before that. This is the largest single friction in the whole exercise and it is
entirely a project-layout problem: an asset database that can answer "the GUID
for `materials/rock.hpmat`" removes it. Nothing in the scene format needs to
change.

**2. A rotation must be written as a quaternion, and there is no other
spelling.** → **T0035** for the authoring surface

Two of the three entities needed one. Neither could be typed by a person: the
sun's was lifted from the rake the gpu suite *measured*
(`Rot(Y, pi - 0.7) * Rot(X, -0.7)`), and the camera's was derived from
`atan(1.5 / 5)` with the pitch sign checked against
`Rot(X, a) -> (0, -sin a, cos a)` rather than guessed. A hand-authored scene is
readable everywhere except in exactly the field most likely to be wrong, and a
sign error there is invisible in the file and obvious on screen.

This is *not* a request to store Euler angles — `Transform::rotation` being a
quaternion is right. It is a note that the format has no alternate input
spelling and the editor does not exist yet, so today the only way to author a
rotation is to compute it elsewhere.

**3. A module-owned component type outliving its module is a use-after-free.**
→ **T0062**

Reproduced as a SIGSEGV in `~basic_registry` while writing
`tests/integration/rockcube_module_test.cpp`. `forgetType` removes the
*reflection*; the `entt` storage pool is a separate thing that nothing removes,
and it holds operations living in the unmapped image. The editor survives only
because `Application::run` calls `layers_.clear()` — destroying the scene —
before `modules_` is destroyed, an ordering chosen for GPU-resource lifetime
(T0025) that happens to be what gameplay needs. Full write-up is on T0062.

**4. A module cannot find its own content.** → **T0024**, and **T0043** for the
shipped case

`hp::executableDirectory()` is the *host's* directory, not the module's, so a
module has to guess a relative layout — `<exe>/content`, `<exe>/../content`,
`<exe>/../../samples/<name>/content` — exactly as the two apps already guess at
where their modules are. It works and it is three guesses too many.

**5. `dist` stages the module and not its content.** → **T0043**

The recursive `*.so`/`*.dll` glob picks `libhp_rockcube` up; nothing stages
`samples/rockcube/content`. In a `dist` layout the module therefore loads, finds
no content root, logs it and does nothing, and the editor falls back to its
demo quad. That is a graceful failure with an actionable message rather than a
crash, and staging sample content is the export pipeline's decision, not this
ticket's.

**6. The editor names the modules it hosts in a preprocessor macro.** → **T0024**

`HP_SANDBOX_MODULE_NAME` and now `HP_ROCKCUBE_MODULE_NAME`. A project should say
which modules an application loads. Until one exists there is nowhere else to
put it, and two names in a list is at least honest about that.

**7. Under `triplanar`, the normal map is ignored** (T0141.8), so `rock_normal.png`
is deliberately **not** named by `rock.hpmat`.

Recorded so that nobody "fixes" its absence. It is in the material format
document already; it is repeated here because a material file that names four of
five available maps looks like an oversight.

**8. Two `ModuleHost` instances cannot load the same module file in one process
on Windows.** → recorded in `copyPathFor`, no ticket

Each host stages a working copy named from its *own* counter, so both pick
`.hot1`; on Windows the second `copy_file` lands on a mapped file and fails with
`CopyFailed`. Measured under wine when a test used two hosts. Nothing needs two
hosts on one module today — the editor and the runtime are separate processes —
so it is written down beside the code rather than fixed.

### Verified 2026-08-06 — the evidence, and what is *not* verified

Host: a real Linux box with an **NVIDIA GeForce RTX 2080**. The build reported
`test (linux-x86_64, ...) natively` and `test (windows-x86_64, ...) under wine`,
which is the right answer on this machine and not a degraded one — and wine
forwarded Vulkan to the same GPU, so **both targets rendered on the RTX 2080**.

```text
zig build all                     EXIT: 0        (both targets)
zig build test -Dtest=all         EXIT: 0
  fast          312 cases | 312 passed | 214809 assertions   x2 targets
  integration    92 cases |  92 passed |    540 assertions   x2 targets
zig build test -Dtest=gpu         EXIT: 0
  gpu            31 cases |  31 passed |    902 assertions   x2 targets
zig build docs                    EXIT: 0        (regenerated; Application.md,
                                                  ModuleHost.md, index.md moved)
```

**The sample, rendered** (`tests/gpu/rockcube_sample_test.cpp`, identical on both
targets):

```text
device: NVIDIA GeForce RTX 2080
cube covers 16.9777% of the frame, luminance variation 11.8747
```

**Parallax is doing work in the sample's own material**, measured by rendering
`rock.hpmat` twice with only `heightScale` changed (0.05 as authored, then 0):

```text
mean absolute difference over the cube's pixels : 10.201 per channel
pixels changed by more than 2                   : 41465
```

**The faces wind outward**, proven twice and at two levels:

- `tests/fast/rockcube_mesh_test.cpp` reads the committed `cube.bin` and checks
  every one of the 12 triangles: `cross(v1 - v0, v2 - v0)` points away from the
  centre *and* agrees with the authored normal; the cube is exactly ±1 on every
  axis; each face carries one axis-aligned normal on all four corners;
- `tests/gpu/rockcube_sample_test.cpp` puts the camera **inside** the cube and
  asserts the frame is untouched clear colour (`covered == 0`). With the winding
  inverted, that view shows the interior — and the view from *outside*, which is
  the only one a person looks at, would be nearly indistinguishable.

**The editor was run and looked at**, bounded, twice: with the sample module
(3 entities, cube on screen) and with `libhp_rockcube.so` moved away, which logs
`no gameplay module found` and falls back to the throwaway quad. Both exited 0.

**Not verified, and worth saying plainly:**

- **`dist` has not been run with this in it.** The module will be staged by the
  recursive shared-library glob and its content will not, so a `dist` editor
  logs a missing content root and shows the fallback quad. Reasoned from
  `cmake/dist.cmake`, **not measured**, and it is T0043's to fix.
- **The rotation was never watched frame by frame in the editor** — a still
  frame cannot show it. What is asserted instead is exact: two 0.25 s steps at
  90 deg/s produce (0, sin 22.5deg, 0, cos 22.5deg) to 1e-3, still unit length,
  and `propagateTransforms` reports work done
  (`tests/integration/rockcube_module_test.cpp`).
- **No second GPU vendor.** Every number here is one RTX 2080.
- **Hot reload of `hp_rockcube` was not exercised.** It re-runs `onLoad`, which
  reloads the scene from the file — so the cube's accumulated rotation resets.
  Correct enough for a sample and untested.

### The sample moved from triplanar to UV-mapped POM, and three bugs came out of it

**All three were found by eye, by the owner, in the editor** — which is the
claim this ticket's "Why" makes for its own existence, so they belong here in
full rather than as a footnote.

**1. Triplanar was the wrong technique for a rotating object.** It projects from
world space, so the cube turns *through* a stationary pattern instead of
carrying it. Inherent, not a defect — and the reason the mesh gained a 0..1 UV
island per face and the material dropped `triplanar`. It also unlocked the
**normal map**, which a triplanar material ignores outright (T0141.8), and which
is most of why the surface now reads as a surface.

**2. The per-face tangent bases were inconsistent, and every assertion passed.**
The first UV layout picked each face's tangent and bitangent on the only
criterion that seemed to matter — `cross(t, b) == n`, so the winding came out
right — and let each face pick freely among the pairs satisfying it. Result:
`u` ran along world X on the ±Z faces and along world **Y** on the ±X faces, so
the rock appeared rotated 90° between them; and `v` pointed world-**up** on +Z
and world-**down** on −Z, so the normal map tilted bumps *toward* the light on
one face and *away* on the opposite one.

Measured over a yaw sweep, on geometry that presents an identical square to the
camera at every step: mean frame luminance **26 at one yaw and 140 at the yaw
180° from it**. It reads as one face being the only one that reacts to light.

*Nothing could have caught it.* The file was valid, the winding correct, the UV
islands 0..1, the silhouette right, and `cross(dP/du, dP/dv) == n` — the
handedness invariant — held on every face. **Handedness and orientation are
different properties**, and only the second was wrong. The fix pins the
bitangent to world-down and derives the tangent as `b × n`; the guard is
`tests/gpu/rockcube_sample_test.cpp`'s yaw sweep, which requires the four means
within 25% of each other and fails at 5.4× on the old mesh.

**3. `Light::intensity` is unitless with no reference scale, and 10 is almost
nothing.** → **T0096**

The scene ran at intensity 3.5, then 6, then 10 and still looked unlit. The
engine's own debug views settled it: rendering `BaseColor` against the shaded
frame showed the **brightest face barely reaching its own unlit albedo** (37 vs
33) and two of three sitting below it. Light that never lifts a surface above
its albedo does not read as light. The scene now runs a key at **60** and a fill
at **10**.

`hp/Light.hpp` documents the field as deliberately unitless — *"a physical unit
would be precision about a number nothing yet interprets physically; revisit
with T0096"* — which is a defensible decision that leaves nobody any way to know
whether 10 is bright or dark. That is the finding for T0096: whatever it does
about exposure and tone mapping should come with a stated reference scale.

**A fourth thing, which was my own error and is recorded because it nearly
became a false bug report.** Probing the light convention with `Rot(X, +90°)`
maps the light's forward `(0, 0, -1)` to `(0, +1, 0)` — straight **up**, not
down — and produced a dim top face that looked like proof the engine had the
sign inverted. The correct probe (`Rot(X, -90°)`) lights the top decisively:
57.5 against 34.8 and 12.6. **The convention is right and the placement was
wrong**: with the cube resting at 30° yaw its large visible face points left of
the view axis, so a key from the right lit a narrow side face brilliantly and
left the big one at `N·L = 0.26`, which looks exactly like backlighting. The key
now comes from upper-front-left: front 0.74, top 0.62, right 0.26.

**There is no ambient or environment lighting (T0087), and it shows.** With one
lamp and a normal map, a face angled away from it renders **solid black** —
measured: normal map on, one whole face black; normal map off, zero fully-black
pixels. The scene carries a fill lamp as an explicit stand-in, documented as one
in the file. This is the argument for T0087 having a consumer.

### What went right, and is worth keeping

- **The scene format's leniency behaved exactly as documented.** Loaded without
  the gameplay module, `RockCubeSpin` is preserved as an unknown component with
  a warning naming it, and the load succeeds — asserted in
  `tests/gpu/rockcube_sample_test.cpp` (`unknownComponents == 1`). Loaded *with*
  the module, it materialises into a real component whose C++ definition lives
  in a shared library the engine was not built against
  (`unknownComponents == 0`, asserted in the integration bucket). That is D23's
  claim, proven across a `dlopen` boundary rather than within one image.
- **Two mounts merging into one directory** is what lets the gpu test read the
  sample's `.hpmeta` files from `samples/` and its PNGs from `test_assets/` as
  one tree. Same mechanism as a patch or a DLC pack (D13), for free.
- **A three-line material is genuinely enough.** `rock.hpmat` sets six
  parameters and leaves everything else at its default, including `metallic` and
  `roughness` at 1 so the ORM texture decides them.
