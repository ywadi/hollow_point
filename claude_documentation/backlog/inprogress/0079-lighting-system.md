# T0079 — Lights and per-object light selection

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 470 |
| **Created** | 2026-08-03 |
| **Refs** | T0085, T0093, [0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md) — **decide whether this ticket configures DiligentFX's lighting or supersedes it**, rather than discovering the overlap mid-implementation; [../completed/0027-render-stack.md](../completed/0027-render-stack.md) — **the engine currently renders every mesh pure black**, measured, and this ticket is what makes shading assertable at all |

## Why

DiligentFX provides the *pieces* — `PBR_Renderer` consumes up to 16 lights and 8
shadow casters, `ShadowMapManager` manages cascade textures, `EnvMapRenderer`
handles environment lighting — but there is **no lighting system**: no light
components, no deciding which lights affect which object, no allocating shadow
maps across the lights that need them.

Without it, lighting is whatever was baked into an imported glTF, which is not a
usable authoring story.

## Done when

- [x] Light components: directional, point, spot, with colour, intensity, range — `hp::Light`, all three verified in pixels on hardware
- [ ] Lights are authored in the editor and serialized with the scene
- [ ] Per-object light selection — the most relevant lights within the shader limit
- [ ] Lights respect the illumination layer mask (T0085)
- [ ] Lights are frustum-culled like geometry (T0045)
- [~] Exceeding the light limit degrades gracefully — it **caps and warns** rather than failing, but *which* lights survive is registry order, which is arbitrary. Graceful requires selection (79.3)

## Subtasks

- [~] 79.1 Light components — built; **not yet reflected** (`Scene.cpp` registration), so they do not serialise
- [~] 79.2 Light gathering — built (`gatherLights`), with the placement resolve split out for T0093; **no frustum culling**, which needs T0045
- [ ] 79.3 Per-object light selection — see notes, this is the design decision
- [x] 79.4 Feed lights into `PBR_Renderer`'s frame attribs — written after the frame struct, with `Renderer.LightCount` set
- [ ] 79.5 Illumination layer mask applied during selection (T0085)
- [ ] 79.6 Debug visualisation of light bounds and affected objects (T0061)
- [ ] 79.7 Profiling zones for light gathering and selection

## Built and measured 2026-08-05 — the engine renders lit surfaces

`engine/include/hp/Light.hpp`, `engine/src/Light.cpp`,
`tests/gpu/lit_surface_test.cpp`. **On an RTX 4070**, both backends:

```
lit red quad under a white light: (211, 144, 144)
same quad with no light:          (0, 0, 0)
intensity 0.5: (92, 61, 61)   versus 3.0: (211, 144, 144)
point light 2 units away: (255, 145, 145),  9 units away: (80, 46, 46)
point light past its range: (0, 0, 0)
spot wide cone: (255, 251, 251),  narrow cone: (0, 0, 0)
```

That is **the assertion T0134 recorded as owed and nothing here could make**:
every pixel test before this one could only ask "did geometry arrive", because
the frame was black whatever the material said.

### The bug, and it is the one this repository had already written down

Adding `PSO_FLAG_USE_LIGHTS` to `kFeatureMask` **changed nothing**. The frame
came back exactly as black as before, with every counter still agreeing a draw
had been issued.

`RenderInfo::Flags` is a **mask** — T0134 recorded that in as many words, after
T0028 got it wrong with `PSO_FLAG_UNSHADED` — so it can only ever *remove* a
flag. `flags = (vertexFlags | kMaterialFlags) & kFeatureMask` cannot produce a
bit that is not already in the left-hand side.

Fixed by splitting the two ideas that were being conflated: **`kFeatureMask`**
is what is permitted, **`kEnabledFeatures`** is what the engine turns on. A
feature not derived from the material or the vertex layout — lights, and later
IBL and shadows — goes in the second. Worth naming because the ticket after this
one will reach for the same lever.

### Two measurements that were surprising and are not bugs

**The specular highlight is achromatic.** A red quad under a white light reads
`(211, 144, 144)`, not `(255, 0, 0)` — the specular term is the colour of the
*light*, so it lifts green and blue equally. The first version of the test
asserted `g < r / 2` and failed; the assertion was wrong about PBR, not the
renderer wrong about lighting. It now asserts ordering and the `g == b` symmetry,
which distinguishes "lit red surface" from every failure mode without pinning a
BRDF this ticket does not own.

**The test quad is lit from behind.** The directional case works with
`L = -direction = +Z`, so a point light placed between the camera and the quad
faces *away* from the surface and renders black. Correct, and it cost a
measurement to see — the lights had to move to `z > 3`. Recorded because the next
person writing a lighting test will put the lamp between the camera and the
subject, exactly as I did.

### T0093's obligation, discharged before it could be forked

The ticket says gathering must stay reusable for a projector that does not shade,
"or T0093 ends up forking it". The forkable part is not the loop — it is
**deriving a world position and facing from a transform**, including the glTF
negative-Z convention and the degenerate-matrix guard. Three lines that are wrong
in two interesting ways.

That is now `hp::resolvePlacement(const float4x4&)` returning `ResolvedPlacement`,
which `gatherLights` calls. A vision cone reuses it and never touches `Light`.
**Per-object *selection* (79.3) does not exist yet, and when it does it must be
written generically too** — that half of the obligation is still open.

### Not done, and the honest shape of it

- **79.3 — per-object light selection does not exist.** Every light lights
  everything, frame-wide. This is the ticket's stated design decision
  (nearest-N versus clustered) and it is untouched.
- **79.5 — no illumination layer mask.** `Light` deliberately has **no**
  `LayerMask` field yet: a mask is applied *during selection*, and adding a field
  now that nothing reads is precisely the mistake `Camera::cullingMask` spent
  three tickets being. It goes in with 79.3.
- **Lights are not reflected and do not serialise.** `Scene.cpp` registers
  `Camera` and `MeshRenderer` with `entt::meta`; `Light` is not registered, so a
  light cannot be saved with a scene. 79.1 is only half done.
- **No frustum culling of lights** (79.2) — needs T0045.
- **Intensity is unitless.** glTF specifies lux for directional and candela for
  point/spot; honouring that needs exposure, which is T0096's. Documented in the
  header as a revisit rather than quietly approximated.
- **Nothing measures cost.** 79.7's profiling zones exist via `HP_PROFILE_ZONE`
  in `gatherLights`, but no scene has enough lights to measure.

## Notes / findings

### Inherited from T0085 (2026-08-05) — the mask type exists; the light field does not

**85.4 moved here.** `hp::LayerMask` is built, honoured by `parseScene` for
cameras, reflected and serialised. What is missing is the field on a light
component, because lights do not exist yet.

Two things to get right when 79.1 and 79.3 land:

- **The light carries a mask, the object carries layers**, matching
  `Camera::cullingMask` against `MeshRenderer::layers`. Use `hp::LayerMask` —
  **do not add a bare `uint32_t`**, which is exactly the second vocabulary T0085
  removed from `Camera`.
- **Apply it during per-object light selection (79.3), never in the shader.** A
  per-pixel mask test wastes the whole cost of shading the object it discards.
  The selection loop already has to run per object, so the test is one AND in a
  loop that exists.

T0086 owns a **separate** shadow-casting mask (85.5) — "lit by this light but
does not cast its shadow" is a common requirement, so do not collapse the two
into one field. See [../completed/0085-layers-and-masks.md](../completed/0085-layers-and-masks.md).

### Inherited from T0134 / D24 (2026-08-05) — configure DiligentFX's lights, do not supersede them

**This ticket's Refs asked the question; D24 answers it: configure.** And the
answer arrives with more already decided than the Done-when above assumes.

**`PBRLightAttribs` already specifies the light model**
(`Shaders/PBR/public/PBR_Structures.fxh:309`):

```c
int   Type;            // 1 directional, 2 point, 3 spot
float PosX, PosY, PosZ;
float DirectionX, DirectionY, DirectionZ;
int   ShadowMapIndex;  // -1 if this light casts none
float IntensityR, IntensityG, IntensityB;
float Range4;          // point/spot range to the fourth power
float SpotAngleScale, SpotAngleOffset;
```

That is the whole of "directional, point, spot, with colour, intensity, range",
**already fixed as a shader-side representation**. So 79.1 designs ECS
*components* and how they map onto this — not a new representation. Note
`Range4` is range to the **fourth power**, and the spot cone is stored as a
precomputed scale/offset pair, not as angles: those conversions belong in one
place or they will disagree.

The mechanism, so nothing is re-surveyed:

- `PBR_Renderer::CreateInfo::MaxLightCount` (default 16) sizes the array, and
  **is currently 0** — which is why nothing is lit.
- `GLTF_PBR_Renderer::WritePBRLightShaderAttribs` is **`static`**, so it is
  usable from the engine's own traversal exactly as the material and primitive
  writers already are.
- `Renderer.LightCount` in `PBRRendererShaderParameters` is what the shader
  clamps its loop to. `SceneRenderer` sets it to 0 explicitly today; **this
  ticket sets it for real.**
- `PSO_FLAG_USE_LIGHTS` must come out of `kFeatureMask` in `SceneRenderer.cpp`.
- The frame buffer is sized by
  `GetPRBFrameAttribsSize(LightCount, ShadowCastingLightCount)`, so raising
  `MaxLightCount` resizes it — it is not a free constant.

`EnableAO` and `Renderer.OcclusionStrength` are also this ticket's, per D24.

Read [../completed/0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md) first.

**Scope:** this ticket is lights and selection only. **Shadows are T0086** and
**environment/IBL is T0087** — lighting is a large area and splitting it keeps
each piece reviewable.


**Per-object light selection is the real design decision.** `PBR_Renderer` takes
a fixed slot count (16 by default), so with more lights in a scene something must
choose. Options: nearest-N per object (simple, pops when the set changes as the
camera moves), tiled/clustered forward (scales far better, significantly more
work), or deferred (changes the whole pipeline shape). **Start with nearest-N and
measure** — but pick the sort criterion carefully, because naive distance sorting
is exactly what causes visible popping.

The old `terrain_lab` app used `EpipolarLightScattering`, so atmospheric
scattering is available in DiligentFX if outdoor scenes matter.

### Measured 2026-08-05 (T0027) — the engine renders every mesh pure black

Not a prediction; read back off a real GPU on both backends, and it changed the
design of another ticket's test:

```
sampled colour of a lit-material quad: (0, 0, 0, 255)
```

`baseColorFactor`, `emissiveFactor` and alpha make no difference.
`SceneRenderer` creates `PBR_Renderer` with `MaxLightCount = 0`,
`EnableIBL = false` and `EnableEmissive = false` — each off because it belongs to
a later ticket and needs resources nothing supplies yet — so the shading result
is zero everywhere. Nothing is broken; there is no light in the world.

**Two things follow for this ticket:**

- **It is the first ticket able to assert that shading is correct at all.** Every
  pixel test written so far can only ask "did geometry reach the target", because
  black-on-clear-colour is the only signal available. Turning on one directional
  light makes material colour observable, and the first test worth writing here
  is the one that asserts a lit surface is the colour it was authored as.
- **T0028's "a mesh is drawn" evidence is weaker than it reads**, and should be
  strengthened here rather than left. That test asserts pixels differ from a blue
  clear colour, and they differ *because they are black* — it would pass
  identically with shading completely broken. The geometry path it proves is
  real; the shading it appears to imply is not tested by anything yet.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0093**: vision sources are cone/radial projectors that reuse light-shaped
  machinery but are *not* lights — a vision cone must not illuminate, and
  visibility is applied as its own material term, never folded into the
  lighting accumulation. Keep light gathering/selection (79.2/79.3) reusable
  for a projector component that does not shade, or T0093 ends up forking it.
