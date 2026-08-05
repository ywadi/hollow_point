# T0062 — Entity behaviours: attaching C++ logic to entities

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 3 — Data model |
| **Order** | 270 |
| **Created** | 2026-08-03 |
| **Refs** | T0100, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md), [../../documentation/09-gameplay-authoring.md](../../documentation/09-gameplay-authoring.md) (owns), [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D23, T0053 (Blocks this), [../completed/0022-scene-serialization.md](../completed/0022-scene-serialization.md), T0095, T0071, T0072, T0073, T0076 |

## Why

The core gameplay-authoring question: how does C++ code get attached to an entity
and executed for it, in the way Godot attaches a script to a node?

Two paradigms are in tension. Godot is OOP-per-object — one script instance per
node with `_process(delta)`. ECS is data-oriented — systems iterating component
arrays. Naively mixing them gives scattered allocations *and* awkward systems.

**Resolution: support both, deliberately.**

- **Systems** for many-of things — physics sync, animation, culling, particles.
  Cache-friendly, parallelisable (T0050).
- **Behaviours** for bespoke per-entity logic — the player, a boss, one door.
  Godot's ergonomics, where entity counts are low and logic is unique.

> **Superseded 2026-08-05 by D23.** The tension is *resolved*, not maintained:
> there is one mechanism — a class whose instances are components in a
> per-concrete-type pool — plus `HP_SYSTEM` as an escape hatch. See the rescope
> note at the bottom and
> [`09-gameplay-authoring.md`](../../documentation/09-gameplay-authoring.md).

## Done when

**Rescoped 2026-08-05 against D23. The shape this must produce is
[`09-gameplay-authoring.md`](../../documentation/09-gameplay-authoring.md),
which this ticket owns.**

- [ ] `hp::Behaviour` base with `ready` / `process` / `physicsProcess` /
      `lateProcess` / `destroyed`, mapped onto frame phases 3b, 4 and 8
- [ ] Instances live in **engine-owned per-concrete-type entt pools**, not in
      module-allocated objects
- [ ] `HP_BEHAVIOUR` / `HP_BEHAVIOUR_BASE` register a type with **no plumbing in
      the gameplay file** — no `adoptMetaContext`, no `forgetType`, no
      `HP_EXPORT`, no `extern "C"`, no `entt::`
- [ ] Deregistration is automatic and symmetric with registration; forgetting it
      is not possible
- [ ] The safe surface makes the silent traps unreachable — `setPosition` /
      `setRotation` mark the transform dirty, `get<T>()` for siblings
- [ ] **State survives a gameplay module hot reload** (T0048) — see 62.6
- [ ] `hp::each<Base>` finds every registered subclass, via `using Super = X`
- [ ] Update order is deterministic and controllable
- [ ] Tiered ticking: no `process()` override costs nothing; an override runs
      every frame; `setProcess(false)` exists but is **not** in the
      getting-started path
- [ ] `HP_SYSTEM` escape hatch — raw component iteration for things you would
      count rather than name
- [ ] A behaviour is attachable and editable from the inspector (T0035, Phase 6)
- [ ] Behaviour properties serialize with the scene **by name** (T0022)
- [ ] Guidance written down on behaviour vs system, and inherit-vs-compose

## Subtasks

- [ ] 62.1 `hp::Behaviour` base: lifecycle, safe transform/sibling/log surface.
      **Deletes copy and move** so entt's `in_place_delete` gives instances
      stable addresses (see notes)
- [ ] 62.2 **Multiple behaviours per entity** — one component pool per concrete
      type
- [ ] 62.3 Registration pushed from the module via an intrusive list walked at
      `onLoad` and unwound at `onUnload` — **no static needing destruction**
- [ ] 62.4 `HP_BEHAVIOUR` / `HP_BEHAVIOUR_BASE`: reflection, field list,
      callback detection, generated per-type dispatch loop (T0053)
- [ ] 62.5 Serialize as a reflected component, reusing T0022 rather than a
      parallel by-name path
- [ ] 62.6 **Hot-reload cycle**: reflected snapshot → unload → load → recreate →
      restore. **Shared with T0076.8 — one mechanism, not two.** Mandatory, not
      optional: genuine unload works (T0048/T0105.1), so module-side vtables and
      pools genuinely dangle
- [ ] 62.7 Deterministic update order, with an explicit priority
- [ ] 62.8 Base-type registry: `using Super = X` → `hp::TypeBuilder::base<Base>()`
      → `hp::each<Base>` walking each derived pool
- [ ] 62.9 Tiered ticking and `setProcess(false)`; **`setEnabled(bool)` is a
      separate, semantic thing** and must not be the same function
- [ ] 62.10 `HP_SYSTEM` registration into the existing phases
- [ ] 62.11 `ModuleContext` gains a `Scene*`; `ModuleApi` gains the phase hooks;
      an app owns a `Scene`. **~30 lines and it blocks every other subtask**
- [ ] 62.12 Measure: dispatch cost per behaviour, and whether `final`
      devirtualises under `zig cc`. The "just always tick" default rests on it
- [ ] 62.13 Editor: "Add Component" surface, not a separate behaviour dropdown
      (T0035)

## Notes / findings


### Frame anatomy — phases 3b and 4 — behaviour dispatch (T0100, D17)

Behaviour dispatch is **phase 4 (variable update)**. A behaviour that must be
reproducible belongs in `onFixedUpdate` (**phase 3b**) instead, and must
tolerate running zero or several times in one frame.

The full order is in [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md); the decision and what it rejected is **D17** in the
decision log. If this ticket needs a phase that does not exist, that is a change
to T0100's document and to D17 — not a new call bolted into `Application::run`.

**Many behaviours per entity, not one.** Two models exist: Godot allows one
script per node and composes via child nodes; Unity allows many components per
object. **Unity's is the right fit here.** Composing a `StateMachine` and a
`PlayerController` onto the player should not require inventing a child entity
whose only purpose is to hold logic — that pollutes the hierarchy with things
that are not scene objects.

Consequence: behaviours need `GetBehaviour<T>()` to find siblings, and update
order within an entity must be defined (declaration order, or explicit priority).

**Not everything reusable should be a behaviour.** The test is whether it needs to
exist *in the scene* — selectable, positioned, saved, visible in the hierarchy. A
state machine is logic, so it is best as a plain C++ class owned by a behaviour
(see T0073). Promote it to a behaviour only when it needs inspector exposure.


**Hot reload is the constraint that shapes the whole design.** A behaviour
instance allocated inside the gameplay DLL and stored in a component has a vtable
pointer into that module — on reload it dangles, and the memory came from a
module that no longer exists. The cycle in 62.6 is the answer, and it works only
because reflection can serialize arbitrary behaviour state generically.

This makes **T0053 (reflection) a hard prerequisite**, and is the third
independent reason it is the keystone of the architecture — the other two being
scene serialization and the inspector.

**Type registration must be pushed from the module, not pulled by the engine.**
The engine cannot enumerate types inside a shared library it just loaded. The
module exports one entry point — `RegisterTypes(Registry&)` — called on load and
again on every reload. That same registry populates the inspector's dropdown.

**Performance, stated honestly so the guidance is not cargo-culted:** a virtual
call is a few nanoseconds, and ten thousand behaviours updating per frame is
genuinely fine. The real cost is cache misses from scattered allocations, which
pooling by type addresses. The rule: *bespoke logic → behaviour; thousands of
identical things → system.* Do not reach for a system because virtual dispatch
sounds slow; reach for it when the data layout actually matters.

**Do not store raw pointers to other behaviours or entities across frames.**
Entities are destroyed, behaviours are recreated on reload. Store `Entity`
handles (GUID-backed) and resolve on use — the same discipline that makes assets
reloadable (T0058).

`OnFixedUpdate` must be driven by the accumulator in T0057, not by the frame
loop, or gameplay physics interactions become frame-rate dependent.

### Architecture review (2026-08-03)

Two scope clarifications, so this Phase 2 ticket is not blocked on Phase 6:

- **The editor-facing Done-when items cannot close in Phase 2.** "Attachable
  from the editor inspector", reflected properties "editable in the
  inspector", and 62.11's dropdown all require T0035 (Phase 6). The Phase 2
  deliverable is the mechanism — base class, registry, serialization,
  hot-reload cycle, update dispatch — exercised from code and tests; the
  editor surface lands with the inspector. Same pattern as other tickets whose
  acceptance spans phases, but worth stating here because this one is the
  gameplay keystone.
- **Decide edit-mode execution policy now, even if the answer is simple:** do
  behaviours run while *editing* (not playing)? The sane default is no —
  `OnCreate`/`OnUpdate` fire only in play mode / runtime, and anything that
  must run in-editor is an explicit opt-in later (Unity's `ExecuteInEditMode`
  equivalent, if ever). Leaving it undecided means someone's `OnCreate` will
  fire during scene load in the editor and mutate the authored scene.

Linkage prerequisite: the type registry being "pushed from the module" only
works once T0095 (module ABI — one engine state, entt across the boundary) is
settled. T0095 now blocks this ticket.

### Second review pass (2026-08-03) — the Phase 3 dependencies, named

The first review scoped the *editor* items out of Phase 2; two more
dependencies also cross the phase line and are worth naming so the sequencing
is honest:

- **Entities (T0021).** Behaviours attach to entities; the Scene/ECS wrapper is
  Phase 3. The Phase 2 mechanism can be built and tested against a raw
  `entt::registry`, but nothing here truly closes before T0021 exists.
- **Serialization (62.5) is T0020/T0022 territory** — Phase 3 again. The
  hot-reload cycle (62.6), however, does *not* need the YAML/file layer: a
  reflection-driven **in-memory** snapshot (property values into a value tree,
  restored after reload) is sufficient and keeps the reload path independent of
  file formats. Design 62.6 against that, and let scene serialization reuse the
  same property enumeration when T0022 lands.

Practical order: T0053 → T0095 → this mechanism (registry + lifecycle +
in-memory snapshot, tested headless) → T0021/T0020 make it real → T0035 gives
it an editor surface. The ticket stays Phase 2 as the *design/mechanism*
keystone, but the plan should not expect it to be demonstrable end-to-end
before mid-Phase 3. *(Superseded on the phase question the same day — see the
re-phasing note below; the practical order stands.)*


### Architecture decision (2026-08-03) — behaviours are keyed by stable name (D14)

**A behaviour's identity is a stable name, not its C++ type identity.** This is
required by serialization, independent of any other consideration: T0095
measured that `entt::type_index` is a per-module sequential number that differs
between the engine and a gameplay module for the *same* type, and cannot be
persisted or compared across the boundary. A scene or prefab that stores a
behaviour reference must store a name.

`entt::type_hash` (name-based) *is* stable across the boundary on both targets
and is what entt itself keys component pools on — so component storage is safe;
it is the sequential index that is not.

Two further consequences:

- **Composition is the extension mechanism** (D14). There is no scripting layer;
  new content is existing behaviours recombined with new data, and genuinely new
  mechanics ship as a game update. This makes the behaviour *catalogue* a
  first-class thing to design — a small set of well-factored behaviours composes
  better than a large set of specific ones
- Registration and deregistration must be symmetric, because the module hosting
  these behaviours is unloaded and reloaded (T0048)

Linkage is settled: rich C++ across the boundary, engine as a shared library,
lockstep with a build-id check (D12). This ticket is no longer blocked on T0095
for that.

### Re-phased 2 → 3 (2026-08-03)

The second review pass above already laid the sequencing out honestly — nothing
here truly closes before T0021 (entities to attach to) and T0020/T0022
(serialization for 62.5) exist, and those are Phase 3. The phase field now says
so, rather than leaving "Phase 2" to contradict the ticket's own analysis. What
stays earlier is unchanged: T0053 (reflection) and T0095 (module ABI) are the
Phase 2 prerequisites, and the editor surface (62.11, inspector items) still
lands with T0035 in Phase 6. The Order field places this after T0071 (entity
references), since behaviour properties hold `EntityRef`s, and before
T0072/T0075, whose handlers assume behaviours exist to receive them.

### Rescoped 2026-08-05 — D23, and the ticket gets smaller

The design conversation that produced **D23** and
[`09-gameplay-authoring.md`](../../documentation/09-gameplay-authoring.md)
answered the question this ticket opens with — *"how does C++ code get attached
to an entity"* — and the answer makes the ticket **smaller**, not larger.
Complexity drops Very Complex → Complex.

**The two-paradigm tension in the Why section is resolved rather than
maintained.** The ticket originally proposed supporting behaviours *and* systems
as separate mechanisms. There is now one mechanism — a class whose instances are
components in a per-concrete-type pool — plus `HP_SYSTEM` as an escape hatch for
things you would count rather than name. The `Behaviour` versus `System` choice
becomes a data-layout choice at one call site, not two subsystems.

**What was deleted, and why:**

| Was | Why it went |
|---|---|
| 62.9 pool allocation via `FixedBlockMemoryAllocator` | entt pools components contiguously already |
| a behaviour type registry distinct from the component registry | it *is* the component registry (T0053) |
| 62.5's parallel `{type, properties}` serialize path | components serialize via T0022 |
| 62.11's separate "Add Behaviour" dropdown | it is "Add Component" (T0035) |

That parallel infrastructure is why this was Very Complex. What replaces it is
the declaration layer, which is one header.

**What was added:**

- **62.11 — the plumbing link.** Verified 2026-08-05: `ModuleContext` carries
  only `generation` and `name`, `ModuleApi` has no phase hooks, and **nothing in
  `apps/` owns a `Scene`**. So no gameplay code of any shape can be written
  today. ~30 lines, and it blocks every other subtask here.
- **62.12 — measurement.** The decision to let every behaviour tick every frame
  by default rests on an *estimated* ~2–5 ns dispatch. That is arithmetic, not a
  measurement, and this ticket owns turning it into one — including whether
  `final` actually devirtualises under `zig cc`.

**The base class earns its place on safety, not just ergonomics.** It is the
only place to put `setPosition`/`setRotation` wrappers that mark the transform
dirty. `Scene.hpp` is explicit that a raw write to `Transform` *"is invisible to
propagation until something marks the entity dirty"* — that is the first silent
failure a door author hits, and a base class makes it unreachable.

**`hp::Behaviour` must delete copy and move.** Verified against vendored entt
3.16.0: `component_traits<T>::in_place_delete` (`entity/component.hpp:15`)
defaults to true for types that are not move-constructible, which gives
behaviour instances **stable addresses**. Signal connections and `Behaviour*`
held within a frame depend on that.

**The old performance argument against a base class was weaker than it looked.**
What makes MonoBehaviour slow is iterating a heterogeneous list of pointers into
scattered heap objects — not inheritance. One pool per concrete type is
contiguous whether or not the type has a vtable. The cost is 8 bytes of vptr.

**The cost accepted:** a class with virtuals is not an aggregate, so the
compile-time field-name reflection used by `glaze`/`reflect-cpp` does not apply
and exported fields must be listed in `HP_BEHAVIOUR`. Base class or
annotation-free reflection — not both.

**Still unverified, and recorded rather than assumed:**

- Self-registering statics inside a module. The intrusive-list node should be
  safe — it is a POD with no destructor, and this toolchain punishes
  *destruction*, not statics — but it has not been tried. Static-init order
  across TUs is unspecified, so sort the walk by name if anything depends on it.
- `hp::each<Base>` per-pool iteration is designed, never built.
- `06-engine-conventions.md` still says *"unloading a module does not currently
  work"*, which T0048's completion contradicts (*"genuine unload works"*, 25
  clean host lifetimes, 1.76 ms reload swap). One of them is stale and it
  matters here: if unload is genuine, module-side vtables and pools genuinely
  dangle and 62.6 is **mandatory**. Resolve before building 62.6.

### From T0022 (closed 2026-08-05) — you must call `materialiseUnknownComponents`

A behaviour is a reflected component and serializes through T0022's path, with no
second mechanism (D23). The half that is **yours**: when a scene is loaded while
this module is absent or failed to build, its types are preserved as raw YAML on
an `UnknownComponents` component rather than dropped. They become real components
only when something calls `hp::materialiseUnknownComponents(scene)` — **this
ticket owns calling it after a module loads or reloads.** Nothing calls it today,
because nothing yet loads a module and a scene in the same process. Without that
call the data is still safe on disk, but a developer who fixes their build and
reloads sees the components missing from the live scene.
