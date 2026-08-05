# T0076 — Autoloads: project and scene scoped services

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Order** | 350 |
| **Created** | 2026-08-03 |
| **Refs** | [../../documentation/09-gameplay-authoring.md](../../documentation/09-gameplay-authoring.md), [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D23, T0062 (shares the reload cycle), T0022, T0024, T0037, T0077 |

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

**Rescoped 2026-08-05 against D23.** These are now `hp::Service` — the same
type, the same callbacks as a behaviour, differing only in **lifetime** and
**where configuration lives**. The shape is
[`09-gameplay-authoring.md`](../../documentation/09-gameplay-authoring.md); the
name "autoload" survives only in this ticket's title.

- [ ] `hp::Service` shares `hp::Behaviour`'s callbacks (`ready`, `process`,
      `physicsProcess`, `lateProcess`, `destroyed`) — one concept, three
      lifetimes
- [ ] `HP_SERVICE(Type, Scope::Session|Scope::Scene, fields...)` registers with
      **no plumbing in the gameplay file**
- [ ] **Session** services live for one play-mode run; **scene** services live
      from scene load to unload (T0077)
- [ ] Accessible as `hp::service<T>()`, resolving to a pointer cached at load —
      no per-access lookup
- [ ] **Initialisation and teardown order come from the language**, not from a
      runtime sort — see the rescope note
- [ ] Configured as data — session services in the project file (T0024), scene
      services in the scene (T0022)
- [ ] State survives a gameplay hot reload (T0048) — **the same mechanism as
      T0062.6**
- [ ] Teardown runs inward-out: scene services → entities → session services
- [ ] The runtime (T0042) honours them identically to the editor

## Subtasks

- [ ] 76.1 `hp::Service` base, sharing `hp::Behaviour`'s callback surface
- [ ] 76.2 Registration from the gameplay module, through the same intrusive-list
      mechanism as behaviours (T0062.3)
- [ ] 76.3 Session scope, created when a **game session** starts — play-mode
      entry in the editor, process start in the runtime. Not project open
- [ ] 76.4 Scene scope, created and torn down with the scene
- [ ] 76.5 `hp::service<T>()`, resolved to a cached pointer
- [ ] 76.6 ~~Explicit ordering with a cycle check~~ — **deleted.** Members of one
      root construct in declaration order and destruct in reverse, guaranteed by
      C++; a dependency cycle is a compile error. See the rescope note
- [ ] 76.7 Configuration in `.hpproj` and in the scene file (T0020, T0022)
- [ ] 76.8 Hot-reload survival — **shared implementation with T0062.6**, not a
      second mechanism
- [ ] 76.9 Editor UI to add, remove and configure services
- [ ] 76.10 Tests: teardown order across the two scopes, scene-scope cleanup,
      and that a session service does **not** leak state between playtests

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

### Re-phased 2 → 3 (2026-08-03)

The two review notes above already establish that this ticket's substance lives
in Phase 3: "configured as data" needs the `.hpproj` format (T0024) and scene
files (T0022), and the second pass redefined project scope as *game-session*
scope — semantics that only mean anything once scene loading and transitions
(T0077) exist to survive. Registration alongside behaviours (76.2) also follows
T0062, itself re-phased to 3. So the phase field now matches, and the Order
field places this after T0077.

Two acceptance items still reach further out, deliberately: 76.9 (editor UI) is
Phase 6 surface, and "the runtime (T0042) honours them identically" is a Phase 8
verification. Both are the usual phase-spanning-acceptance pattern (compare
T0062's editor items) — they close late without moving the ticket again.

### Rescoped 2026-08-05 — autoloads become `hp::Service`, and half the ticket dissolves

**D23** and
[`09-gameplay-authoring.md`](../../documentation/09-gameplay-authoring.md)
replace the type-keyed registry this ticket specified with **one root object per
scope**, sharing `hp::Behaviour`'s callback surface. The "autoload" name survives
only in the title.

**76.6 is deleted outright, and this is the substance of the rescope.** The
ticket's own Notes call teardown ordering the place where this breaks —
*"autoloads that reference each other must be destroyed in reverse creation
order, or a late destructor touches something already gone."* Members of a
single root **construct in declaration order and destruct in reverse, guaranteed
by the language**, and a dependency expressed as a constructor argument makes a
cycle a *compile* error. There is nothing left for a runtime topological sort
and cycle detector to do.

`hp::Ref<T>` (T0071) softens the related hazard as well: it is GUID-backed and
resolves on use, so a scene service outliving an entity reference gets null
rather than a dangling pointer. The inward-out teardown order — scene services →
entities → session services — still matters; getting it wrong stops being fatal.

**Rejected — `registry.ctx()` as the service container.** Verified against
vendored entt 3.16.0 (`entity/registry.hpp:159`): it is a
`dense_map<id_type, basic_any<0u>>`, so **destruction order is unspecified** and
every entry is a separate heap allocation. It is a fine storage primitive for
*one* root — both objections vanish at n=1 — and the wrong container for twenty
services. `emplace_as` takes an explicit id, so a stable name key per D14 is
available.

**The architecture-review note's worry is answered.** That note flagged
`Autoload::Get<T>()` as *"exactly the kind of static state T0095 exists to sort
out"*, and preferred access through a context. `hp::service<T>()` resolves to a
pointer cached at load rather than a global lookup — fast, and with
compiler-guaranteed lifetime. It is still **ambient access**, and that
traceability cost is real and accepted deliberately: *"who touches the score?"*
is a grep, not a call graph, which is the same cost **D10** records for the
message bus.

**76.8 is no longer this ticket's own mechanism.** Behaviours and services are
both reflected data in engine-owned storage, so the reload snapshot is **one
implementation shared with T0062.6**. Do not build a second.

**"Created at startup" is now settled in the Done-when text**, not just in a
note: session scope means one **play-mode run**, created on play entry and
destroyed on stop. 76.10 gains an explicit test for it, because the failure —
a playtest starting from the previous run's state — only appears on the *second*
run and is exactly what play mode's scene clone exists to prevent.

**Left open on purpose:** whether a scene service should instead be a behaviour
on a scene-root entity. As a behaviour it gets the inspector, serialization and
lifecycle free; as a service it stays out of the hierarchy, which this ticket
argues for. Godot and Unity make it a node/GameObject, Unreal a world-spawned
actor. Genuinely balanced, and not decided.
