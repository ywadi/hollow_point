# T0021 — Scene and entity-component system

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Order** | 200 |
| **Created** | 2026-08-02 |
| **Refs** | T0053, T0100, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md), [../inprogress/0048-hot-reloadable-gameplay-module.md](../inprogress/0048-hot-reloadable-gameplay-module.md), [../completed/0112-string-identity-and-localisation.md](../completed/0112-string-identity-and-localisation.md) |

## Why

A Scene is the container of everything the game and editor manipulate, and it is
the simplest self-contained concept in the data model — so it comes before assets
and projects, both of which reference it.

EnTT 3.16.0 is already vendored (D7), so the registry itself is free; the work is
the component set, the entity handle, and the scene's ownership semantics.

## Done when

- [x] `Scene` owns an entt registry and creates/destroys entities
- [x] Every entity automatically gets an ID (GUID, T0016) and tag component
- [x] A lightweight `Entity` handle for add/get/has/remove of components
- [x] Core components: transform, mesh, material, camera
- [x] Scene copy works — Phase 6 play mode needs to clone and discard
- [x] Tests for entity lifetime, component add/remove, GUID stability

## Subtasks

- [x] 21.1 `Scene` wrapping `entt::registry`
- [x] 21.2 `Entity` handle — a registry pointer plus an `entt::entity`, cheap to
      copy, never owning
- [x] 21.3 Automatic ID + tag on creation
- [x] 21.4 Transform component **with parenting** — required, not optional (notes)
- [x] 21.5 Mesh, material and camera components referencing assets by GUID
- [x] 21.6 `Scene::Copy` for play mode
- [x] 21.7 GUID → entity lookup, since serialization and selection both need it

## Notes / findings


### Frame anatomy — phase 5 — structural apply (T0100, D17)

Entity creation and destruction queued during update apply at **phase 5
(structural apply)**, not at the end-of-frame safe point. Deferring them to end-
of-frame would let a destroyed entity be transformed at 7, read by a follower at
8 and drawn at 10 — one last frame of a thing that no longer exists.

The full order is in [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md); the decision and what it rejected is **D17** in the
decision log. If this ticket needs a phase that does not exist, that is a change
to T0100's document and to D17 — not a new call bolted into `Application::run`.

**Components reference assets by GUID, never by pointer.** Pointers do not
serialize and do not survive an asset reload. This is the single most important
rule in the data model.

**Transform hierarchy is required, not optional.** Skeletal animation is core to
this engine (T0041), and skinned characters need parenting for attachments —
weapons in hands, props on sockets, cameras on rigs. Retrofitting parenting
changes the transform component, the update order and the hierarchy panel, so it
goes in from the start.

Bone transforms are deliberately *not* entities. A skeleton with 80 bones per
character would swamp the registry; ozz keeps its own compact SoA pose buffers
and the renderer consumes those directly. Entity parenting is for attachment
points, not for the skeleton itself — conflating the two is a common and
expensive mistake.

Do not expose `entt::` types in public engine headers; keep the dependency
swappable and keep compile times down. The `Entity` handle is the seam.

### Architecture review (2026-08-03) — `Scene::Copy` is two operations, not one

Two different copies exist with **opposite GUID semantics**, and 21.6 currently
names only one of them:

- **Play-mode clone** (T0037): GUIDs are *preserved*, so EntityRefs (T0071),
  signal connections (T0072) and save data keep working inside the clone.
  Consequence: GUID→entity lookup must be per-scene, never global, because two
  live scenes now contain the same GUIDs.
- **Duplicate / subtree copy** (editor duplicate, prefab instantiate): GUIDs
  are *regenerated* and internal references *remapped* (T0071's old→new map).

Make both explicit in the API — one function with a mode is fine; one function
with an ambiguous behaviour is how an instance ends up silently sharing state
with its template.

Also: once behaviours exist (T0062), cloning a scene must clone behaviour
*instances* — heap objects with vtables — not just component data. The
reflection serialize/recreate cycle T0062 builds for hot reload is the same
mechanism; pointer-copying instances between scenes is never correct. Worth
knowing before 21.6 is designed, even though behaviours land later.


### Inherited from T0053 (closed 2026-08-04)

- **T0053.7 moved here.** Reflection landed without ECS integration because
  there was no registry to hook into. This ticket owns it: when a component type
  is registered with the registry it must also be registered for reflection, in
  **one** place — `hp::reflect<T>("Name")` alongside the component's declaration,
  not a second list that drifts. The documented entt idiom is a meta func
  wrapping `registry::emplace_or_replace<T>`, roughly thirty lines of glue.
  Identity is the **name**, never `entt::type_index`, which T0095 measured to be
  a per-module number with no meaning across the module boundary.

### Cross-ticket obligation — T0048 (2026-08-04)

**T0048.5 is parked here, and it is a small test rather than a design task.**
Hot reload is built and works: a module is loaded, swapped and unloaded at frame
phase 12, and a rebuilt module's *code* is live in the running process
(measured). What could not be verified is T0048's third Done-when — "the open
scene, entities and component data survive a reload intact" — because there is
no scene and no engine-owned `entt::registry` for a component to live in.

When this ticket lands, add the case: put a component on an entity, mutate it,
reload the gameplay module, and assert the value is unchanged. It belongs in
`tests/integration/module_host_test.cpp`.

**The rule it is testing is already binding on this ticket's design**, so it is
worth stating rather than leaving to the test: *all persistent state lives in
the ECS, owned by the engine — never in the gameplay module.* Statics inside a
module are destroyed on unload, so anything kept there is gone. That single rule
is what decides whether hot reload is reliable or a source of baffling bugs, and
it means the registry must be owned engine-side from the start rather than
handed to gameplay to keep.

### Cross-ticket obligation — T0112 (2026-08-04)

**This is the ticket that starts authoring data, so it is the one the string
convention was written ahead of.** T0112 decided that every player-facing string
is a string-table key, never an English literal; the rule and the reasoning are
in [`../../documentation/06-engine-conventions.md`](../../documentation/06-engine-conventions.md).

Two specifics land on this ticket's design:

- **The entity tag/name component (21.3) is not player-facing and must not be a
  key.** It is the editor's label for an entity — `Door_01`, `PlayerSpawn` —
  and keying it would put developer identifiers in a translation table while
  making the hierarchy panel depend on a table load. If a game wants a
  displayable name for an entity, that is a separate, keyed component.
- **None of the core components (21.4–21.5: transform, mesh, material, camera)
  carries player-facing text**, which is why T0112 concluded no engine machinery
  is needed yet. If that turns out to be wrong while implementing, the first
  such field gets its own reflected type rather than a bare `std::string` — see
  the convention's closing note, and say so on T0112 rather than adding one
  quietly.

### Finding (2026-08-04) — the math policy and the no-Diligent-in-public-headers rule are in direct conflict

Discovered on 21.4, which is the first public header that needs a vector. Two
binding rules collide and neither anticipated the other:

- `06-engine-conventions.md` says **gameplay uses Diligent's math types
  directly**, not a wrapper — every renderer call takes them, a second library
  means conversions at every boundary forever, and the two disagree on row versus
  column major.
- `hp/Render.hpp` and 13.3 say **no public engine header may name a Diligent
  type**, because that hands every consumer Diligent's include path.

You cannot satisfy both, because **the math cannot be had without the RHI**.
`BasicMath.hpp` includes `HashUtils.hpp` — solely for `ComputeHash`, used by the
`std::hash` specializations at the bottom of the file — and `HashUtils.hpp`
includes `Sampler.h`, `PipelineState.h`, `TextureView.h`,
`PipelineResourceSignature.h` and `VertexPool.h` so it can hash *their*
descriptors. The leak is an artifact of Diligent putting all its hashing in one
header, not of anything intrinsic to the math.

**Measured rather than inferred** (2026-08-04, zig 0.16.0):

- a TU containing only `#include "BasicMath.hpp"` preprocesses to **146,280
  lines across 74 distinct Diligent headers**;
- `IRenderDevice`, `IDeviceContext`, `IPipelineState`, `ISampler` and
  `ITextureView` are all visible from it;
- adding it to a TU that already includes entt costs **+594 ms**, 1557 → 2151 ms
  (+38%), three-run mean;
- `-I Common/interface` plus `PLATFORM_LINUX=1` is **sufficient** — everything
  below resolves through relative includes, so the exposure is one directory;
- `Diligent-PublicBuildSettings` is an `INTERFACE` target (verified at
  `DiligentCore/CMakeLists.txt:158`), so linking it PUBLIC adds no code.

**Resolved in favour of the math policy, recorded as D21**, with the exposure
made as narrow as it can be: `hp/Math.hpp` is the only public header that
includes Diligent, and only Diligent's **include directories** go PUBLIC — never
`Diligent-Common` itself, which is a STATIC library whose code would then be
compiled into every gameplay module. That is the statics-per-artifact hazard that
produced T0105.1's dangling `__cxa_atexit` and T0127's typeinfo mismatch, and it
is why the graphics engines stay PRIVATE: the RHI types are *visible* to gameplay
and *unlinkable* by it.

Rejected: an `hp::Vec3` with conversions at the boundary (the conventions
document already rejected it, and D21 does not reopen it); forward declaration
(a component holds a transform by value, so the type must be complete);
patching the header (DiligentEngine is an upstream submodule, `HashUtils.hpp`
uses `#pragma once` so its guard cannot be pre-tripped, and carrying a fork to
save 600 ms is a bad trade).

### Built 2026-08-04 — what landed, and what is measured

`engine/include/hp/Scene.hpp`, `engine/src/Scene.cpp`, `engine/include/hp/Math.hpp`,
`tests/fast/scene_test.cpp`.

**Verified:** `zig build test` green on **both** targets — 63 test cases, 213,267
assertions, Linux natively and Windows under wine. The 14 new cases were
confirmed present by name via `--list-test-cases`, not inferred from the total,
because a test file that silently fails to compile into the bucket looks exactly
like one that passes.

Design decisions worth knowing before extending this:

- **`Hierarchy` is a separate component from `Transform`.** 21.4 asked for a
  transform "with parenting" and this is that, split deliberately: a transform is
  data a caller may assign freely, while a hierarchy carries an invariant — a
  parent's `children` must contain exactly the entities whose `parent` is it. A
  `parent` field inside `Transform` invites `t.parent = e`, which breaks that
  invariant silently and produces a child transformed by a parent unaware of it.
  `Scene::setParent` is the only supported mutation, and it refuses cycles
  (tested) because every traversal in the engine assumes termination.
- **`Scene::clone(CloneIds)` has no default argument.** The two modes have
  opposite GUID semantics and picking one silently is the exact failure the
  architecture review warned about. `Preserve` is the play-mode clone; note the
  consequence it forces, which is honoured here: **GUID lookup is per-scene**
  (`byGuid_` is a member, not a global) because two live scenes now legitimately
  contain the same GUIDs.
- **`registerComponent<T>("Name")` is the single registration point** and
  discharges T0053.7. It records the clone operation *and* returns `reflect`'s
  builder, so fields are described in the same expression that registers the
  type — there is no second list to drift. Identity is the name, never
  `entt::type_index` (T0095).
- **An unregistered component is silently dropped by `clone`.** That is the real
  failure mode of this design and it has its own test rather than a comment, so
  it is discoverable rather than surprising.
- **`entt::` types do appear in this public header**, contradicting the note
  above from before T0095. That note predates the decision to link `EnTT::EnTT`
  PUBLIC so gameplay modules can register types into the shared meta context. A
  component API is templated, so its bodies must be in the header, so the
  registry type must be visible; hiding it would mean type-erasing every
  component access through reflection. The include is narrowed to
  `<entt/entity/registry.hpp>` rather than `<entt/entt.hpp>`.
- `Entity` is `HP_API`: the engine builds with hidden visibility, and `valid` and
  `guid` are its only out-of-line members. Without the export they linked inside
  the engine and were undefined in the test binary — caught at link time, noted
  here because the next handle-like class will hit it too.

**Also changed, and it is a real widening:** `tools/gen_api_docs.py` gained a
`--define` option, and `build.zig` now passes Diligent's include directory plus
the host's platform macro to the docs generator. Without both, libclang cannot
parse `hp/Math.hpp` and reports the failure as a defect in *our* header. The
platform macro follows the **host**, not the build target, because the generator
parses rather than compiles and Diligent's Linux platform headers reach for
system headers a Windows host lacks.

### Not done — 21.x carried forward

- **T0048.5's hot-reload test is not written.** The obligation above says: put a
  component on an entity, mutate it, reload the gameplay module, assert the value
  is unchanged, in `tests/integration/module_host_test.cpp`. The registry is now
  engine-owned, which is the precondition it was waiting for, so nothing blocks
  it — it simply was not written in this pass. **T0048 stays blocked on it**, and
  its third Done-when stays unticked.
- **`Scene` does not queue structural changes.** `create` and `destroy` are
  immediate. D17 places queued creation and destruction at frame phase 5, and the
  queue belongs to whatever drives the frame rather than to the container —
  stated in the header, but no phase-5 apply exists yet, so anything relying on
  D17's ordering guarantee does not have it today.
- **World transforms are not computed.** `Transform` is local only; propagation,
  dirty-marking and traversal order are T0101 and were deliberately left there.
- **Reference remapping on `CloneIds::Regenerate` covers the hierarchy only.**
  Components holding entity references are copied verbatim and will point into
  the source scene. That needs the old→new map and the reference type itself,
  both T0071's — **T0071 must extend `Scene::clone`**, and its ticket now says so.
