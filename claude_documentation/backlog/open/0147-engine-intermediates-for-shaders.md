# T0147 — Engine intermediates: scene depth, scene colour, and game-fed inputs for shaders

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 458 |
| **Created** | 2026-08-06 |
| **Refs** | [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) — its Done-when promises intermediates and no subtask delivers the sampled ones; [0093-visibility-and-fog-of-war.md](0093-visibility-and-fog-of-war.md) — the visibility field arrives with it, through this mechanism; [0094-gameplay-extensible-rendering.md](0094-gameplay-extensible-rendering.md) — 94.4/94.5 are the game-fed half of this; [../completed/0046-frame-render-targets.md](../completed/0046-frame-render-targets.md) — every target already carries `BIND_SHADER_RESOURCE`, and design-gaps item 8 flagged the scene-colour seam into it; [0096-hdr-pipeline-and-tonemapping.md](0096-hdr-pipeline-and-tonemapping.md) — sidedness rules; [0106-vfx-sprites-and-flipbooks.md](0106-vfx-sprites-and-flipbooks.md) — soft particles are this ticket's depth read; [0089-fog-and-atmospherics.md](0089-fog-and-atmospherics.md) — fog is another consumer |

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

- [ ] A surface or lighting-stage shader can sample **scene depth** during the
      transparent pass, and the worked example is a depth-fade (the soft
      particle read, proven before T0106 needs it)
- [ ] A shader can sample **scene colour** during the transparent pass — the
      snapshot point in the frame is decided and documented in
      `08-frame-anatomy.md`'s table (it is a new step in the 10.x sequence) —
      and the worked example is a refraction material
- [ ] The **game-fed input** mechanism exists with T0094: a texture a game
      layer produced is reachable from a material shader by declaration, and
      the fog-of-war dim in T0093's scenario is expressible with it
- [ ] Every intermediate documents **when it is valid** — which passes may
      read it, what it contains before its snapshot, and what an opaque-pass
      read of scene colour does (fails loudly, not garbage)
- [ ] D27's arrival rule is honoured and restated: **no `Visibility` field
      until T0093's mechanism exists**, no normal-roughness promise unless
      something produces it — each field names its owning ticket
- [ ] What is deliberately **not** offered is written down: this engine is
      forward-only (D24), so a G-buffer normal-roughness read either gets a
      cheap forward answer or an honest rejection — decided, not silent

## Subtasks

- [ ] 147.1 Depth SRV plumbing: which target, when it is complete, how the
      surface stage declares the read; placement recorded in frame anatomy
- [ ] 147.2 Scene-colour snapshot: where in 10.x it is copied (after opaque +
      sky, before transparents is the obvious point — decide against T0096's
      HDR ordering), full or half resolution decided by measurement
- [ ] 147.3 The contract fields and their validity docs — the
      `HpMaterial.slang` arrival table grows rows with owners, exactly as it
      already does for `ShadowFactor`/`Visibility`
- [ ] 147.4 Game-fed texture slots, designed with T0094.5 rather than beside
      it — one mechanism, referenced both ways
- [ ] 147.5 Worked examples with pixel assertions: depth-fade and refraction
      in the gpu suite
- [ ] 147.6 The normal-roughness disposition (offer a forward-friendly
      answer, or reject with the reasoning)

## Notes / findings

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
