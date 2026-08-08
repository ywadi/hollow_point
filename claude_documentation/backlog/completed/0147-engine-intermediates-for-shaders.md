# T0147 — Engine intermediates: scene depth, scene colour, and game-fed inputs for shaders

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 458 |
| **Created** | 2026-08-06 |
| **Completed** | 2026-08-07 |
| **Refs** | **T0161** ([../completed/0161-game-resource-model.md](../completed/0161-game-resource-model.md)) / **D35** — the other side of the two-namespace line is drawn: game-*declared* resources are reflected into the per-module signature, so the engine-*fed* screen resources this ticket adds belong in the engine's base signature under engine names, and `buildModuleSignatureDesc`'s subtraction will automatically leave them alone once they are declared there; [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) — its Done-when promises intermediates and no subtask delivers the sampled ones; [0093-visibility-and-fog-of-war.md](../open/0093-visibility-and-fog-of-war.md) — the visibility field arrives with it, through this mechanism; [0094-gameplay-extensible-rendering.md](../inprogress/0094-gameplay-extensible-rendering.md) — 94.4/94.5 are the game-fed half of this; [../completed/0046-frame-render-targets.md](../completed/0046-frame-render-targets.md) — every target already carries `BIND_SHADER_RESOURCE`, and design-gaps item 8 flagged the scene-colour seam into it; [0096-hdr-pipeline-and-tonemapping.md](0096-hdr-pipeline-and-tonemapping.md) — sidedness rules; [0106-vfx-sprites-and-flipbooks.md](0106-vfx-sprites-and-flipbooks.md) — soft particles are this ticket's depth read; [0089-fog-and-atmospherics.md](0089-fog-and-atmospherics.md) — fog is another consumer |

## Why

**T0141's Done-when promised it and nothing delivered it:** *"Custom shaders
receive engine intermediates — visibility (T0093), screen position, depth,
world position — not just a finished colour."* Audited 2026-08-06:
`HpSurfaceInput` carries `ScreenPos` (the raw `SV_POSITION`, so the fragment's
own depth) and `WorldPos` — the *computed-in-shader* intermediates. What no
shader can reach is anything **sampled from the frame**: the scene depth
*texture* (what is behind this transparent fragment), the scene *colour*
(refraction, distortion, frosted glass), T0093's visibility, or any texture a
game's own pass produced (T0094).

**T0141 closed 2026-08-06 with that Done-when at `[~]`**, the interpolated half
shipped and the sampled half named as this ticket's — so nothing here waits on
it any more, and nothing there will tick when this lands. **The evidence goes
in this ticket**, and its closure is what makes the promise true; a closed
T0141 will not be edited again.

The consumers are already queued, which is why this is one mechanism and not
four retrofits: T0106.5's soft particles fade against sampled scene depth;
T0089's fog wants depth; the design-gap survey's item 8 records screen
distortion needing scene colour during transparents "exactly as soft particles
need depth"; T0093's whole architectural constraint is a raw per-pixel factor
reaching material shaders; T0094.5 binds a gameplay texture as a material
parameter.

**Godot's shape here (4.7.1, surveyed 2026-08-06)** is the hint-uniform:
`hint_screen_texture`, `hint_depth_texture`, `hint_normal_roughness_texture` —
the first two available on every renderer, the third Forward+-only. That is
the bar: a shader author *declares* the need, the engine supplies the resource
and its validity rules.

## Done when

- [x] A surface or lighting-stage shader can sample **scene depth** during the
      transparent pass, and the worked example is a depth-fade (the soft
      particle read, proven before T0106 needs it)
- [x] A shader can sample **scene colour** during the transparent pass — the
      snapshot point in the frame is decided and documented in
      `08-frame-anatomy.md`'s table (it is a new step in the 10.x sequence) —
      and the worked example is a refraction material
- [x] The **game-fed input** mechanism exists with T0094: a texture a game
      layer produced is reachable from a material shader by declaration, and
      the fog-of-war dim in T0093's scenario is expressible with it
- [x] Every intermediate documents **when it is valid** — which passes may
      read it, what it contains before its snapshot, and what an opaque-pass
      read of scene colour does (fails loudly, not garbage)
- [x] D27's arrival rule is honoured and restated: **no `Visibility` field
      until T0093's mechanism exists**, no normal-roughness promise unless
      something produces it — each field names its owning ticket
- [x] What is deliberately **not** offered is written down: this engine is
      forward-only (D24), so a G-buffer normal-roughness read either gets a
      cheap forward answer or an honest rejection — decided, not silent

## Subtasks

- [x] 147.1 Depth SRV plumbing: which target, when it is complete, how the
      surface stage declares the read; placement recorded in frame anatomy
- [x] 147.2 Scene-colour snapshot: where in 10.x it is copied (after opaque +
      sky, before transparents is the obvious point — decide against T0096's
      HDR ordering), full or half resolution decided by measurement
- [x] 147.3 The contract fields and their validity docs — the
      `HpMaterial.slang` arrival table grows rows with owners, exactly as it
      already does for `ShadowFactor`/`Visibility`
- [x] 147.4 Game-fed texture slots, designed with T0094.5 rather than beside
      it — one mechanism, referenced both ways
- [x] 147.5 Worked examples with pixel assertions: depth-fade and refraction
      in the gpu suite
- [x] 147.6 The normal-roughness disposition (offer a forward-friendly
      answer, or reject with the reasoning)

## Notes / findings

### The design, decided before any of it was built (2026-08-07)

**The frame gained a pass split, and that is the whole shape of this ticket.**
`SceneRenderer::render` submitted the draw list in one walk, in list order, so
a blended surface could be drawn *before* the opaque geometry behind it — and
there was no point in the frame at which "the opaque image" existed. Both
problems have one answer:

| Step | What | Why here |
|---|---|---|
| 10.9a | **Opaque pass** — every primitive whose alpha mode is `Opaque` or `Mask` | |
| 10.9b | **Scene snapshot** — copy the bound colour and depth into the caller's snapshot targets | the only moment the opaque image is complete and no blended surface has touched it |
| 10.9c | **Blend pass** — every primitive whose alpha mode is `Blend` | reads 10.9b |

The split is a correctness fix on its own (transparents now draw after opaques,
which they did not) and it is what makes a snapshot point *nameable*.

**Copy rather than alias, and this was the decision most at risk of "works on
one driver".** The alternative was to bind the depth buffer through a
`TEXTURE_VIEW_READ_ONLY_DEPTH_STENCIL` view during the blend pass and sample
the live attachment — `VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL` permits
exactly that. It was rejected: it needs the *caller* to bind a different DSV for
one of two passes, it makes the depth a shader reads depend on how many blended
surfaces have already drawn into it, and it has no counterpart for colour — so
the engine would have carried two mechanisms with two validity rules. One copy
mechanism, one rule, every backend.

**Two engine names in the base signature, no new samplers.** `g_SceneColour`
and `g_SceneDepth` are `MUTABLE` texture SRVs on the base signature (the ticket's
own Refs argued this, and `buildModuleSignatureDesc`'s subtraction leaves them
alone for free). They are sampled through the **existing** palette
(`HpSamplerLinearClamp`, `HpSamplerPointClamp`), so the immutable-sampler count
is unchanged at 13 and only the sampled-image count moves, 6 → 8.

**The validity rule is enforced at pipeline build, not documented and hoped
for.** A module that names either resource in a pipeline whose alpha mode is not
`Blend` is refused **by name**: `SurfacePipeline::build` returns null, the
renderer substitutes the missing-material checkerboard, and one log line says
which module and why. That is the Done-when's "fails loudly, not garbage",
achieved without any per-pass rebinding — the alpha mode is already in the
`PSOKey`.

**The snapshot costs nothing when nothing wants it, and the demand is exact
rather than one frame late.** The opaque walk *scans* the primitives it skips:
it records whether the blend pass has any work at all and whether any of its
modules either names a screen resource or has never been compiled (unknown
counts as wanting). Only then is the copy issued. A scene with no blended
material never pays; a scene with blended materials that ignore the screen pays
nothing from the second frame and one copy on the first.

**Game-fed textures ride the module signature, by name.** A game layer calls
`setGameTexture("visibility", view)`; a module declares
`Texture2DArray visibility;` exactly as T0161 already allows, and the module SRB
binder resolves the name against the `.hpmat` first and the game feed second.
No new signature, no new declaration syntax — which is the point: T0161's
mechanism was already the right one, and what was missing was a *source* for the
bytes.

### Scene colour has a cost and a trap worth naming now

The snapshot is a full copy of the HDR target once per frame (when any
material declares the need — it should cost nothing when none does, which is a
pipeline-flag question for the material system). The trap is recursion:
a refractive surface sampling scene colour does not see *other* transparents
drawn after it. That is the standard limitation every engine ships (Godot's
screen texture has it too); it is documented, not solved.

### The sidedness rule applies

T0096: "if a pass cannot say which side of the tonemap it is on, that is a
design smell." Scene colour sampled by materials is **pre-tonemap HDR** by
construction here; the docs in 147.3 must say so, because a shader author
porting a Godot LDR trick will otherwise be surprised by values above 1.

---

## Evidence (2026-08-07)

Every number below is from this machine — **NVIDIA GeForce RTX 2080**, the
Linux target running **natively** and the Windows target **under wine**, both
reaching the same GPU. `zig build all`, `zig build test -Dtest=all`,
`zig build test -Dtest=gpu` and `zig build docs` are clean on both targets.

**Suites: fast 321, integration 92, gpu 51 → 57.** Six new gpu cases, all in
`tests/gpu/screen_inputs_test.cpp` except the sample's, which is in
`rockcube_sample_test.cpp`.

### The byte-identical discipline held

**`no-height vs zero-scale: 0` on all three parallax baselines**, unchanged:

```
parallax_test.cpp:321          no-height vs zero-scale: 0; no-height vs displaced: 15.4693
triplanar_parallax_test.cpp:553 no-height vs zero-scale: 0; no-height vs displaced: 7.38278
triplanar_parallax_test.cpp:605 terrain variation 7.16354; no-height vs zero-scale: 0; ...
```

And the rock cube's own baselines, with the glass pane removed by the cases
that measure the cube: self-shadowing `covered: on 46723, off 46723`, sway
`rest covers 47393 px, peak covers 47224 px; gained 1177, lost 1346` — every
figure identical to what T0159 and T0146 recorded.

### Refraction is pixel-exact, which was not the design target

`screen_inputs_test.cpp`, against a render of the same scene without the pane:

```
wall alone at centre: (188, 187, 0); through the pane: (188, 187, 0)
wall alone at 3/4:    (225, 136, 0); pane displaced +0.25: (225, 136, 0)
```

The wall is a **horizontal ramp in screen space**, not a flat colour, so a
shader that ignored the displacement would pass the first line and fail the
second. The sRGB decode-on-sample and re-encode-on-write round trip cost
nothing measurable at these values.

That case also carries a second assertion for free: **its pane entity is created
before the wall**, so the draw list hands the blended surface over first. Before
the split that pane wrote depth at z = 3 and the wall at z = 6 then failed the
reverse-Z test outright — the wall was not mis-blended, it was *invisible*. Any
wall colour arriving through the pane is the split working.

### Depth fade, hand-checkable

```
depth fade over the wall: (188, 188, 188); over background: (255, 255, 255)
```

A pane at z = 5 over a wall at z = 6 with a 2 m range is linear 0.5, which the
sRGB target encodes as 188. The background is the cleared depth — the far plane
under reverse-Z — so it saturates. One frame, two places.

### The opaque read fails loudly

```
[error] render.pipeline: shader module 'materials/module0.slang' samples
g_SceneColour or g_SceneDepth, which only a material with alphaMode: Blend may
do -- the snapshot is taken after the opaque pass, so an opaque read would see
the previous frame. Set the material's alpha mode to Blend, or stop sampling them

opaque screen read: refusals 1, magenta 16384 px
```

One line, on the compile attempt, not per draw or per frame. The whole quad is
the missing-material checkerboard.

### The game-fed texture

```
no game feed:      (0, 255, 0)     — the module's declared texture reads white
game feed bound:   (255, 255, 0)   — the checkerboard's magenta texel arrived
```

Channels chosen so neither outcome is magenta, so the guard can police the frame
honestly. This is T0093's fog-of-war dim, expressible today: the module declares
`Texture2DArray visibility;` and a layer calls `setGameTexture("visibility", …)`.

### The snapshot's cost, and what "costs nothing" actually turned out to mean

**Demand, measured over two frames of three scenes:**

| the blend pass contains | copies |
|---|---|
| a **standard** material (no module) | **0**, ever |
| a module that does not read the screen | **1** — the speculative first frame |
| a module that reads it | **2** — one per frame |

A module's demand is a fact about its *compiled bytecode*, so it is unknown
until a pipeline exists, and unknown counts as wanting. That costs exactly one
speculative copy per module. The alternative — letting demand lag a frame —
costs nothing and renders the first frame of every refraction against an
uninitialised texture, which is the more expensive mistake.

**Bandwidth, at 1024×1024, median of five interleaved runs:**

```
3.41 ms per frame with, 3.72 ms without, difference -311 us
per-run spread: with 1.85 .. 6.26 ms, without 2.00 .. 4.94 ms
```

**The difference has no sign, and that is the finding.** 8 MB of copy is ~18 µs
of bandwidth on this card; the run-to-run spread of a frame ending in a readback
stall is a *millisecond*. Three consecutive runs of the same case measured
+26 µs, +1120 µs and −560 µs. So there is no assertion on the delta — any bound
tight enough to be interesting would be a flaky test — and 147.2's resolution
question is answered on the other side: **full resolution**, because the copy is
nowhere near expensive enough to justify a haloed depth fade and a soft
refraction.

Two things had to be fixed before that measurement meant anything, both worth
keeping: `view.render` records and returns, so timing a loop of them measures
CPU submission only (the case flushes per frame now); and running one variant's
repeats before the other's produced a **systematic 400 µs bias in favour of
whichever went first**, on every run — GPU clock or thermal drift over ten
seconds of load. Interleaving the variants cancels it, and the sign of the
difference stopped being constant the moment it did.

### The descriptor budget

Pinned off the renderer's own creation-time log line by
`custom_shader_material_test`:

| | sampled images | immutable samplers |
|---|---|---|
| T0160 | 10 | 11 |
| T0161 | 6 | 13 |
| **today** | **8** | **13** |

The two screen textures sample through the palette that already exists, so the
count T0143 is pressing on **did not move**.

### The sample

`samples/rockcube/content/shaders/glass.slang` + `materials/glass.hpmat` — a
pane of glass in front of the cube, and the first material in this repository
that reads the frame rather than a texture. It uses **both** intermediates:
`HpSceneColour` at a displaced coordinate for the refraction, `HpSceneViewDepth`
to fade where the cube presses against it. **It carries no texture at all**,
which is the observation worth keeping — the rock beside it needs four images.

No new mesh: a pane of glass is the cube scaled to 4 cm thick. The scene's `y`
is derived (the camera's view axis passes through 0.481 at z = 3.4), and
`contactRange` is set against the measured 0.3 m clearance to the cube's nearest
corner.

```
glass pane changes 5.75% of the frame; centre (70, 70, 31) against (53, 45, 22)
cube covers 18.079% of the frame, luminance variation 25.6373 (was 26.7341)
```

Coverage did not move at all: the pane sits inside the cube's silhouette, so it
changes what those pixels *are* without changing whether they are covered. The
committed-content case asserts the centre moved **and** that the corners are
identical to the bit — the second half is the one that would catch a blended
surface writing depth over the whole frame, or a snapshot taken at the wrong
point.

Two of its parameters were chosen **by looking at the rendered frame**, and the
reasoning is in the `.hpmat`: at 2–3% displacement the refracted rock sits
within a texel or two of the rock beside it and the pane reads as a slight
tint; at 6% the strata step visibly sideways. And a near-white tint left the
displacement as the only evidence the pane existed, which reads as a rendering
artefact rather than as glass.

### Diligent told us what to do, and we did it

```
[debug] render.diligent: Texture 'scene.colour' is currently bound as render
target and will be unset along with all other render targets and depth-stencil
buffer. ... To silence this message, explicitly unbind the texture with
SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE)
```

The renderer now unbinds explicitly, copies, and re-binds **exactly the two
views it was handed** plus the viewport. That is the documented contract for
reading a target you have been writing, not a cosmetic fix — and it makes the
re-bind afterwards obviously required rather than defensive. The remaining
occurrences of that line in the log are `FrameTargets::readback`, which
pre-dates this ticket.

---

## What was NOT done, and what could not be verified

**Recorded because an overstated ticket is worse than an open question.**

- **The `RenderStack` path is implemented and unexercised.**
  `SceneRenderLayer::onRenderLayer` fills a `SceneScreenInputs` from
  `pass.targets` when the stack's targets declare `kSceneColourSnapshotTarget`
  and `kSceneDepthSnapshotTarget`, and `setGameTexture` forwards. **No test
  declares that pair on a stack**, so every screen-input assertion here goes
  through `SceneView`. The code is a dozen lines and the two paths share
  `SceneRenderer::render`, but "shares the implementation" is not "was run".
  T0094 is where a gameplay-authored layer will exercise it first.
- **`TargetFormat::ColourHDR` snapshots are unverified.** `SceneView` defaults
  to `Colour` and every test uses it, so the snapshot has only ever copied an
  `RGBA8_UNORM_SRGB` target. The copy checks format equality before issuing, so
  a mismatch is refused rather than silently wrong — but T0096 should expect to
  be the first to run the HDR path.
- **Render scale (T0111) and `ScreenUV` have an interaction nobody has met.**
  `HpSurfaceInput::ScreenUV` divides `SV_POSITION` by the **render target's**
  size, read off the target itself, which is right today and stays right under a
  letterboxing viewport. Under a render-scale upscale the target and the
  presented image differ in size; that is still the correct divisor for sampling
  a target-sized snapshot, but nothing has exercised it because render scale is
  not implemented. T0111 is closed, so this note is here rather than on it.
- **The bandwidth cost is below this instrument's noise floor**, as above. The
  claim "the copy is cheap" rests on the arithmetic (8 MB / ~450 GB/s) plus the
  absence of a measurable difference, not on a positive measurement of the copy
  itself. A GPU-timestamp measurement would settle it; none was taken.
- **Only measured on one device.** T0161 took its number on the RTX 2080 *and*
  on llvmpipe; this ticket did not, because the number it produced was noise on
  the fast device and would be noisier on the slow one.
- **The editor was never opened.** The sample's appearance was verified by
  reading back the gpu suite's frame and inspecting it as an image, not by
  running `hp_editor` — a GUI on somebody's desktop is not something to leave
  running, and the readback is the same pixels.
- **No `Visibility` field was added, and no normal-roughness read.** Both are
  Done-when items and both are recorded as *decisions* above and in **D37**
  rather than as work. A game can build the fog-of-war dim today out of its own
  texture; what does not exist is the engine *computing* a visibility field,
  which is T0093's.
- **Ordering within the blend pass is still submission order.** The split makes
  transparents draw after opaques, which they did not; it does **not** sort them
  back to front, and T0045 owns that. A scene with two overlapping blended
  surfaces still renders them in draw-list order.
- **A refractive surface does not see other blended surfaces.** Documented, not
  solved — the standard limitation, recorded in D37 and in the contract.

## The one thing that surprised us

**The pass split was the work, and it was owed anyway.** The contract half of
this ticket is four helper functions, one `ScreenUV` field and two signature
resources — an afternoon. What took the time was that the frame had nowhere to
*put* a snapshot, because submission was one walk in draw-list order.

And that walk was already wrong. A blended surface submitted before the opaque
geometry behind it did not merely blend against the clear colour; it wrote depth
that then **rejected** the geometry it should have been in front of. The
refraction test's own scene reproduces it exactly — pane entity first, wall
second — and before this ticket the wall was invisible in that arrangement.
Nobody had noticed, because nothing in the engine's own content had two surfaces
at different alpha modes overlapping.

So the screen-space family was blocked on a binding, and the binding was blocked
on a bug in submission order that no ticket owned.
