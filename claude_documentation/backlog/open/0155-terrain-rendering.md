# T0155 — Terrain rendering: their reference implementation is the floor, not the ceiling

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Very Complex |
| **Phase** | 4 — Render layer |
| **Order** | 464 |
| **Created** | 2026-08-06 |
| **Refs** | [../../documentation/12-vendored-capabilities.md](../../documentation/12-vendored-capabilities.md) — **read this first**, it is why this ticket exists; [0153-surface-detiling.md](0153-surface-detiling.md) — **terrain is de-tiling's worst case** and this ticket is its most demanding consumer; [0154-noise-generation.md](0154-noise-generation.md) — procedural heightmaps and erosion both need it; [0086-shadows.md](0086-shadows.md) — their terrain reuses the *same* cascaded path, so this depends on it rather than duplicating it; [0088-sky-atmosphere-time-of-day.md](0088-sky-atmosphere-time-of-day.md) — the sky above it, and the sample that carries the terrain; [0045-culling-and-render-queues.md](0045-culling-and-render-queues.md) — LOD selection is queue work; [0097-texture-import-pipeline.md](0097-texture-import-pipeline.md) — heightmaps and splat masks are content; [0146-vertex-stage-hook.md](0146-vertex-stage-hook.md) — **owns tessellation (146.7)**, deferred with a trigger this ticket is the likeliest to pull; **D24**, **D26** ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)) |

## Why

**Nothing in the backlog owned terrain.** A capability survey of the vendored tree
(2026-08-06) searched every open ticket for terrain, heightmap, clipmap, geo-mip
and large-world, and found only three tickets — T0087, T0088, T0091 — mentioning a
**retired `terrain_lab` app** in passing. They own the sky; nobody owned the
ground.

Meanwhile **a complete working implementation sits in the vendored tree**:
concentric ring mesh, 16-bit heightmap, 5-layer material-mask splatting with
per-layer normal maps, and cascaded-shadow integration. That is exactly the
situation this project's library rule exists to catch.

**The owner's framing, and it sets this ticket's shape:** *"we should have terrain
rendering (but we might need to heavily modify or create our own with erosion,
texture blending and de-tiling and more)."*

So this is not "adopt their terrain". It is **use theirs as the reference and the
floor, and be explicit about where we exceed it.**

## What theirs actually is — and its four real limits

Measured, not assumed, from `DiligentSamples/Samples/Atmosphere/src/Terrain/`:

- A **CPU-built concentric-ring mesh** around the camera (`RingMeshBuilder`), with
  degenerate-triangle strips stitching ring boundaries. Not GPU tessellation, and
  not a true geometry clipmap.
- Height sampled **on the CPU at mesh-build time**, from a 16-bit heightmap.
- Normal maps generated **once** from the heightmap on the GPU.
- Five material layers blended by normalised mask weights, each with diffuse and
  normal.
- Shadowing reuses **`Shadows.fxh`'s cascaded path** — the same one T0086 owns.

**The limits, stated plainly because they decide how much survives:**

1. **No runtime LOD selection and no re-tessellation.** The mesh is built once and
   rendered as-is. Ring count and dimension are fixed at create time.
2. **`TEXTURING_MODE` is a compile-time shader macro** baked into the PSO, so
   switching texturing mode at runtime means pre-built PSOs per mode — which the
   sample does not do.
3. **It is `Samples/`, not `DiligentFX/`.** This is copy-and-adapt source with all
   the maintenance that implies, **not** a library to link. That changes the cost
   by an order of magnitude versus the six post-process components, and it is the
   single most important fact in this ticket.
4. **Five layers, fixed** (`NUM_TILE_TEXTURES = 1 + 4`).

**None of it needs compute** — CPU mesh build plus vertex/pixel shading — so T0150
does not block this.

## Where we exceed it, which is the actual work

| Beyond theirs | Why | Depends on |
|---|---|---|
| **De-tiling** | Terrain is the worst case in the engine: huge surfaces, world-space tiling, no unwrap to hide behind. Their 5-layer splat does nothing about repetition | **T0153** |
| **Erosion** | Hydraulic and thermal erosion is what makes a heightmap read as landscape rather than as noise. **Decide offline-tool vs runtime** — see below | **T0154** |
| **Richer texture blending** | Height-blend and slope/altitude rules rather than plain mask weights; more than 5 layers, or a virtual-texture answer | T0153, T0097 |
| **Real runtime LOD** | Theirs has none. Clipmap or chunked-quadtree, with the popping question answered | **T0045** |
| **Procedural heightmaps** | Generated rather than authored, deterministically | **T0154** |

## The decision this ticket must make first

**Adopt-and-extend, or build our own?** The honest answer is not obvious, and it
should be argued rather than defaulted:

- **Adopt-and-extend** — start from their ring mesh, replace pieces. Fastest to
  something on screen. But it is sample code we then own entirely, and its
  fixed-mesh design is the thing most of the wants above would replace anyway.
- **Build our own, reading theirs** — take the parts that are genuinely reusable
  (the heightmap sampling, the splat shading, the cascade integration) and write
  the LOD scheme we actually want.

**The tell is limit 1.** If real runtime LOD is required — and "large open
terrain" implies it — then their central design decision is the one being
replaced, and adopting the rest around it may cost more than it saves. Do not
settle this from the description; read `EarthHemisphere.cpp` and decide with the
source in front of you.

## Done when

- [ ] **The adopt-vs-build decision is recorded** with what was rejected and why
- [ ] Terrain renders with **runtime LOD**, and the popping/transition policy is
      written down rather than tuned into existence
- [ ] **De-tiling applies to terrain** (T0153), measured on a large surface —
      this is the case that proves T0153 rather than a nice-to-have
- [ ] **Texture blending exceeds plain mask weights** — at minimum height-aware
      blending, with the layer count decided rather than inherited
- [ ] **Erosion exists, and its home is decided** — offline tool or runtime — with
      the reasoning
- [ ] Terrain shadows reuse **T0086's** cascade path, not a bespoke one
- [ ] **What is not delivered is written down with a trigger** — this ticket is
      large and will ship in stages; the stages should be named

## Subtasks

- [ ] 155.1 **Read `EarthHemisphere.cpp` and make the adopt-vs-build call.** First,
      and with the source open. Record rejections
- [ ] 155.2 The LOD scheme — clipmap, chunked quadtree, or theirs extended.
      **If the answer is GPU tessellation, the stage work is `T0146.7` and it
      is not built** — `PBR_Renderer` creates no hull or domain shaders, so
      adding them is pipeline work that lives with the ticket that owns the
      pre-rasteriser stages (inherited there from T0141.9 on 2026-08-06, with
      the trigger *"when a silhouette must change at a density the mesh does
      not carry"*). **This ticket is its likeliest trigger**; do not
      re-derive the capability here
- [ ] 155.3 Heightmap as content: format, streaming, and T0097's path
- [ ] 155.4 Texture blending: height-aware, slope/altitude rules, layer count
- [ ] 155.5 De-tiling on terrain (consumes T0153; this is its hardest case)
- [ ] 155.6 Erosion — **decide offline vs runtime first**; hydraulic and thermal.
      Offline makes it an authoring tool and a content pipeline question;
      runtime makes it a compute problem and therefore T0150's
- [ ] 155.7 Procedural heightmap generation (consumes T0154, deterministic)
- [ ] 155.8 Cascade-shadow integration against T0086, reusing rather than forking
- [ ] 155.9 Culling and draw submission against T0045

## Not in scope, so they are decisions rather than omissions

- **Vegetation, scattering and foliage** — a different system that sits on top.
- **Terrain editing in the editor** — editor-era, and it needs 155.3's format
  settled first.
- **Virtual texturing** — a candidate answer to layer-count limits and to
  repetition, and a different order of magnitude. Named in T0153 as out of scope
  there too; if it is ever wanted it is its own ticket.

## Notes / findings

### Surveyed 2026-08-06 — what exists, and why this is not simply adoption

The vendored implementation is real and complete, and it is in `Samples/`. The
four limits above are what a survey of `EarthHemisphere.{hpp,cpp}`,
`ElevationDataSource.{hpp,cpp}` and `assets/shaders/terrain/*.fx` found. Recorded
here so 155.1 starts from measurement rather than from a fresh reading — but 155.1
should still open the file, because "no runtime LOD" is the claim the whole
adopt-vs-build decision turns on.
