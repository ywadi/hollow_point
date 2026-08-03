# T0069 — Game UI system

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 12 — Game UI |
| **Order** | 880 |
| **Created** | 2026-08-03 |

> **Placeholder epic.** The library is not chosen. Recorded because it is a real
> gap with no owner, not because it is ready to start.

## Why

**Dear ImGui is not the game's UI.** It is the editor's UI (T0032) and the
editor is explicitly not part of the game. So HUD, menus, inventory and
dialogue have no system at all — the RenderStack (T0027) reserves a UI layer,
but nothing draws into it.

(Precision, from the architecture review 2026-08-03: ImGui *code* does ship —
DiligentFX links `Diligent-Imgui` PUBLIC and its post-process components call
into it (D6), so the runtime binary carries ImGui regardless. The rule this
ticket defends is that ImGui must never be the *player-facing* UI, not that
its symbols are absent. T0042's "no editor symbols" check is worded
accordingly.)

This is easy to overlook precisely because the editor is full of working UI.

## The decision to make

| Option | Trade-off |
|---|---|
| **RmlUi** | HTML/CSS-like retained UI, permissive, built for games. Real layout, styling and fonts out of the box. A dependency with its own concepts to learn. |
| **Custom, on the RenderStack** | Total control, no dependency, exactly the features needed. UI systems are deceptively large — text layout, input focus, scaling, localisation are each substantial. |
| **Noesis (XAML)** | Very capable with strong designer tooling. Commercial licence. |
| **ImGui anyway, restyled** | Tempting because it is already integrated. Immediate mode fights retained UI needs (animation, transitions, layout), and it is not built for shipping player-facing UI. |

Deciding needs to know how UI-heavy the game is — a HUD with three elements and a
menu is a very different problem from an inventory-driven RPG.

## Rough scope

- [ ] Choose the approach and record it in the decision log
- [ ] Confirm it cross-compiles to `x86_64-windows-gnu` (G2/G3/G4 history)
- [ ] Render into the RenderStack's UI layer, above the world (T0027)
- [ ] Input via the input context stack (T0068), not raw events
- [ ] Resolution and DPI independence
- [ ] Font handling, and a decision on localisation
- [ ] Bind UI to gameplay state without coupling UI code into systems

## Notes / findings

**Nothing in Phases 1-8 is blocked by this**, which is why it can wait. But the
RenderStack's UI layer should be designed so that *something else* draws into it —
not so that ImGui does, with a later swap.

**Depth and ordering:** UI must not depth-test against the world, and
post-processing (tonemapping, bloom) must apply to the world layer *before* UI is
drawn over it — otherwise the HUD looks washed out. T0027 already flags this; it
is the concrete reason it matters.

Resist "we will just use ImGui for now". Player-facing UI written in immediate
mode is very hard to make feel good, and the migration later is a rewrite.

### Amendment (2026-08-03) -- from the design-gap survey

Three adjustments, from `documentation/07-design-gaps.md` items 3, 11 and 15:

- **String identity is pulled out of this epic into T0112**, which decides
  keys-versus-literals *before* Phase 3-7 authoring, because every literal
  authored ahead of that decision is a migration. What stays here is the half
  that genuinely belongs to a UI system: fonts, shaping, text layout, and
  language-specific rendering. Fonts are also the standing reason the library
  choice above cannot drift forever -- each candidate brings its own font
  stack, and no text can be drawn until one is picked.
- **Two accessibility items join the decision list.** UI scale as a *user
  option* -- distinct from the DPI correctness already in scope; a player with
  low vision wants "make everything bigger" regardless of DPI -- and a
  colourblind-safe palette policy for HUD/UI colour semantics. Both are far
  cheaper as requirements on an unbuilt system than as patches to a built
  one. (Input rebinding, the highest-value accessibility feature, is already
  fully owned by T0068 and is not re-raised.)
- **HUD anchoring inherits the aspect-ratio policy** T0044 decides (T0081's
  amendment has the projection half): whatever the answer -- free aspect,
  letterbox, clamped FOV -- HUD elements anchor to screen edges/corners under
  it, and a safe-area inset is the cheap generalisation to carry.

### Amendment (2026-08-03) -- text rendering split into T0117

T0117 now owns font loading, glyph rasterisation, atlas packing, and
world-space/debug text rendering -- pulled out for the same reason T0112
pulled string identity out of this epic: debug text (T0061), profiling
overlays (T0031) and in-world labels need a text stack well before Phase 12,
and building one ad hoc under those tickets would only be redone here. What
stays in this epic: UI-specific layout and shaping (multi-line wrapping
inside a panel, focus-aware input) built on top of T0117's stack, and the UI
library choice above, which may bring its own font stack that supersedes or
wraps T0117's for player-facing text specifically.
