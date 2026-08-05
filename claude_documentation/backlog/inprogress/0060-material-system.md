# T0060 — Material assets

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 450 |
| **Created** | 2026-08-03 |
| **Refs** | [0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md) — **must reconcile this ticket's material model with `PBR_Renderer`'s material attribs, or diverge deliberately and say so.** T0028 adopted DiligentFX's PBR renderer; read T0134.1 before designing materials. [../open/0141-custom-shader-materials.md](../open/0141-custom-shader-materials.md) — **took the renderer half of this ticket on 2026-08-05**; see "Re-cut" below. [../open/0059-prefabs.md](../open/0059-prefabs.md) 59.10 |
| **Scope** | **The material data model only.** Anything that decides how a surface is *shaded* is T0141's, including the standard material shader |

## Why

Diligent's `PBR_Renderer` shades glTF materials, but there is no material
**asset** — nothing GUID-addressable, editable in the inspector, or reusable
across meshes. Without one, the only way to change how something looks is to
re-export from the DCC tool.

**This ticket is the data model and nothing else.** How a surface is shaded — the
standard material shader included — is T0141's, for the reason recorded under
"Re-cut" below.

## Done when

- [x] Material is an asset with a GUID, serialized (T0022/T0020), and reflected
      so the inspector gets it for free
- [ ] Materials are assignable **per surface** on the mesh component, overriding
      what the model imported
- [ ] A material that is missing or will not load has a **defined, visible**
      fallback — the convention and the three-state table are this ticket's; the
      *rendering* of it is T0141's
- [ ] Nothing here decides how a surface is shaded, so **T0141 is free to change
      the shader without changing the asset**

## Subtasks

- [x] 60.1 Material asset: the parameter block, mapped onto
      `PBRMaterialShaderAttribs`, plus room for a shader reference T0141 fills
- [x] 60.6 Material assignment on the mesh component, overriding import defaults
- [ ] 60.10 The fallback **convention** for missing or unloadable assets — the
      three-state table, and a default-GUID slot never being treated as an error

### Moved to T0141 on 2026-08-05 — the re-cut

- 60.2 Standard PBR material mapping onto `PBR_Renderer` → **141.10**
- 60.11 The textured-render regression test T0134 could not write → **141.11**

## Re-cut 2026-08-05, at the owner's call — and the split reasoning that was wrong

The owner's words: *"i think we should of done 141 first then 0060 because
everything im asking for is just 141 and currently blocked, i think 0060 should
follow what 141 can do"*. They were right, and the reason is worth recording
because it was **my own stated justification that turned out to rest on a false
fact**.

The 2026-08-05 split said T0141 *"is larger than the first and **blocks
nothing**"*. That is false in two places found the same day:

- it blocks the **shape of 60.2** — "standard materials drive `PBR_Renderer`"
  bakes in the assumption that `RenderPBR.psh` is the material shader, which is
  precisely what T0141.0 is deciding;
- it blocks **T0086**, whose shadow sampling is built on that same shader.

So the split was made on a cost argument resting on a wrong fact, and the fault
line does not run between the two tickets — **it runs through this one**:

| Work | Depends on the shader decision? |
|---|---|
| 60.1 asset, GUID, serialization, reflection, UV channels | **No** — done, and safe |
| 60.6 per-slot `materials` on `MeshRenderer` | **No** — pure data model |
| 60.10 fallback *convention* | **No** — it is a policy |
| 60.2 standard material shader | **Yes** → 141.10 |
| 60.11 textured-render regression test | **Yes**, needs a working textured path → 141.11 |

**The test that settles it: what do T0045 and T0086 actually need from here?**
T0045 needs material *identity and blend mode* to sort and bucket on. T0086 needs
*cutout* materials. Both are 60.1 (done) plus 60.6, and neither needs one line of
shader. So this ticket keeps exactly what unblocks them and gives up the rest —
which is what "T0060 should follow what T0141 can do" means in practice.

### Descoped 2026-08-05 — moved to [../open/0141-custom-shader-materials.md](../open/0141-custom-shader-materials.md)

Not dropped, and not "later" in the vague sense: T0141 exists, is blocked by this
ticket, and carries the notes rather than leaving them here to rot.

- 60.3 Custom shader material with a declared parameter interface → **141.1**
- 60.4 Reflect shader parameters for the inspector → **141.2**
- 60.5 PSO management via `RenderStateCache` / `BytecodeCache` → **141.3**
- 60.7 Error *shader* on compile failure → **141.4** (a fallback *material* for a
  missing asset stays here as 60.10; they are different failures)
- 60.8 Shader hot reload → **141.5**

**Why.** This ticket was two tickets under one number: a material *asset*, which
is a data-model gap that blocks T0045 (whose render queues and sort keys are
material properties) and T0086 (which needs cutout materials for alpha-tested
shadow casters) — and a *shader system*, which is larger and blocks nothing.
Keeping them together meant culling and shadows waiting behind a shader compile
cache, which is the wrong dependency to accept.

- [ ] 60.9 Sort by material/PSO in the render queue → **T0045 owns this**; it is
      listed in that ticket's Done-when already. What this ticket owes T0045 is
      the material *identity and blend mode* to sort and bucket on

## Decided 2026-08-05, with the owner, before writing code

### Per-surface material slots, like Godot

`MeshRenderer` carries a **vector** of material GUIDs, one per surface, not a
single override:

```cpp
struct MeshRenderer {
    Guid mesh;
    std::vector<Guid> materials;   // per surface; a default entry = use the import
    LayerMask layers;
};
```

```yaml
MeshRenderer:
  mesh: 4f8a12c0d3e5b678
  materials: [91aa2b3c4d5e6f70, 0000000000000000, c3d4e5f60718293a]
```

**Why a vector rather than the single `material` field that exists today.** A
glTF with five materials is the normal case, not the exotic one — a character
whose body, eyes and hair differ is one mesh with three surfaces. A single
override can only replace *all* of them or none, which is wrong for most real
assets, and discovering that after materials serialize means changing the
component, the schema and every file written with it.

The renderer already carries `primitive.MaterialId` per draw
(`SceneRenderer.cpp`), so the index the slot vector needs is the index the model
already has. A default entry means "use what the model imported", which keeps an
untouched import writing an **empty** vector rather than a list of zeros.

Rejected: slots on the `MeshAsset` with a separate override component. Cheaper
per entity when a thousand objects share a model, and closest to how glTF stores
it — but changing one object's material then means editing shared data or adding
a second component, and that is a worse authoring experience than a vector that
is usually empty.

### A missing material renders as the checkerboard, and that is not the same as unassigned

**Three states, and conflating the first two would make every unassigned mesh
look broken:**

| State | Meaning | What renders |
|---|---|---|
| Slot is a **default GUID** | Nothing assigned. Legitimate and common | The model's imported material |
| Slot names an asset that **is not in the pool or failed to load** | An error | **The missing-material pattern** |
| A custom shader **fails to compile** | A different error, same appearance | **The same pattern**, via T0141 |

The pattern is **magenta and black checks, reusing `makePlaceholderTexture`**
(T0023.6) rather than inventing a second convention — that function exists for
exactly this reason and its comment already argues the case: *"a missing texture
that renders as white or as nothing is a bug someone ships; one that renders as
loud checks is a bug someone fixes before lunch."*

**T0141 renders the same pattern for a shader that will not compile**, decided
with the owner on 2026-08-05. One visual convention across every "something is
wrong here" — the *log* says which, not the pixels. So whatever this ticket
builds to produce the fallback must be reachable from there rather than being
private to the material path.

Two properties it must have, and the second is the one that gets missed:

- **Unlit**, or emissive enough that lighting cannot dim it. A magenta surface
  standing in shadow reads as plausible art; an unlit one cannot.
- **Visible, never invisible.** T0023 already established that a failed load
  yields a valid GUID so a scene referencing a broken asset resolves to a
  placeholder rather than silently detaching every entity. The same rule here:
  the mesh still draws, loudly wrong, rather than disappearing — a missing object
  is a much harder bug to find than an ugly one.

## Notes / findings

### 60.1 landed 2026-08-05 — the asset, and three things found on the way

`hp::Material` is plain reflected data in `engine/include/hp/Material.hpp`, with
`AssetKind::Material`, a `.hpmat` extension and `AssetTraits<Material>`. No
per-field code: save and load walk the reflected properties, so adding a
parameter is one line and no format change. Format documented in
[../../documentation/11-material-format.md](../../documentation/11-material-format.md).

**Measured:** 286 fast + 89 integration green on both targets (was 268 fast);
`zig build docs` clean.

**A material owns no GPU resources**, deliberately — the textures it names are
separate assets — which is what puts the whole of 60.1 in the fast bucket.

### 60.6 landed 2026-08-05 — per-surface slots, and a harness bug that invalidated Windows results

`MeshRenderer::material` (one `Guid`) is now `materials` (`std::vector<Guid>`),
through `DrawItem`, `DrawSubmission.cpp` and the scene schema. Empty means every
surface uses what the model imported, which is what an untouched import writes.

Two things had to be built underneath it, both of which were **gaps nothing had
exercised** because no serialized type had a sequence property before this one:

- **The YAML sequence shape.** A sequence of leaves was written as a list of
  single-key maps — `- 0: <guid>` — because the leaf writer could only `set` a
  key. Fixed by giving the writer a *destination* (`LeafSink`) rather than
  giving the file a second shape, so there is still one type list. Now
  `materials: [a, b, c]`.
- **The binary path had no sequence support at all.** `cookProperties` simply
  returned false for a type with a sequence property, so the whole component
  failed to cook — surfacing as a cooked scene that silently differed from the
  YAML beside it rather than as an error where the omission was. The YAML path
  had handled sequences since T0020; the two had drifted exactly the way
  `Serialize.cpp`'s own comments warn about, and only a type exercising both
  caught it. The uncook is bounded by the remaining byte count **before** any
  resize, so a corrupt length cannot become a multi-gigabyte allocation.

#### Finding 5: the Windows suite was running a stale DLL, and had been

**This is the important one, because it means Windows test results could not be
trusted.** Found while chasing what looked like an `entt` platform difference
and was not.

`add_custom_command(TARGET … POST_BUILD)` fires only when the target is
*rebuilt*. A Windows `.exe` links against the **import library**, which does not
change when an engine `.cpp` is recompiled without altering the exported symbol
set — so the exe is up to date, nothing relinks, the DLL copy never runs, and
the suite executes against the **previous** `libhp_engine.dll` beside it.

Seen in both directions in one session: a **green** Windows suite while the same
source **segfaulted** on Linux (the enum work, 60.1), and **two Windows failures
for a bug already fixed** (this ticket), which sent the investigation after a
phantom `as_sequence_container` platform difference that did not exist.

Fixed with a stamped copy rule the binary depends on, in `tests/CMakeLists.txt`
and both `apps/*/CMakeLists.txt`. **Two earlier attempts configured cleanly and
did nothing**, which is the part worth remembering:

- `add_custom_target(… ALL …)` never runs, because `build.zig` builds *named*
  targets (`cmake --build --target hp_tests_fast`) so the fast suite does not
  drag in ~1100 engine targets. Nothing asks for `all`.
- `add_dependencies(copy_step the_exe)` is backwards; it has to be
  `add_dependencies(the_exe copy_step)`.
- `$<TARGET_FILE_NAME:…>` is not in the restricted set `OUTPUT` allows, and
  fails with the misleading `No target "hp_engine"`. Hence the stamp file.

**Proved by content, not by timestamps**, and that matters: the first proof
appended a comment to a `.cpp`, which produces a byte-identical library, so
`copy_if_different` correctly skipped it and the test reported a false failure.
The real proof inserts a function, rebuilds only `hp_tests_fast`, confirms the
Windows exe did **not** relink, and `cmp`s the library beside the binary against
the freshly built one.

Now documented in `CLAUDE.md`'s traps and in `03-build-harness.md`, because it
was written down nowhere and is not guessable.

#### Finding 1: the texture lineup is five slots, and displacement does not exist

**Height mapping, parallax occlusion and triplanar are now 141.7/141.8/141.9**,
referenced both ways, with the reason recorded there: they need a hook *before*
texture sampling and `PBR_Renderer` has none — `GetPSMainSource` only reaches
the output struct and a footer. They are not "missing from materials", they are
shader-stage work that no material parameter can express. T0141 also now carries
the finding that DiligentFX's *lighting* is a reusable public library
(`PBR_Shading.fxh`) even though its *material shader* is not hookable, which is
what makes owning the surface stage cheap rather than a PBR rewrite.

Diligent defines **17** texture attributes; this carries the five the renderer
binds (base colour, metallic-roughness, normal, occlusion, emissive). The other
twelve are extended materials (clearcoat, sheen, anisotropy, iridescence,
transmission, thickness) that D24 keeps off, plus the two legacy spec-gloss
slots.

Two things worth knowing before anyone expects a Godot-sized parameter list:

- **Metallic and roughness are one texture**, glTF's packing — roughness in
  green, metallic in blue. A DCC tool exporting them separately needs them packed
  at import (T0023), not two slots here.
- **There is no displacement or height slot, and `PBR_Renderer` has no path for
  one** — no parallax-occlusion mapping, no tessellation. A `displacementTexture`
  field would be a field nothing reads, which is what `Camera::cullingMask` spent
  three tickets being. **Height-mapped surfaces are shader work and belong to
  T0141**, which is now referenced from there.

#### Finding 2: enums serialised as integers, and hand-authored ones silently did not load

Not a material problem, found because `alphaMode` is exactly the field a person
hand-edits. `10-scene-file-format.md` has claimed since it was written that enums
are written **by name**; the code wrote integers, and `readLeaf` parsed *only* an
integer. So a hand-authored `Light: {type: Spot}` — the whole point of T0139 —
failed to read, hit the lenient rule that leaves an unreadable field alone, and
produced a **directional light with no warning**.

Fixed generically rather than per type: `TypeBuilder::value` had existed since
T0053 for exactly this and nothing used it. An enum earns names by being
reflected; one that is not falls back to its integer, so nothing regressed by
omission. Numbers are still *read*, which makes this a representation change
rather than a schema break — no `version` bump.

**The test that should have caught it was there and could not.** The authoring
fixture used `type: Directional`, and Directional is the default, so "read
correctly" and "never read" produce the same result. The new cases use `Spot` and
`Point` for that reason.

#### Finding 4: a glb's node tree renders, and is not addressable

Raised by the owner while this was in flight, and checked rather than assumed. A
`.glb` containing a tree of objects imports as **one** `MeshAsset` under one
GUID, and one entity's `MeshRenderer` draws the whole thing —
`SceneRenderer::drawModel` walks `model.Scenes[0].LinearNodes` and Diligent
computes the node transforms internally. Nothing creates an entity per node;
`AssetImport.cpp` never touches `Hierarchy`.

So the hierarchy is honoured for *rendering* and is not addressable: a sub-object
cannot be moved, hidden, parented to or given a component.

**That belongs to T0059, not here** — importing a tree as objects is what a
prefab is, and building it in the importer would produce a second, worse override
model. Filed as **59.10** with the three constraints this ticket imposes on it,
and T0059 now references this ticket back.

#### Finding 3: `meta_any::allow_cast<T>()` returns `bool` on a mutable any

**This one cost two rounds and crashes only in release.** entt has two overloads:

```cpp
meta_any allow_cast<T>() const;   // converts, returns the result
bool     allow_cast<T>();         // converts IN PLACE, returns whether it could
```

So `entt::meta_any n = someAny.allow_cast<std::int64_t>();` compiles on a
non-const any and builds an any holding **`true`** — a perfectly valid `meta_any`,
so every check downstream passes. `n.cast<std::int64_t>()` then dereferences
null: an assert in a debug build, a **SIGSEGV** in release. The same trap applies
to `allow_cast(const meta_type&)`, where it produced a "valid" converted value
that matched no enumerator and silently dropped the field through the cook path.

Isolated with a 30-line standalone program against the vendored entt rather than
by rebuilding the engine — entt is header-only, and that is the fastest way to
settle a question about it. The working shape is: construct, convert in place,
check the bool.

### Inherited from T0134 / D24 (2026-08-05) — materials map onto `PBRMaterialShaderAttribs`

**Decided, not left open**: `PBR_Renderer`'s material attribs are the engine's
material vocabulary, and a material asset maps onto them rather than defining a
parallel structure. Read [../completed/0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md)
before designing the asset — the survey is done, and redoing it is the waste this
ticket's Refs exist to prevent.

What that gives you for free: `BaseColorFactor`, `EmissiveFactor`, `NormalScale`,
`SpecularFactor`, `Workflow`, `AlphaMode`, `AlphaMaskCutoff`, `MetallicFactor`,
plus 17 texture slots addressed by `TEXTURE_ATTRIB_ID_*`.

Three obligations this ticket picks up:

- **`GetMaterialPSOFlags` is currently inlined and constant-folded** in
  `SceneRenderer.cpp`, because with every optional feature off it collapses to
  `USE_COLOR_MAP | USE_NORMAL_MAP | USE_PHYS_DESC_MAP`. It carries a
  `static_assert` guard. **The moment this ticket enables `EnableEmissive` or any
  extended-material setting, that must go back to consulting the material** or
  materials will silently render with the wrong feature set.
- **Extended materials are off by design** — clearcoat, sheen, anisotropy,
  iridescence, transmission, volume. Each widens the PSO permutation space and
  the material attribs buffer *whether or not a material uses it*, so turning one
  on is this ticket's decision to argue, per D24.
- **Texture colour-space conversion is unresolved and inherited from T0028.**
  `GetPBRTextureSRV` is not public, so textures bind through the model's own
  views with no conversion. Untextured materials are unaffected, which is exactly
  why no test has caught it. Sits next to T0097's sRGB work.

**The regression test T0134 could not write belongs here.** T0134 fixed an
unwritten `PBRFrameAttribs::Renderer` whose observable symptom — a garbage
`MipBias` fed into every texture sample — needs a *textured* material to see. A
textured mesh rendered with its pixels asserted is the guard, and there is no
texture path in a test yet.

**Custom shaders must be able to reach engine intermediates, not just material
parameters.** T0093 (vision-based visibility) needs a per-pixel visibility factor
inside the material shader to dim, hide or dither. If shading is a sealed pipeline
that consumes lights and emits pixels, that capability has to be bolted on as a
post-process hack later. Design the custom-shader interface to expose documented
inputs — visibility, screen position, depth, world position — from the start.


**`RenderStateCache.hpp` and `BytecodeCache.h` already exist in
`Graphics/GraphicsTools`** and solve shader compile hitching and startup cost. Use
them rather than building a cache — this is a significant piece of work Diligent
has already done.

**Shader parameter reflection is separate from C++ reflection (T0053).** Getting
a custom shader's uniforms into the inspector means reflecting the *shader*.
Diligent's shader resource querying can provide this — check what
`IShader::GetResourceDesc` and the shader resource variables expose before
writing a parser.

**Variants are the thing that grows without limit.** Every optional feature
doubles the permutation count. Decide early whether variants are enumerated
explicitly or generated on demand and cached, and prefer the smallest scheme that
works — this is where material systems become unmaintainable.

The error material matters more than it sounds: a shader failure that renders
black is indistinguishable from an unlit object, and costs hours.

### Architecture review (2026-08-03) — two gaps in this ticket

**1. The custom-shader *language* is an undecided decision hiding in 60.3.**
Custom materials must run on both Vulkan and OpenGL (D2 — GL is the only
fallback on Windows). Diligent's portable path is **HLSL** (compiled via
glslang for Vulkan, converted for GL via its HLSL2GLSL converter, with
documented limitations); GLSL written directly does not portably reach both
backends through the same pipeline. Whatever is chosen becomes the language
every custom material is written in, forever — decide it explicitly at the
start of this ticket and record it, including which HLSL feature subset is
safe on the GL path.

**2. Skinning is a missing variant axis.** The variant discussion covers
optional features but not the one variant the engine is guaranteed to need:
skinned vs static vertex input (T0041/T0049). The standard PBR path gets this
from `PBR_Renderer` (verified: joints buffer support exists); custom-shader
materials need the skinned variant defined here, or skinned characters will be
limited to standard materials by accident rather than by decision.

Also note T0096 (HDR/tonemapping) now owns where material output lands —
custom shaders write linear HDR and must not tonemap themselves.


### Architecture amendment (2026-08-03) — particles need a material path this ticket does not describe

This ticket never mentions particles, blending, additive, or unlit — it is
implicitly about opaque, lit surfaces. VFX need a material path with different
requirements, and deciding now whether they share the material *system* or get
their own is cheaper than retrofitting either:

- **Blend mode is material state** — additive, alpha, and probably
  premultiplied alpha (T0106.4). Opaque materials have no such concept.
- **Usually unlit.** Fire and magic are emissive and must not be shaded by scene
  lights; smoke arguably should be. T0106.7 owns the fork, but "can a material
  opt out of lighting entirely" is a question about *this* system's shape.
- **Soft particles** need the material to sample scene depth, which is a
  resource an opaque material never binds (T0046).
- **Per-particle input.** A particle material is fed colour, opacity and
  flipbook frame *per instance* from the simulation buffer, not from uniform
  material parameters. That is a different data path from a mesh material.

The likely answer is that VFX materials are a distinct material *domain* sharing
the asset and shader-authoring machinery rather than a separate system — the
same way engines distinguish surface, decal and post-process domains. Decals
(T0108) are a third such domain and land in the same conversation, so it is
worth having once rather than three times.
