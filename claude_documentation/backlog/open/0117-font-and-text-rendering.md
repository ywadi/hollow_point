# T0117 — Font and text rendering

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 495 |
| **Created** | 2026-08-03 |
| **Refs** | T0023, T0027, T0031, T0032, T0060, T0061, T0069, T0097, T0106, T0112, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D16 |

## Why

Text is needed well before Phase 12 and by things that are not the game UI, and no ticket currently owns drawing a glyph anywhere. Two tickets touch text and both explicitly scope it out:

- `backlog/open/0069-game-ui.md` (Phase 12, a placeholder epic — the library is not chosen) mentions fonts three times: once in its decision table ("text layout, input focus, scaling, localisation are each substantial"), once in its scope list ("Font handling, and a decision on localisation"), and once in its 2026-08-03 amendment, which keeps "fonts, shaping, text layout, and language-specific rendering" for itself.
- `backlog/open/0112-string-identity-and-localisation.md` explicitly scopes fonts *out*: "The expensive half of localisation is not fonts... T0069 keeps fonts, shaping and layout."

Both tickets agree fonts belong to T0069 — a Phase 12 epic whose library is undecided. That is fine for player-facing HUD/menu text. It is not fine for the things that need text well before Phase 12 and have nothing to do with player-facing UI:

- **Debug text.** T0061 (debug draw, Phase 4, Order 500) lists `DebugDraw::Text` among its required primitives — a text-rendering mechanism is presupposed there and owned nowhere.
- **Profiling overlays.** T0031 (profiling workflow, Phase 5) wants on-screen budgets and readouts, which is on-screen text.
- **In-world text.** Labels, damage numbers, signage — 3D content, not screen-space UI, with different rendering requirements (billboarding, depth, distance-based scaling) than a HUD.
- **Editor text.** ImGui already ships its own font atlas and text renderer (`Diligent-Imgui`, D6/D16). This ticket does not own or duplicate that; it is named here only so the scope boundary is explicit rather than assumed.

## What this ticket owns versus T0069

A "text stack" is several distinct layers, and different candidates solve different subsets of them:

| Layer | What it does |
|---|---|
| Font file loading | Reading a `.ttf`/`.otf` (or a pre-baked atlas) off disk, through the VFS (D13) |
| Glyph rasterisation | Turning an outline into pixels (or an SDF/MSDF field) at a given size |
| Atlas packing | Placing many glyphs into one texture, with lookup by codepoint |
| Shaping | Turning a string of codepoints into positioned glyphs — multi-glyph clusters, ligatures, non-Latin scripts (Arabic joining, Devanagari reordering) |
| Layout | Wrapping, alignment, line breaking |
| Rendering | Drawing the shaped, laid-out glyphs — screen-space (HUD) or world-space (in-world text) |

This ticket owns **font loading, rasterisation, atlas packing, and world-space/debug rendering** — the layers every consumer above needs regardless of what draws a menu. **T0069 owns UI-specific layout and shaping** (multi-line wrapping inside a panel, focus-aware text input) built on top of whatever this ticket produces, and the final choice of *UI library* (T0069's decision table: RmlUi, custom, Noesis, ImGui-restyled) may bring its own font stack that supersedes or wraps this one for player-facing text specifically — that choice is T0069's to make, not pre-empted here. Basic shaping (enough to lay out a fixed debug string or a damage number) belongs here; a full shaping engine for arbitrary UI text does not.

**SDF/MSDF is worth naming now because it constrains the atlas format this ticket decides.** Signed-distance-field text scales without re-rasterising — a single baked atlas renders correctly from a debug HUD's normal size up to a large in-world sign, and stays sharp at any camera distance for world-space labels, where a fixed-resolution bitmap atlas either blurs (magnified) or aliases (minified). MSDF sharpens corners that plain SDF rounds. Choosing bitmap-only now and wanting SDF later means re-baking every atlas and touching every consumer's shader; deciding the atlas format here, once, is cheap by comparison.

**The library question interacts with two other undecided things and should not pre-empt either.** D16 chose SDL3 as the platform layer, and SDL ships `SDL_ttf` as a companion library (FreeType-backed) — a plausible candidate for the loading/rasterisation layers this ticket owns, though its compatibility with the pinned SDL3 version and this project's cross-compile toolchain is unverified (see Notes). Separately, T0069's UI library choice is undecided and, per its decision table, "each candidate brings its own font stack" — RmlUi and Noesis both include text rendering. If T0069 lands on one of those, this ticket's output may end up feeding only debug/in-world text, with the UI library's own stack handling player-facing text. That is a legitimate outcome and should be recorded as a real possibility, not treated as this ticket failing to be adopted.

**Localisation constrains glyph coverage, and that is a font-stack question, not a string-identity one.** T0112 owns *string identity* — keys before literals — and is explicit that fonts are not its concern. But once a non-Latin language is in scope, the font(s) chosen must cover the needed glyph ranges, and shaping for scripts with joining/reordering behaviour (Arabic, Devanagari, Thai) is a rendering-layer problem no string-table key can solve. That belongs here, scoped to whatever this ticket decides to support (a fixed set of ranges, versus general Unicode coverage, is itself a decision this ticket should make and record rather than assume).

## Done when

- [ ] Font files load through the VFS (T0103/D13) — no direct filesystem access on a font path, matching every other asset read
- [ ] Glyphs rasterise into a packed atlas, and the atlas format decision (bitmap versus SDF/MSDF) is made and recorded, with the trade-off above weighed rather than defaulted
- [ ] `DebugDraw::Text` (T0061) and a basic in-world text primitive both render through this stack
- [ ] Basic layout — enough for a fixed debug string or a single- or multi-line label with wrapping and alignment — works without depending on T0069's UI library
- [ ] The library question is recorded as a decision, not silently resolved: what this ticket vendors or wraps for loading/rasterisation (candidates include `SDL_ttf`, `stb_truetype`, FreeType directly), how that interacts with D16's SDL3 choice, and an explicit note that T0069's eventual UI library may supersede or wrap this stack for player-facing text
- [ ] Glyph coverage is a stated, not implicit, scope — which Unicode ranges/scripts are supported, and what a missing glyph renders as (never a silent blank, matching T0112's convention for missing string-table keys)
- [ ] Editor text is confirmed out of scope — ImGui's own atlas continues to serve the editor, and this ticket does not duplicate it
- [ ] Works on both targets

## Subtasks

- [ ] 117.1 Evaluate and decide the loading/rasterisation library against D16's SDL3 choice — `SDL_ttf`, `stb_truetype`, or FreeType directly — and record the rejected options' costs, not just the winner
- [ ] 117.2 Atlas packing, and the bitmap-vs-SDF/MSDF format decision, including whether Diligent's `DynamicTextureAtlas` (already adopted for T0106's sprite sheets) is the right mechanism to reuse here rather than a second atlas implementation
- [ ] 117.3 Basic shaping sufficient for the scripts this ticket decides to support (117.6) — codepoint-to-glyph mapping and cluster positioning, explicitly not a full shaping engine
- [ ] 117.4 Basic layout: wrapping, alignment, line breaking — the subset needed by debug text and simple labels, not a UI layout system
- [ ] 117.5 Rendering paths: screen-space (feeding T0061's `DebugDraw::Text` and T0031's overlays) and world-space (billboarded or oriented, feeding in-world labels/damage numbers/signage), both through the RenderStack (T0027)
- [ ] 117.6 Decide and record glyph coverage scope, informed by whatever T0112 has settled on string identity — coverage is a font/atlas cost decision for the engine (Latin-1 and CJK differ by orders of magnitude in atlas size and shaping need), not a per-game one
- [ ] 117.7 Missing-glyph behaviour: a visible placeholder (tofu box or similar), never a silent blank — matching T0112's convention for missing string keys
- [ ] 117.8 Confirm cross-compilation to `x86_64-windows-gnu` for whatever library is chosen (G2/G3/G4 history — this has broken other dependencies before)

## Notes / findings

**The ordering problem this ticket exists to fix mirrors T0112's.** T0112 pulled string identity out of T0069 because Phase 3-7 authoring cannot wait for a Phase 12 UI epic. The same argument applies to text *rendering*: T0061 sits at Order 500, inside Phase 4, and its Text primitive has nowhere to render from without this ticket. Leaving text rendering inside T0069 means Phase 4's debug draw either ships without Text or grows an ad-hoc renderer this ticket then has to reconcile with later — the retrofit T0112's Why section already warned about, for the same reason.

**Premultiplied alpha is already a project convention for sprite-shaped rendering** (T0106's amendment to T0097) — glyph quads are the same shape of problem (alpha-blended textured quads) and should default to the same convention rather than rediscovering the halo artefact independently.

**Do not build a general 2D UI text-layout engine here.** T0106's own note on the equivalent boundary — "do not build a general 2D sprite renderer... if HUD sprites are ever wanted, that is T0069's problem" — is the right model for this ticket too: own the glyph pipeline, not the UI.

**Unverified:** whether `SDL_ttf` is compatible with the pinned SDL3 version and cross-compiles cleanly under this project's zig toolchain has not been checked as part of writing this ticket. D16 confirms SDL3 itself is vendored and cross-compiles; `SDL_ttf` is a separate library with its own FreeType dependency and should be verified before being assumed as the answer to 117.1.
