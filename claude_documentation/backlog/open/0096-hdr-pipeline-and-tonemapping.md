# T0096 — HDR pipeline, tonemapping and the linear-workflow policy

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 460 |
| **Created** | 2026-08-03 |
| **Refs** | T0027, T0046, T0060, T0087, T0089, T0111, [../completed/0130-camera-lens-model.md](../completed/0130-camera-lens-model.md), [0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md) — **DiligentFX ships its own `ToneMapping` component; this ticket's policy must say whether it is used** |

## Why

The render tickets assume PBR throughout — `PBR_Renderer` materials (T0060),
punctual lights (T0079), IBL (T0087) — and T0027 already notes that
"tonemapping and bloom apply to the world layer, not to the UI". But **no
ticket owns the pipeline that makes PBR output correct**: rendering in linear
HDR, tonemapping to display, and keeping sRGB/linear straight end to end.

Without an owner this arrives piecemeal, and colour-space bugs are the silent
kind this project keeps recording (G-series): lighting gets tuned against an
LDR clamped buffer, an albedo texture gets sampled linear when it is sRGB,
the UI gets tonemapped along with the world — each individually looks
plausible, and fixing it later means re-tuning every light and every material
in every scene. That re-tuning *is* the cost this ticket exists to avoid.

Verified in the tree: DiligentFX ships a **ToneMapping component**
(`DiligentFX/Components/interface/ToneMapping.hpp` + shader library) alongside
Bloom, DepthOfField, SSAO, SSR and TAA under `DiligentFX/PostProcess/`. The
work here is wiring and policy, not writing a tonemapper.

## Done when

- [ ] The world renders into a **linear HDR** target — format decided and
      recorded (RGBA16F vs R11G11B10, memory vs precision) in T0046's frame
      targets
- [ ] Tonemapping converts HDR → display via DiligentFX's component; UI, HUD
      and debug draw composite **after** it (T0027's ordering, now enforced)
- [ ] sRGB vs linear is explicit at every texture bind: albedo/emissive sRGB,
      normals/data/lookup textures linear — recorded per asset (T0097), not
      guessed per call site
- [ ] The exposure model is decided and recorded: fixed per scene first;
      auto-exposure only if evidence demands it
- [ ] Swapchain colour-space handling is correct on **both** backends — GL and
      Vulkan sRGB framebuffer semantics differ, and this is per-backend code
- [ ] The editor viewport (T0033) displays the tonemapped image, not the raw
      HDR target
- [ ] A deliberately overbright test scene shows highlights rolling off rather
      than clipping

## Subtasks

- [ ] 96.1 Decide the HDR target format and add it to T0046's declared formats
- [ ] 96.2 Wire the DiligentFX ToneMapping component as the world layer's
      resolve step in the RenderStack (T0027)
- [ ] 96.3 Write the sRGB policy down (which semantic slots are sRGB) and
      plumb it through texture load (T0023/T0097) and material binding (T0060)
- [ ] 96.4 Exposure control as a scene/camera setting, serialized
- [ ] 96.5 Confirm ordering against debug draw (T0061) and the UI layer — both
      post-tonemap
- [ ] 96.6 Verify on both backends; expect the sRGB-framebuffer and Y-flip
      differences to show up here first
- [ ] 96.7 Leave the hook where Bloom/TAA slot in later, behind quality
      settings (T0078) — do not integrate them yet

### Inherited from T0111 / D25 (2026-08-05) — you own the composite seam

**Three separate things all want the same pass, and D25's instruction is: do not
build three.**

1. **Tonemap** — D24 already recommends a pass over an HDR target rather than
   `PSO_FLAG_ENABLE_TONE_MAPPING`.
2. **The render-scale upscale** — D25 sizes world targets from
   `output x renderScale`, so something must resolve them to native.
3. **UI at native resolution** — T0027.5 composites every layer into one target, so
   a HUD inherits the world's render scale unless the stack becomes two-phase.

All three sit between the world layers and the UI layers. Whichever ticket reaches
this seam first should build it so the others plug in.

Also from D25: **MSAA is out**, which removes the resolve-before-tonemap trap this
ticket would otherwise have to handle (resolving multisampled HDR before tonemapping
lets bright samples dominate the average and produce fireflies). And `AverageLogLum`
in `PBRRendererShaderParameters` is the auto-exposure hook, with **T0130.4 already
deciding exposure lives on the camera** — auto-exposure writes `Camera::exposureEv100`
rather than shadowing it.

See [../completed/0111-anti-aliasing-and-render-scale.md](../completed/0111-anti-aliasing-and-render-scale.md).

### Inherited from T0134 / D24 (2026-08-05) — and this ticket's Refs stated a false premise

**Correction first, because it is written into this ticket's own Refs and into
T0134's Done-when, and it is wrong.** The claim was that DiligentFX ships its own
`ToneMapping` *component* alongside the in-shader path, so the two could
double-apply. **There is no such component.**

- `Components/interface/ToneMapping.hpp` declares exactly two functions —
  `ReverseExpToneMap` and `ToneMappingUpdateUI`. It is a UI helper, which is why
  **D6** lists it among the ImGui-calling headers.
- `Shaders/PostProcess/ToneMapping/` contains only `ToneMapping.fxh` and
  `ToneMappingStructures.fxh` — a shader **include**, shared rather than staged.
- `PostProcess/` on the C++ side has **no ToneMapping directory**: only `Bloom`,
  `DepthOfField`, `EpipolarLightScattering`, `ScreenSpaceAmbientOcclusion`,
  `ScreenSpaceReflection` and `TemporalAntiAliasing`.

**Tonemapping in DiligentFX happens in the PBR pixel shader and nowhere else** —
`RenderPBR.psh:530-540`, under `#if ENABLE_TONE_MAPPING`, calling
`ToneMap(OutColor.rgb, TMAttribs, g_Frame.Renderer.AverageLogLum)` with
`TONE_MAPPING_MODE` as a compile-time define. Twelve modes ship, including AgX
and PBR-neutral. So there is **no double-apply hazard to design around.**

**The real fork, and D24's recommendation — take the pass:**

- *In-shader* (`PSO_FLAG_ENABLE_TONE_MAPPING`): no extra pass, no HDR target.
  But it tonemaps **per draw, before blending** — which breaks transparency and
  leaves DiligentFX's `Bloom` component with no HDR image to read.
- *A pass over an HDR target*: what `TargetFormat::ColourHDR` (`RGBA16_FLOAT`)
  already exists for, and the only option compatible with bloom and correct
  transparency. `Renderer.AverageLogLum` is the auto-exposure hook, and
  **T0130.4 already decided exposure lives on the camera** — so auto-exposure
  writes `Camera::exposureEv100` rather than shadowing it.

**Ordering, which T0027 flagged and the current architecture gets right by
accident:** tonemapping must apply to the world and not to UI composited over it.
Today that holds because it would happen inside the world layer's own pixel
shader. **A tonemap pass must preserve it** — it belongs between the world layers
and the UI layers, which is what `IRenderLayer::order` exists to express.

Read [../completed/0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md) first.

**T0130 put exposure on the camera, and this ticket must not add a second one.**
`hp::Camera::exposureEv100` is the exposure, as EV100, and the reasoning is that
exposure is a property of a view: a frame can hold a main view, a security
monitor and a portal looking at the same world and needing different exposures,
which a single post-process value cannot express.

The precedence T0130 set: **this ticket owns the tonemap curve and any
auto-exposure, and auto-exposure writes `exposureEv100` rather than shadowing
it.** An exposure value held on the post-process stack as well would multiply
with the camera's, and the failure mode is that every individual number looks
reasonable while the image is wrong.

`hp::exposureMultiplierFromEv100` converts to the linear multiplier a shader
wants.


- **Ordering interactions already implied elsewhere, collected here:** fog
  (T0089) must apply in HDR before tonemap; T0093's visibility dimming is a
  material-level term so it is naturally pre-tonemap; UI (T0069) and debug
  draw are post-tonemap. If a pass cannot say which side of the tonemap it is
  on, that is a design smell.
- DiligentFX post-process components carry ImGui settings panels (see D6) —
  usable for free in the editor while tuning.
- Auto-exposure is deliberately deferred: it needs luminance
  reduction/histogram work and interacts with every lighting decision. A fixed
  exposure value per scene is the right starting point and is what most
  stylised games ship with anyway.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0111** owns the AA/render-scale decision and either confirms or drops the
  TAA-shaped hook 96.7 leaves open — its 111.2 names the full dependency list
  (motion vectors, jitter, history buffers, ordering against this tonemap).
  Check T0111's recorded decision before wiring anything into the hook.

### Cross-ticket obligation — T0130 (2026-08-05)

**Exposure ownership is contested between this ticket and T0130, and one of you
must own it.** A camera is where exposure naturally belongs conceptually — it is
a lens property in every physical model — while the HDR chain here is where it is
actually applied. T0130 asks for the decision to be recorded with a stated
precedence rather than each ticket assuming the other handles it.

The failure this prevents is concrete: exposure implemented in both places,
multiplying, and a scene that is correct only when one of them happens to be at
its neutral value.
