# T0076 — Autoloads: project and scene scoped services

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Created** | 2026-08-03 |

## Why

There is currently nowhere for user code that is **not attached to an entity** to
live. Behaviours are per-entity; systems iterate components. Neither covers "one
object that exists for the whole game" — a save system, a game-state manager, an
audio director, a settings service.

Godot calls these autoloads. Two scopes are needed:

| Scope | Lifetime | Example |
|---|---|---|
| **Project** | Created at startup, survives scene changes | save system, settings, game state, fog-of-war memory (T0093) |
| **Scene** | Created on scene load, destroyed on unload | level director, wave spawner |

## Done when

- [ ] User code registers an autoload; the engine constructs it at the right time
- [ ] Project autoloads survive scene transitions (T0077); scene autoloads do not
- [ ] Accessible from anywhere by type, cheaply
- [ ] Lifecycle callbacks: create, update, fixed update, destroy
- [ ] **Initialisation order is explicit** where autoloads depend on each other
- [ ] Configured as data — project autoloads in the project file, scene autoloads
      in the scene
- [ ] State survives a gameplay hot reload (T0048)
- [ ] The runtime (T0042) honours them identically to the editor

## Subtasks

- [ ] 76.1 `Autoload` base with the lifecycle callbacks
- [ ] 76.2 Registration from the gameplay module, alongside behaviours (T0062)
- [ ] 76.3 Project-scope container, created on project open
- [ ] 76.4 Scene-scope container, created and torn down with the scene
- [ ] 76.5 Typed access — `Autoload::Get<SaveSystem>()`
- [ ] 76.6 Explicit ordering, with a cycle check that fails loudly
- [ ] 76.7 Configuration in `.hpproj` and in the scene file (T0020)
- [ ] 76.8 Hot-reload survival via the same serialize/restore cycle as behaviours
- [ ] 76.9 Editor UI to add, remove and reorder autoloads
- [ ] 76.10 Tests, especially teardown order and scene-scope cleanup

## Notes / findings

**Autoloads are not entities.** Applying the same test as T0062: does it need to
exist in the scene — selectable, positioned, saved as a scene object? A save
system does not. Making autoloads entities would put them in the hierarchy, into
scene files, and into the export, for no benefit. Godot makes them nodes; that is
a consequence of everything in Godot being a node, not a virtue to copy.

**Teardown order is where this breaks.** Autoloads that reference each other must
be destroyed in reverse creation order, or a late destructor touches something
already gone. Cheap to get right at construction; a confusing crash otherwise.

**Scene-scope autoloads must be destroyed before the scene's entities**, since
they typically hold entity references (T0071). Destroying entities first leaves
the autoload resolving dangling references during its own teardown.

**Related but distinct: persistent entities.** "This entity survives a scene
change" (Unity's `DontDestroyOnLoad`) is a different feature and should not be
conflated with autoloads. If it is wanted, it belongs in T0077.

Global access is convenient and easy to overuse. Autoloads are for genuine
singletons — worth saying so where this is documented, because "just make it an
autoload" is how a codebase acquires twenty global objects.

### Architecture review (2026-08-03) — phase-spanning acceptance, and a T0095 tie

Two Done-when items reach into Phase 3: "configured as data" needs the
`.hpproj` format (T0024) and scene files (T0022), and project-scope creation
"on project open" needs the ProjectManager. The Phase 2 deliverable is the
registration/lifecycle/ordering mechanism with code-registered autoloads; the
data-driven configuration lands with Phase 3. Fine as long as it is known —
this note is so nobody blocks Phase 2 on it.

Also: `Autoload::Get<T>()` is a type-keyed registry reached from both the
engine and the gameplay module, which makes it exactly the kind of static
state T0095 exists to sort out. Same answer as behaviours: name-based
registration pushed from the module, single registry instance owned by the
engine, access through a context rather than a global if the linkage model
requires it.

### Second review pass (2026-08-03) — "created at startup" is ambiguous in the editor

"Project scope: created at startup" has one meaning in the runtime (process
start) and **two candidate meanings in the editor**: editor launch / project
open, or play-mode entry. They are not equivalent. If gameplay project
autoloads (game state, fog-of-war memory) are created at project open and
reused across play sessions, their mutated state survives stop — every
playtest starts from the previous one's state, which is precisely the
corruption play mode's scene clone exists to prevent.

The consistent rule: **project scope = game-session scope.** In the runtime a
session is the process; in the editor a session is one play-mode run (created
on play entry, destroyed on stop — see the matching note on T0037). Autoloads
do not run, and need not exist, while *editing* — same default as behaviours
(T0062's edit-mode policy). If some future autoload genuinely must run in the
editor, that is an explicit opt-in, not the default. Word 76.3 accordingly
("created when a game session starts", not "on project open").
