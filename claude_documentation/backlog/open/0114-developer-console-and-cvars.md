# T0114 — Runtime developer console and cvars

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Low |
| **Complexity** | Moderate |
| **Phase** | 6 — Editor |
| **Order** | 675 |
| **Created** | 2026-08-03 |
| **Refs** | T0048, T0054, T0061, T0066, T0068, T0078, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D14, [../../documentation/07-design-gaps.md](../../documentation/07-design-gaps.md) item 12 |

## Why

T0066 is a log *viewer* -- filter, search, counts. `console command`, `cvar`,
`cheat` -- zero hits at survey time (2026-08-03). Nothing anywhere provides
"type a command, tweak a value, spawn a thing" at runtime, in the editor or in
a dev build of the game. T0078.8's command-line overrides are the same idea at
process start only.

This is not structural -- it can be added at any time without touching an
interface -- which is why it is Low priority. It is filed because it is the
tool that makes every *other* system debuggable, and engines that lack one
grow five ad-hoc debug keybinds per system instead. A ticket makes it a
decision rather than an accident.

**D14 boundary, stated up front:** there is no scripting language in this
engine, and a console must not become one by stealth. A console here is a
command registry -- names bound to C++ callbacks, registered by stable name
exactly the way behaviours are (D14) -- plus a cvar registry of typed, named
values. Command lines are tokenised, not evaluated; no expressions, no control
flow. If a use case seems to need scripting, it is gameplay code per D14.

## Done when

- [ ] A cvar registry exists: typed values with name, default, description,
      enumerable, settable from the console and from the T0078.8 command line
      through one mechanism rather than two
- [ ] A command registry exists: stable name → C++ callback with tokenised
      arguments; registration API usable from engine and gameplay module alike
- [ ] The editor has a command line, in or beside T0066's console panel, with
      completion over registered names
- [ ] The dev *game* build question is decided: whether the runtime gets an
      in-game console overlay, and what it renders with (T0061's debug text is
      the honest floor and is already compiled out of shipping builds, which
      is the right gate)
- [ ] Cvars and T0078 settings do not become two competing stores: a cvar can
      shadow a setting for the session, but persistence stays T0078's --
      recorded as a rule
- [ ] Commands registered by the gameplay module are unregistered on hot
      reload (T0048) -- same dangling-callback hazard T0068's review note
      records for action callbacks
- [ ] Shipping builds: compiled out or hard-disabled, decided explicitly --
      a console is a cheat surface, and the answer should be chosen rather
      than inherited

## Subtasks

- [ ] 114.1 Cvar registry: type set (bool/int/float/string), defaults,
      descriptions, change callbacks
- [ ] 114.2 Command registry and the tokeniser -- deliberately no expression
      language (D14)
- [ ] 114.3 Editor command line in the console panel (T0066), with history and
      completion
- [ ] 114.4 Decide and, if yes, build the dev-build in-game overlay
- [ ] 114.5 The T0078 interaction rule: session shadowing, no second
      persistence path
- [ ] 114.6 Module registration lifecycle across hot reload (T0048)
- [ ] 114.7 Ship-build policy and its enforcement

## Notes / findings

- The value concentrates in the registries, not the UI. Once cvars exist,
  T0078.8's command-line overrides, an ImGui tweak panel, and the console are
  all views over the same table -- which is the argument for doing the registry
  once instead of three ad-hoc mechanisms.
- Worth stealing the convention every id-Software-descended engine converged
  on: commands and cvars share one namespace and one lookup, so "help" and
  completion cover both.
