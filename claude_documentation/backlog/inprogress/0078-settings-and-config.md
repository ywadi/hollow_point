# T0078 — Settings and configuration

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 3 — Data model |
| **Order** | 360 |
| **Created** | 2026-08-03 |
| **Refs** | T0082, T0085, T0110, T0114 , [../completed/0129-display-modes-and-window-control.md](../completed/0129-display-modes-and-window-control.md) , [../completed/0110-presentation-and-frame-pacing.md](../completed/0110-presentation-and-frame-pacing.md) |

## Why

Three distinct kinds of configuration currently have no home, and they are
routinely conflated — which is how a user's graphics preference ends up committed
to the project, or a project setting gets overwritten per machine.

| Kind | Scope | Example |
|---|---|---|
| **Project settings** | Committed, shared by the team | startup scene, autoloads, physics tick rate |
| **User preferences** | Per machine, never committed | window size, editor layout, recent projects |
| **Game options** | Set by the player at runtime | resolution, volume, keybinds |

## Done when

- [~] Three separate stores, in appropriate locations — **one mechanism built** (`SettingsStore`), which is all three; the *locations* are not resolved yet
- [x] All use the serialization util (T0020) — no bespoke formats; `SettingsStore` is `YamlDocument` underneath
- [x] Typed accessors with defaults, so a missing key is not a crash — **no getter can fail**: missing key, wrong type and unparsed file all return the fallback
- [x] A corrupt file falls back to defaults with a warning, never blocks startup — asserted, including that the store stays usable afterwards
- [ ] Game options apply at runtime where possible, not only on restart
- [ ] Project settings are editable in the editor
- [ ] User prefs and game options live outside the project directory

## Subtasks

- [~] 78.1 Project settings — **the store exists and round-trips through the filesystem**; the `.hpproj` location and versioning (T0082) do not
- [ ] 78.2 User preferences in a per-user config location
- [ ] 78.3 Game options written beside the game's save data
- [x] 78.4 Typed get/set with defaults — bool, int, float, string, over **dotted keys** that nest in the file
- [x] 78.5 Corrupt-file recovery — a missing file is *not* an error; an unparseable one is reported, logged and survived
- [ ] 78.6 Change notification so systems react without polling
- [ ] 78.7 Editor panel for project settings
- [ ] 78.8 Command-line overrides for development
- [ ] 78.9 Display and graphics-quality section of game options -- see the
      2026-08-03 amendment below

## Built 2026-08-05 — the store and the layer names, not the rest

**Picked up out of order, to unblock T0085.** T0085 was closed and reopened the
same day because its headline requirement — *"layers are named in project
settings, not bare numbers"* — was unmet, and moving it here was justified on
"blocked on something that does not exist". That did not hold up: this ticket is
**order 360**, *earlier* than T0085's 430, and complexity Simple. So it was
worked rather than used as a parking space.

`engine/include/hp/Settings.hpp`, `engine/src/Settings.cpp`,
`tests/fast/settings_test.cpp`. **220 fast and 89 integration green on both
targets**, twelve new cases.

### What was built

- **`SettingsStore`** — a typed key-value store over `YamlDocument`, with
  **dotted keys** (`render.shadows.enabled`) that create nested maps, so a
  settings file reads as a grouped document rather than a flat list of long keys.
- **Every getter takes a fallback and none can fail.** A missing key, a key of
  the wrong type and a file that would not parse all produce the fallback. That
  is the property that makes it impossible for a config file to break a program
  reading it correctly, and it is what 78.4 and 78.5 actually mean.
- **A missing file is not an error.** A project that has never saved settings is
  the normal state of a new project; `load` returns true and leaves defaults.
  Only a file that *exists and will not parse* returns false — and even then the
  store stays usable, which is asserted.
- **`LayerNames`** — the 32-entry table, stored under `layers` as a **sequence**
  so the file reads as names in layer order.

### Three decisions worth arguing with

**One store class, not three.** Project settings, user preferences and game
options differ only in *where the file lives and who may commit it*. Three
classes would triple the corrupt-file behaviour and the defaults, which is how
three slightly different ideas of "missing key" appear.

**No singleton.** Whoever owns the project owns its settings — T0024's
`ProjectManager` when it exists. A global would be a second lifetime to
invalidate on project switch.

**An unknown layer name returns -1, never 0.** Returning 0 would put the object
on the default layer, which is visible to everything — so a typo would not fail,
it would quietly do the most permissive possible thing. `LayerNames::mask` skips
unknown names **and logs**, because a typo in a mask is otherwise
indistinguishable from a deliberately narrow one.

**Gaps are written, not skipped.** Position *is* the layer index, so an unnamed
layer 1 still occupies a slot in the sequence. Omitting it would silently
renumber every layer after it, and a scene authored before the change would light
the wrong objects. Trailing unnamed layers *are* dropped, so a project naming
three layers gets three lines rather than 32.

### Not done, and this ticket stays open for it

The store is the foundation; most of this ticket is what sits on top, and each
piece needs a consumer that does not exist:

- **78.2 / 78.3 — user preferences and game options have no *location*.** The
  mechanism works; nothing resolves a per-user config directory or a save-data
  directory. `hp/Paths.hpp` currently offers only `executableDirectory()`.
- **78.6 — no change notification.** Nothing polls settings yet, so there is
  nothing to notify. Adding it now would be a callback list with no callers.
- **78.7 — no editor panel.** T0032 does not exist.
- **78.8 — no command-line overrides.**
- **78.9 — no quality tiers or display section.** This ticket's own amendment
  says the tier set is *"decided when the first consumer lands, not invented
  here"*, and none has landed. T0110's vsync/cap and T0129's resolution are
  built and still have nowhere for a player to set them.
- **`.hpproj` and schema versioning** (T0082) are not addressed.

### One thing T0085 wanted that is deliberately not done

**Layers still serialise as an integer, not as names.** The `writeLeaf` branch in
`Serialize.cpp` carries a comment marking the line. Writing names requires
threading the `LayerNames` table into the serializer, which has no owner today —
that is T0024's `ProjectManager`, and inventing a global to reach it is the
singleton this ticket just declined. The Done-when it satisfies is the one that
matters: **code can say `names.indexOf("Player")` instead of `7`.**

## Notes / findings

### Inherited from T0085 (2026-08-05) — layers need names, and nothing else will give them names

**85.1 moved here**, and it is the single biggest gap T0085 left. The layer
*mechanism* is built — `hp::LayerMask`, 32 layers, honoured in `parseScene` — but
there is **no name table**, so `layer 7` appears in code as `7`.

T0085's own Done-when says *"named in project settings, not bare numbers"*, and
that is unmet until this ticket exists. What it needs to provide:

- A fixed table of up to `hp::kMaxLayers` (32) names, in project settings.
- Enough for an inspector (T0032) to show a multi-select of names rather than an
  integer, and for `Serialize.cpp` to write **names instead of numbers** — the
  `writeLeaf` branch for `LayerMask` carries a comment marking the line to
  change.

The same names are shared by cameras (T0085), lights (T0079), shadows (T0086) and
physics collision layers (T0051), so this table is the single definition all four
read. See [../completed/0085-layers-and-masks.md](../completed/0085-layers-and-masks.md).

**Getting the split wrong is the actual risk here**, and it shows up as friction
rather than bugs: user preferences committed to the repo cause churn and merge
conflicts on every window resize; project settings stored per-user mean the team
silently runs different physics rates.

The test: *would another developer on this project want this value?* Yes → project.
No → user.

**Never block startup on a config file.** A malformed preferences file that
prevents the editor launching is infuriating and entirely avoidable — log, fall
back to defaults, carry on, and ideally rename the bad file aside so it is not
lost.

Command-line overrides are worth having early; they make automated testing and
bug reproduction dramatically easier.

### Amendment (2026-08-03) -- the quality settings four tickets cite must exist here

Four rendering tickets wire themselves into a quality section of this ticket
that this ticket did not have: T0086.9 "Quality settings wired to project/user
config (T0078)", T0091.7 "Quality tiers driven by settings (T0078)", T0089's
"the fallback on lower quality settings", T0096.7 "behind quality settings
(T0078)". The game-options row above says "resolution, volume, keybinds" and no
subtask mentioned graphics tiers. Four consumers, no producer -- found by the
design-gap survey (`documentation/07-design-gaps.md`, item 4).

Nothing here is hard; the risk is drift -- each rendering ticket inventing its
own quality enum, then a settings screen trying to unify four shapes. 78.9
closes it, and its scope is:

- **Display options**, populated by T0110 (presentation): vsync, frame-rate
  cap, fullscreen/borderless (applied by T0015), resolution -- the "resolution"
  already listed stops being alone
- **A small set of named quality tiers** (the exact set is decided when the
  first consumer lands, not invented here) that the four tickets above key
  off, plus the policy for **per-system overrides**: tiers set defaults,
  individual toggles may override them, and the file records overrides rather
  than a mutated tier
- **Gamma/brightness** -- `gamma` and `brightness` had zero hits anywhere at
  survey time; a player-facing brightness control is standard and belongs in
  this same section (its render-side application is T0096's business)

The shape to avoid: quality enums defined in T0086/T0089/T0091/T0096 and
merely *stored* here. The schema is this ticket's; the consumers read it.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0085.1**: layer *names* live in project settings — the same shape as the
  quality-tier rule above: the schema is this ticket's, T0085's mask-editor
  widget reads it, and `layer 7` never appears in code.
- **T0114**: cvars may *shadow* a setting for the session, but persistence
  stays here — one store. The rule is recorded on T0114's side; this side must
  not grow a second persistence path when the console arrives.

### Cross-ticket obligation — T0129 (2026-08-04)

This ticket lists **resolution** as a player-facing setting, and until T0129
lands nothing can apply it: the window has no runtime resolution change, no
fullscreen and no monitor selection. T0015 spotted exactly this — "so *something*
must implement applying it" — and that something was never built.

Do not design the display section of the options as though the mechanism exists.
Agree the settings shape and the window API together, or the options UI offers
modes the window cannot enter.

### Cross-ticket obligation — T0110.5 (2026-08-04)

**T0110 closed with one item deferred here: the display section of the options.**
Vsync and the frame-rate cap are implemented and measured; what does not exist is
a place for a player to set them.

Two findings constrain the UI before it is designed:

- **Vsync is a boolean and cannot be anything else.** Diligent derives the
  present mode from it and exposes no way to choose — on prefers
  `FIFO_RELAXED` then `FIFO`, off prefers `MAILBOX`, `IMMEDIATE`, `FIFO`. So an
  "advanced" raw present-mode selector **cannot be offered without patching
  Diligent**. Do not put one in a mockup.
- **"Vsync on" is not a no-tearing promise.** `FIFO_RELAXED` shows a late frame
  immediately. Labelling the option "no tearing" would be false on this engine.

The cap is a separate control and genuinely independent: measured 121 fps with
vsync on versus 4,000-5,200 fps with it off, so the cap is what stops a menu or a
background window rendering flat out. Offer off/30/60/120/custom, and remember
the background cap is a second value (T0110 defaults it to 10 Hz).

`resolution` in this ticket's list is **T0129**'s mechanism, now built — see the
separate obligation above.