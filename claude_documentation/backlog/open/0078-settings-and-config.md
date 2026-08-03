# T0078 — Settings and configuration

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 3 — Data model |
| **Order** | 360 |
| **Created** | 2026-08-03 |

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
