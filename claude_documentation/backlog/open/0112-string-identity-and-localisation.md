# T0112 — String identity: keys before literals

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 3 — Data model |
| **Order** | 185 |
| **Created** | 2026-08-03 |
| **Refs** | T0020, T0022, T0068, T0069, T0083, [../../documentation/07-design-gaps.md](../../documentation/07-design-gaps.md) item 3 |

## Why

All player-facing text currently lives as two bullets inside T0069, a Phase 12
placeholder epic whose UI library is undecided: "text layout, input focus,
scaling, localisation are each substantial" and "Font handling, and a decision
on localisation". `glyph`, `text render`, `freetype`, `msdf`, `subtitle`,
`i18n`, `string table` -- zero hits anywhere else at survey time (2026-08-03).
Debug text exists (T0061's `DebugDraw::Text`) but is compiled out of shipping
builds by its own Done-when.

The expensive half of localisation is not fonts, and it cannot wait for
Phase 12. **It is string identity.** Phases 3-7 author components, prefabs and
scenes; every user-facing string that lands in that data as a literal
("Rusty Key", "Door locked") is a migration when localisation arrives. A
string-table key costs nothing extra *if the convention exists when authoring
starts* -- which is why this ticket sits at the head of Phase 3, before the
first scene is saved, and why it is a data-model ticket rather than a UI one.

Two consumers hit the question well before Phase 12: input rebinding (T0068)
shows key names in UI, and save-slot metadata (T0083) carries display strings.

This ticket also records an ordering observation the survey made: Phase 8
"ships a game" (T0042 runtime, T0043 export, T0083 saves implying a load menu)
four phases before the system that can draw a menu exists (T0069, Phase 12).
That may be intended -- phases are engine-building order, not release order --
but nothing currently says so.

## Done when

- [ ] The decision is made and recorded: user-facing strings are **keys**
      resolved through a string table, or the game **explicitly ships
      English-only** with literals in data. Either is a legitimate answer; the
      absence of an answer is not, because it is being answered implicitly by
      every string authored
- [ ] If keys: the convention is written into
      `documentation/06-engine-conventions.md` as a short section -- key naming
      shape, where the English table lives, and missing-key behaviour (visible
      placeholder like `[item.rusty_key.name]`, never silently blank)
- [ ] Confirmed that no engine machinery is needed *now* beyond the convention
      -- a string table is a plain asset through T0020/T0023 when first needed;
      this ticket must not grow a text-rendering or localisation system
- [ ] T0069 keeps fonts, shaping and layout; its localisation bullet points
      here for the identity half (see the amendment there)
- [ ] The engine-building-order vs release-order observation above is recorded
      where the phase plan lives, so Phase 8 "ships a game" is not read as a
      promise of shippable menus

## Subtasks

- [ ] 112.1 Decide keys-vs-literals with the project owner and record it (this
      is small but is a product-adjacent decision, like T0044)
- [ ] 112.2 If keys: write the convention -- namespaced dotted keys
      (`item.rusty_key.name`-shaped), one authoritative English table, visible
      missing-key fallback
- [ ] 112.3 Note the early consumers explicitly so they comply from the start:
      T0068's key display names, T0083's slot metadata, and any authored
      component string in Phases 3-7
- [ ] 112.4 Record the phase-ordering clarification (engine-building order,
      not release order) in the backlog README or T0069

## Notes / findings

- The survey's framing is the right test: "a string-table key costs nothing
  extra if the convention exists when authoring starts". The entire value of
  this ticket is timing; done in Phase 12 it is worthless, because by then the
  strings exist.
- English-only is a real option and choosing it deliberately is fine -- the
  rejection gets recorded and the absence stops being a gap. What is not fine
  is deciding by accumulation of literals.
- Fonts remain the reason T0069's library choice cannot drift forever (every
  candidate UI library has its own font stack); that pressure is recorded in
  T0069, not here.

### Amendment (2026-08-03) -- text rendering (not string identity) is T0117

This ticket's scope is unchanged: string identity, not fonts. T0117 was filed
to own the rendering half the survey flagged as also unowned -- font loading,
rasterisation, atlas packing, and world-space/debug rendering -- for the
systems that need text before Phase 12 (T0061's debug text, T0031's overlays,
in-world labels). Glyph coverage for the languages this ticket's localisation
decision eventually names is T0117's concern, not this one's.
