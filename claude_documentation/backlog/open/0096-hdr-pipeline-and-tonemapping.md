# T0096 — HDR pipeline, tonemapping and the linear-workflow policy

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 460 |
| **Created** | 2026-08-03 |
| **Refs** | T0027, T0046, T0060, T0087, T0089, T0111 |

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

## Notes / findings

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
