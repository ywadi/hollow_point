# T0078 — Settings and configuration

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 3 — Data model |
| **Order** | 360 |
| **Created** | 2026-08-03 |
| **Refs** | T0082, T0085, T0110, T0114 |

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

- [ ] Three separate stores, in appropriate locations
- [ ] All use the serialization util (T0020) — no bespoke formats
- [ ] Typed accessors with defaults, so a missing key is not a crash
- [ ] A corrupt file falls back to defaults with a warning, never blocks startup
- [ ] Game options apply at runtime where possible, not only on restart
- [ ] Project settings are editable in the editor
- [ ] User prefs and game options live outside the project directory

## Subtasks

- [ ] 78.1 Project settings in `.hpproj`, versioned (T0082)
- [ ] 78.2 User preferences in a per-user config location
- [ ] 78.3 Game options written beside the game's save data
- [ ] 78.4 Typed get/set with defaults and validation
- [ ] 78.5 Corrupt-file recovery
- [ ] 78.6 Change notification so systems react without polling
- [ ] 78.7 Editor panel for project settings
- [ ] 78.8 Command-line overrides for development
- [ ] 78.9 Display and graphics-quality section of game options -- see the
      2026-08-03 amendment below

## Notes / findings

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
