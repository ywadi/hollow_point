# T0158 — Making parallax read as relief: the reference plane and self-shadowing

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 464 |
| **Created** | 2026-08-06 |
| **Blocked by** | nothing — both halves sit inside the surface stage the engine already owns (**D26**) |
| **Refs** | [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) 141.7 — the march this extends; [../completed/0156-parallax-under-triplanar.md](../completed/0156-parallax-under-triplanar.md) — the per-projection march, and **156.6's evaluation against silhouette POM**, which is what makes this the remaining move; [../completed/0157-rock-cube-sample.md](../completed/0157-rock-cube-sample.md) — **the sample that exposed the gap by being looked at**, and the test bed; [0145-lighting-stage-own-the-light-loop.md](0145-lighting-stage-own-the-light-loop.md) — **158.3 must be decided there**, see below; [0146-vertex-stage-hook.md](0146-vertex-stage-hook.md) 146.7 — tessellation, the *other* answer, and deliberately not this one; [0155-terrain-rendering.md](0155-terrain-rendering.md) — the consumer that would justify the faster marches this ticket rejects; [0087-environment-lighting.md](0087-environment-lighting.md), T0096 — findings land on both; **D24**, **D26**, **D27** ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)) |

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

- [ ] 158.1 **The reference plane.** `heightScale` currently measures depth below
      a ceiling; add the zero point, so a value of ~0.5 puts half the range above
      the surface and half below. Free — a subtraction inside the existing march,
      no extra samples. This is Unreal's `BumpOffset` *Reference Plane* by
      another name, and it is the direct answer to "make it pop out" for a
      technique that structurally cannot
- [ ] 158.2 **Parallax self-shadowing.** A second, shorter march from the hit
      point toward the light; if the height field blocks it, darken. Fewer steps
      than the view march — a shadow ray answers *blocked or not*, not *where* —
      and it wants a soft falloff rather than a binary result, per Tatarchuk's
      approximate soft shadows
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
