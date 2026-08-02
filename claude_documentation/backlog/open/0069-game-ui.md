# T0069 — Game UI system

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 11 — Game UI |
| **Created** | 2026-08-03 |

> **Placeholder epic.** The library is not chosen. Recorded because it is a real
> gap with no owner, not because it is ready to start.

## Why

**Dear ImGui never ships.** It is the editor's UI (T0032) and the editor is
explicitly not part of the game. So HUD, menus, inventory and dialogue have no
system at all — the RenderStack (T0027) reserves a UI layer, but nothing draws
into it.

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
