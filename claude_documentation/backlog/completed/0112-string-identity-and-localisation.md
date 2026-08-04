# T0112 — String identity: keys before literals

| | |
|---|---|
| **Status** | ✅ DONE |
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

- [x] The decision is made and recorded: user-facing strings are **keys**
      resolved through a string table, or the game **explicitly ships
      English-only** with literals in data. Either is a legitimate answer; the
      absence of an answer is not, because it is being answered implicitly by
      every string authored
- [x] If keys: the convention is written into
      `documentation/06-engine-conventions.md` as a short section -- key naming
      shape, where the English table lives, and missing-key behaviour (visible
      placeholder like `[item.rusty_key.name]`, never silently blank)
- [x] Confirmed that no engine machinery is needed *now* beyond the convention
      -- a string table is a plain asset through T0020/T0023 when first needed;
      this ticket must not grow a text-rendering or localisation system
- [x] T0069 keeps fonts, shaping and layout; its localisation bullet points
      here for the identity half (see the amendment there)
- [x] The engine-building-order vs release-order observation above is recorded
      where the phase plan lives, so Phase 8 "ships a game" is not read as a
      promise of shippable menus

## Subtasks

- [x] 112.1 Decide keys-vs-literals and record it. Small, and an **engineering**
      decision despite looking like a product one: literals become a mechanical
      migration across every call site that exists by then, keys cost one table
      from the start. That asymmetry decides it, not which languages any game
      ships in
- [x] 112.2 If keys: write the convention -- namespaced dotted keys
      (`item.rusty_key.name`-shaped), one authoritative English table, visible
      missing-key fallback
- [x] 112.3 Note the early consumers explicitly so they comply from the start:
      T0068's key display names, T0083's slot metadata, and any authored
      component string in Phases 3-7
- [x] 112.4 Record the phase-ordering clarification (engine-building order,
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

### Closed 2026-08-04 — the decision, and the argument that settled it

**Keys.** The convention is
[`../../documentation/06-engine-conventions.md`](../../documentation/06-engine-conventions.md),
section "Player-facing text is a key, never a literal".

112.1 framed this as an engineering decision on the migration asymmetry, and that
is right, but the asymmetry alone is not what closed it — a studio that knows it
ships English-only could reasonably take the cheaper path. What closed it is that
**this engine is not the thing entitled to make that call.** It powers several
games (T0109); whether any given one localises is that game's decision, taken
later, by people who will not revisit the engine's data format. Literals in the
engine's authored data mean every future game inherits English-only and the first
one that wants otherwise pays for all of them. Keys mean an English-only game
ships a table whose values are English, which costs nothing. The decision is about
what the engine forecloses, not about which languages get funded — which is
exactly why it is not the owner's call to make and did not need to wait on one.

**Scope discipline held: nothing was built.** No resolver, no table loader, no
type. Verified by survey rather than assumed — a case-insensitive grep for
`localis|localiz|i18n|string table|stringtable` across `engine/ apps/ samples/
tools/` (headers, sources, CMake and Python) returned **zero hits** on
2026-08-04, and there is no `displayName`/`keyName` API anywhere in
`engine/include/hp/`. Clean slate confirmed, which is what made the third
Done-when honestly tickable rather than merely asserted.

**The one thing I considered building and did not.** A distinct `hp::StringKey`
type would let reflection, the inspector and a future resolver tell a key from
prose — real value, and cheap. It is not here because it currently has no
consumer: T0021's core components (transform, mesh, material, camera) carry no
player-facing text, and the entity tag is an editor label rather than something a
player reads. Building it now would be designing for a hypothetical. The decision
that it *will* be a distinct type when the first such field appears is recorded
in the convention and on T0021, so the question is settled without the code
existing.

**Where the obligations landed.** T0069, T0083, T0020 and T0117 already
referenced this ticket; the two that mattered most did not, and now do:

- **T0021** — the ticket that starts authoring data, and the reason this one was
  done first. Carries the tag-is-not-a-key rule and the no-machinery-needed
  finding.
- **T0035** — the inspector must show *resolved* text and edit the key. This is
  the convention's real failure mode and it is behavioural, not technical: an
  inspector showing raw keys makes authoring miserable, and people who cannot see
  what they are typing paste literals to stay sane.
- **T0020** — owns the table's file format, since the table is an ordinary asset.

**112.4** is recorded in the backlog README's Phases section rather than in
T0069: phase numbers are engine-building order, so Phase 8 "Runtime & export"
sitting four phases before the UI system is intended and is not a promise that a
game ships at phase 8.

**Not verified, because it is not verifiable yet:** nothing here is enforced by a
check. A literal authored into a scene next month will not fail CI, because there
is no scene format and no linter that knows which fields are player-facing. The
convention is currently held by documentation and by the two cross-references
above. If that proves insufficient once real content exists, the enforcement point
is whatever validates authored assets — not a grep over source, which cannot
distinguish a player-facing literal from any other string.
