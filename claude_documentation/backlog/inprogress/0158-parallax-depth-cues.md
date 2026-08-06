# T0158 — Making parallax read as relief: the reference plane and self-shadowing

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 464 |
| **Created** | 2026-08-06 |
| **Blocked by** | nothing — both halves sit inside the surface stage the engine already owns (**D26**) |
| **Refs** | [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) 141.7 — the march this extends; [../completed/0156-parallax-under-triplanar.md](../completed/0156-parallax-under-triplanar.md) — the per-projection march, and **156.6's evaluation against silhouette POM**, which is what makes this the remaining move; [../completed/0157-rock-cube-sample.md](../completed/0157-rock-cube-sample.md) — **the sample that exposed the gap by being looked at**, and the test bed; [0145-lighting-stage-own-the-light-loop.md](../open/0145-lighting-stage-own-the-light-loop.md) — **158.3 must be decided there**, see below; [0146-vertex-stage-hook.md](../completed/0146-vertex-stage-hook.md) 146.7 — tessellation, the *other* answer, and deliberately not this one; [0155-terrain-rendering.md](../open/0155-terrain-rendering.md) — the consumer that would justify the faster marches this ticket rejects; [0087-environment-lighting.md](../open/0087-environment-lighting.md), T0096 — findings land on both; **D24**, **D26**, **D27** ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)) |

## Why

**T0157 put a parallax-mapped rock cube on screen and the owner's first reaction
was that it looked like a glass cube with the texture inside it.** That reaction
is diagnostically precise, and both halves of it have a cause:

**1. Parallax occlusion mapping displaces inward only.** `HpParallaxMarch`
anchors height 1 at the polygon plane — *"White (height 1) is the surface, black
is `heightScale` UV units deep"* — so every texel sits at or below the face. The
polygon is the *ceiling* of the relief. Nothing rises, ever, and the surface
reads as a window onto something behind it rather than as rock.

**2. There is no self-shadowing.** The march finds the displaced texel and never
traces toward the light, so crevices do not shadow each other. Measured
2026-08-06: `self-shadow` appears **nowhere** in `claude_documentation/` or
`engine/shaders/`. All shading comes from `N·L` on the flat face normal plus the
AO map, so the relief has *motion* parallax and no *shading* parallax — which
the eye reads as a picture that slides rather than a surface with depth.

**Occlusion is the dominant depth cue.** Every other term is secondary. This is
the cheapest large improvement available to the renderer, and it is available
because the expensive half — the march itself — is already built and measured.

### Why this and not the alternatives, decided rather than defaulted

A survey of the field was done on 2026-08-06 (sources at the bottom). The
techniques that make relief read as protruding sort into three groups and only
one of them is the right buy today:

| Group | Verdict |
|---|---|
| **Cheaper/better marches** — cone step mapping, relaxed cone stepping, quadtree displacement (Drobot) | **Not now.** They are optimisations of the *same illusion*: cone stepping's reported ~10× is quoted against a **200-step** march, and this engine marches 8–32. Both need tooling that does not exist — a cone-map baker or a min-max mip chain. **Revisit when T0155's terrain pushes step counts up**, which is the condition that would make them pay |
| **Silhouette-capable pixel techniques** — silhouette POM, prism/shell marching, interior mapping | **Closed.** T0156.6 evaluated and recommended against, on `discard`'s measured hierarchical-Z cost and non-composition with triplanar. Not reopened here |
| **Real geometry** — vertex displacement, hardware tessellation, Nanite-style runtime tessellation | **T0146.7's, and its trigger stands**: *when a silhouette must change at a density the mesh does not carry*. Complementary, not competing — production engines layer them by distance (tessellate near, POM mid, normal-map far) |

**This ticket buys the middle rung**, which covers the most screen area in
practice and is not wasted work when tessellation later lands above it.

## Done when

- [ ] A **reference plane** parameter moves where the height field sits relative
      to the polygon, so relief can read as **raised** rather than only sunken —
      with a rendered before/after and a number, not an assertion that it looks
      better
- [ ] **Self-shadowing** darkens texels the height field occludes from the light,
      measured as a frame difference against the same material with it off
- [ ] **Both are off by default and cost nothing when off** — no extra samples,
      and the existing zero-scale bit-identical invariant (T0156) still holds
- [ ] The **per-light cost decision is made with T0145, not invented here** — see
      158.3
- [ ] **A geometrically displaced cube exists as ground truth**, so every
      approximation is judged against something real rather than against taste

## Subtasks

- [x] 158.1 **The reference plane.** `heightScale` currently measures depth below
      a ceiling; add the zero point, so a value of ~0.5 puts half the range above
      the surface and half below. Free — a subtraction inside the existing march,
      no extra samples. This is Unreal's `BumpOffset` *Reference Plane* by
      another name, and it is the direct answer to "make it pop out" for a
      technique that structurally cannot
- [x] 158.2 **Parallax self-shadowing.** *Landed 2026-08-06 — in the sample's
      own `.slang`, on T0159's opened contract, not behind T0145 as the earlier
      note predicted.* The `[mutating]` hooks carry the tangent frame from
      `surfaceCoordinates` to `surface()`, and the amended D27 puts
      `g_Frame.Lights[]` in scope; the Tatarchuk-style soft march gained a
      reach clamp, a horizon fade and a light-share cap, all three found by
      watching the spinning cube live. Measured: 72 pixels darkened >10
      luminance (max drop 51.7) at the test pose, zero added black pixels,
      asserted in `tests/gpu/rockcube_sample_test.cpp`. What stays with
      158.3/T0145 is the real per-light answer — the sample approximates it by
      capping the bite at the marched light's incident share
- [ ] 158.3 **How many lights get a shadow march — with T0145.** Naively this is
      one march *per light*, which is the real cost risk and is a **lighting**
      decision, not a material one. The likely answer is "the dominant light
      only, and the fill goes unshadowed, because nobody can tell" — but a
      material must not hard-code a light index, and the loop that would know
      which light dominates is the one T0145 moves into the engine. **Two-way
      reference required**
- [ ] 158.4 **Both parameters reach a `.hpmat`**, reflected and serialised like
      every other material parameter (D23), and appear in the generated shader
      reference
- [ ] 158.5 **The grazing-angle guard.** The shadow ray degenerates exactly where
      the view march already does. It needs its own step cap and a stable answer
      past the horizon, or terminators shimmer
- [ ] 158.6 **A geometrically displaced cube** from `tools/make_cube_gltf.py` —
      subdivide each face, push vertices along the normal by the height map.
      Zero engine changes, real geometry, real silhouette, real shadows. **Not a
      feature; a reference.** It is what tells us how much of the gap each
      approximation actually closes
- [ ] 158.7 **Re-tune `rock.hpmat` once**, because a reference plane changes what
      `heightScale` means, and record the before/after

## Not in scope

- **Cone stepping, quadtree displacement, relaxed cone maps.** See the table
  above — they are the answer to a step-count problem this engine does not have
  yet, and they arrive with tooling. T0155 is the trigger.
- **Anything that changes the silhouette.** T0156.6 closed the pixel-shader
  route and T0146.7 owns the geometric one.
- **Making a game's custom `.slang` do this.** A material *could* override
  `surfaceCoordinates` and march however it likes, and that is exactly why this
  belongs in the engine instead: self-shadowing is general, and every game
  reimplementing it is the outcome D26 exists to prevent.

## Notes / findings

### 158.1 landed as **game content**, not an engine change — and the engine change was reverted

The reference plane was first built into the engine: a `Material::heightReferencePlane`
parameter in `CustomData.z`, read by `HpParallaxMarch`. It worked and it was
**wrong to do**, and the owner said so. Reverted before commit.

**It belongs in a game's `.slang` module**, and that is not a style preference —
it is what D26 and D28 are *for*. A surface technique nobody has shipped yet
should be proved as content first; the engine adopting a parameter on every
material's behalf, before any game has needed it, is the decision those two
entries exist to defer. `samples/rockcube/content/shaders/rock_pom.slang` is now
the first thing outside `tests/` to author a material shader, which makes it the
first real exercise of that path.

**The custom module reproduces the engine's march exactly** at
`kReferencePlane == 1` — luminance variation **26.7202**, the same number the
standard material produces — and at 0.5 gives **26.7297**, matching the reverted
engine-side experiment to four significant figures. That equivalence is the
correctness proof; the picture is the point of it.

### 158.2 — self-shadowing cannot be written in a material today

> **CORRECTED 2026-08-06.** The heading below said this was a limitation of the
> contract that a material *could not* overcome. Half of that is wrong and the
> correction matters: **Slang supports `[mutating]` interface requirements, and
> a non-mutating override still satisfies one** — so declaring our hooks
> mutating is a backward-compatible, ~3-line change, not a redesign. The
> blocker was never Slang; it was that *we* did not declare it. **T0159** does,
> and self-shadowing lands there. The measurements below stand exactly as
> recorded; only the conclusion "cannot" was overstated. *(Confirmed 2026-08-06
> later the same day: it landed — see 158.2 above for the numbers.)*

### Why it could not be written *as the contract stood*

Attempted, measured, abandoned in the material. Two facts close the door
together, and neither is obvious from reading the contract:

**1. `IHpMaterial`'s methods are non-mutating, so a material cannot carry
per-fragment state between hooks.** Marking `surfaceCoordinates` `[mutating]` so
it can cache the tangent frame in a member is rejected by slang with
`error[E30854]: 'surfaceCoordinates' marked as 'override' is not overriding any
base declarations` — the attribute changes the signature and the override stops
matching. Measured on the pinned `slangc 2026.14.1`.

**2. By the time `surface()` runs, `In.UV0` is the *displaced* coordinate**, so
the frame cannot be rebuilt there either. The march moved the UV per pixel, so
its screen-space derivatives are discontinuous, and the frame built from them
collapses: probed with an unshaded debug output, the shadow ray's lateral reach
came out **~0.006 UV — about three texels of a 512 map** — against the ~0.14 the
geometry calls for. It therefore never met an occluder and the shadow term was
zero everywhere, **while looking entirely correct in the source**. It took
rendering `occlusion`, `lightTS.z` and `|lightOffset|` into the colour channels
to see it.

**So self-shadowing belongs in the engine's surface stage**, where the smooth UV
and the light are both in scope — which is what this ticket said, and now has
evidence for rather than an argument. Two consequences to carry:

- **158.3's dependency on T0145 is now the *only* blocker of substance.** The
  march itself is twenty lines and was written; what it cannot get from a
  material is a frame and a light.
- **The contract gap is worth recording on its own.** "A material cannot keep
  state between hooks" is a real limit on what D28's authoring surface can
  express, and it will be hit again by any multi-hook technique — T0153's
  de-tiling is the obvious next one. Whether `IHpMaterial` should gain mutating
  methods, or a per-fragment context struct threaded through the hooks, is a
  design question for whoever hits it second.

### A process failure worth recording, because it wasted an hour

**A magenta checkerboard was reported as a working render.** The custom material
was wired up, the gpu test's `variation > 4.0` passed, the number moved, and it
was called a success — from summary statistics, without looking at the frame.
The frame was the missing-material pattern at mean RGB (127, 0, 127), because
the gpu test keeps its **own** asset list and the shader had been added only to
the module's, so the material named a shader GUID that was not in the pool.

That is precisely the failure `rockcube_sample_test.cpp`'s own comment warns
about — *"a missing material renders the checkerboard, and the checkerboard
would pass a coverage check happily"* — and it was written in that file before
this happened. It then produced a second wrong conclusion: that an **empty**
custom module renders differently from the standard material, which would have
been an engine bug contradicting D28. It does not; both frames were magenta.

**The guard that would have caught it:** the gpu test asserts coverage and
variation, and neither excludes the checkerboard. A cheap assertion that the
frame is not predominantly magenta belongs in every case that renders a material
by GUID.

### Measured 2026-08-06, on T0157's sample, before any of this was built

- **The brightest face barely reached its own unlit albedo.** Rendering the
  `BaseColor` debug view against the shaded frame: left face 34.9 unlit / 19.1
  shaded, right 32.8 / 37.2, top 34.6 / 29.3. Lighting was correct in
  *direction* the whole time and far too weak in *magnitude* — see T0157's
  finding for T0096 about `intensity` being unitless with no reference scale.
- **Turning POM off barely moves the frame statistics**: coverage identical to
  four decimal places, luminance variation 11.08 with the march and 12.45
  without it. The march relocates which texel is seen; it does not add contrast.
  That is the measurement that says the missing ingredient is *shading*, not
  more displacement — and it is why 158.2 is expected to matter more than 158.1.
- **The reference plane is geometrically a lie above the plane**, and should be
  documented as one. A texel raised above the polygon should have been hit by
  the ray *before* it reached the surface, and was not, so raised regions swim
  more at grazing angles than sunken ones. Keep the default modest — half-out is
  convincing, fully-out is a different artefact.

### Sources for the survey behind the "why not the alternatives" table

Tatarchuk, *Practical Dynamic Parallax Occlusion Mapping* and *Practical POM
with approximate soft shadows* (ACM); Policarpo & Oliveira, *Relief Mapping of
Non-Height-Field Surface Details* (I3D 2006) and *Real-Time Relief Mapping on
Arbitrary Polygonal Surfaces*; Drobot, *Quadtree Displacement Mapping with
Height Blending* (GPU Pro 1); *Quadtree Relief Mapping* (Graphics Hardware
2007); Szirmay-Kalos & Umenhoffer, *Displacement Mapping on the GPU — State of
the Art*; *Silhouette management for protruded displacement mapping*; Epic,
*Using Bump Offset in Unreal Engine* (the Reference Plane parameter); Nanite
tessellation and displacement, UE 5.4.

**The same caveat T0156.6 recorded applies to every performance claim above:**
the primary literature is 2005–2010 and none of it has a modern re-benchmark, so
relative costs were treated as direction rather than magnitude, and this engine's
own numbers (T0156's cost table) are the only measured ones.
