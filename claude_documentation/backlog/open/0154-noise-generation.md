# T0154 — Noise: one field, generated on the CPU, evaluated on the GPU, and provably the same

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 462 |
| **Created** | 2026-08-06 |
| **Refs** | [0153-surface-detiling.md](0153-surface-detiling.md) — **its tier 1 and tier 2 depend on this and it was written before this existed**; [0091-volumetric-fog.md](0091-volumetric-fog.md), [0092-wet-surfaces.md](0092-wet-surfaces.md), [0080-particles.md](0080-particles.md) — consumers; [0093-visibility-and-fog-of-war.md](0093-visibility-and-fog-of-war.md) — **the case where CPU and GPU must agree about a field**, which is what 154.5 exists for; [0097-texture-import-pipeline.md](0097-texture-import-pipeline.md) — a baked noise texture is content and travels its path; **D12** (gameplay in lockstep, so determinism is not optional), **D13** (VFS), **D21** (only a math subset is exported to consumers) ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)) |

## Why

**The owner asked, having noticed Godot ships one:** *"we don't have any noise
generator for this engine like godot does right? shouldn't we add this?"*

**Measured 2026-08-06 — there is nothing, and nothing vendored either.** Zero
matches for perlin, simplex, worley, cellular, fbm, value-noise or FastNoise
across `engine/`, `tests/` and `samples/`. No noise library in `third_party/`.
No ticket owned it.

DiligentEngine has fragments and **none of them is usable as an answer**:

- `DiligentSamples/Samples/Asteroids/src/simplexnoise1234.{h,c}` — real simplex
  noise, but it sits in a **sample**, not in DiligentFX. Not a library, and not
  something to take a dependency on.
- `DiligentFX/Shaders/Common/private/ComputeBlueNoiseTexture.fx` plus a baked
  blue-noise sampler — **private**, and purpose-built for SSR/TAA dithering.

So the standing first question here — *is it already in `third_party/`?* — has
been asked and answered.

**T0153 already depends on this, and was written before it existed.** Its tier 1
is literally "multiply by low-frequency noise" and its tier 2 needs a per-cell
hash. That is the gap making itself felt immediately rather than theoretically.

Beyond that: volumetric fog (T0091), wet surfaces (T0092), particles (T0080,
curl noise for turbulence), and — the one that matters most for a studio
building **several** games — gameplay-side procedural generation.

## The design point this ticket exists to get right

**CPU noise and GPU noise are different things**, and Godot's split is the model
worth copying deliberately rather than arriving at by accident:

| | Requirement |
|---|---|
| **CPU** | deterministic, seedable, callable from gameplay, reproducible **across targets** |
| **GPU** | cheap hash/value/simplex in Slang, evaluated per fragment — cannot share the CPU implementation |
| **The bridge** — bake CPU noise to a texture | so a shader and gameplay see **the same field** |

**That third row is the part that gets retrofitted badly.** If gameplay decides
"this tile is grass" from noise and the shader shades it from *different* noise,
they disagree — subtly, and only at the edges. **T0093 is already a case where
CPU and GPU must agree about a field**, so this is not hypothetical. Designing
the bridge now costs little; bolting it on later is the expensive version, and
it is the class of retrofit D27's contract discipline exists to avoid.

## Determinism is a hard requirement here, not a nice-to-have

**D12 puts gameplay in lockstep against the real headers**, and this engine
cross-compiles to Windows. If gameplay depends on noise — and procedural
generation is exactly that — then **the same seed and coordinates must produce
the same result on Linux and Windows**. Floating-point noise does not guarantee
that for free.

**This is measurable here rather than arguable**: the harness already runs both
target suites (`zig build test -Dtest=all`, Windows under wine). So 154.3 settles
it with a test instead of an assumption — and if it does *not* hold, that is a
finding that changes the design (integer-lattice noise, or fixed-point) rather
than a caveat to write down and hope about.

## Done when

- [ ] **Gameplay can generate seeded noise**, and the same seed and coordinates
      produce **bit-identical** results on both targets — asserted by a test in
      the fast bucket, not assumed
- [ ] **A shader can evaluate noise** through the engine's own Slang functions,
      including the per-cell hash T0153 needs
- [ ] **Noise can be baked to a texture** (2D and 3D) so a shader and gameplay
      can be shown to read the same field
- [ ] **The library choice is recorded with what was rejected and why** — per
      `CLAUDE.md`, building it here is a conclusion that has to be argued for,
      not the default
- [ ] The **seeding and determinism policy is written down** — what is stable
      across engine versions, and what is explicitly not
- [ ] **What is exposed to gameplay is decided deliberately**, respecting D21's
      rule that only a math subset crosses to consumers

## Subtasks

- [ ] 154.1 **Survey and choose, recording rejections.** The leading candidate
      is **FastNoiseLite**: single header, MIT, no dependencies, no build system
      of its own, no network fetch at configure time — so it passes every
      constraint in `03-build-harness.md`, and header-only means the MinGW
      cross-link never sees it. It is also what Godot itself ships. Evaluate it
      properly rather than adopting it because this ticket named it, and write
      down what else was considered
- [ ] 154.2 **The CPU generator** — noise types (Perlin, OpenSimplex2,
      cellular/Worley, value), fractal modes (FBm, ridged, ping-pong), domain
      warp, seeded
- [ ] 154.3 **The cross-target determinism test** — same seed, same
      coordinates, bit-identical on Linux and Windows. **Do this early**: a
      negative result changes 154.2's design, and discovering it after gameplay
      depends on noise is the expensive order
- [ ] 154.4 **Slang-side noise** — hash, value and simplex functions in the
      engine's shader library, plus the per-cell hash T0153.2/153.3 consume.
      Note these are a *different implementation* from 154.2 and are not
      expected to match it bit for bit; 154.5 is what makes agreement possible
      where agreement is needed
- [ ] 154.5 **The bridge: bake to texture**, 2D and 3D, so the field a shader
      samples is provably the field gameplay generated. Godot's
      `NoiseTexture2D/3D` is the shape. **This is the subtask that stops T0093
      needing a retrofit**
- [ ] 154.6 **A noise configuration as reflected, serialisable data** — so it is
      authorable, inspectable and diffable like a material. `entt::meta` and the
      material format are the precedent
- [ ] 154.7 **Decide the gameplay-facing surface** against D21 — what a game
      module can call, and what stays engine-internal

## Not in scope, so it is a decision rather than an omission

- **Blue noise / dithering patterns.** DiligentFX already carries a baked blue
  noise sampler for its post-process components; if T0148 needs one, it uses
  theirs rather than generating it here. Different problem, different answer.
- **A terrain system.** Heightmap generation is a consumer of this, not part of
  it — and the Diligent capability survey found a complete ring-LOD terrain
  implementation in their samples tree that no ticket owns. That is its own
  conversation.
- **A node-graph noise editor.** D32's visual-editor answer applies.

## Notes / findings

### Measured 2026-08-06 — the gap, and what the vendored tree actually has

`grep -rniE 'perlin|simplex|worley|cellular|fbm|value.?noise|FastNoise|opensimplex'` over `engine/`, `tests/`, `samples/`: **no matches**. `third_party/` contains no noise library. The only noise in DiligentEngine is a sample-local simplex implementation (Asteroids) and a private blue-noise generator for post-process dithering — recorded above with paths, so the next person does not repeat the search.
