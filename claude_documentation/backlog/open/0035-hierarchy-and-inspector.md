# T0035 — Scene hierarchy and inspector panels

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 6 — Editor |
| **Order** | 630 |
| **Created** | 2026-08-02 |
| **Refs** | T0053 (Blocks this), T0032, T0071, T0062, [../completed/0160-material-declared-parameters.md](../completed/0160-material-declared-parameters.md) — **a material's rows come from two reflections and must present as one**: `hp::ShaderParam::meta()` returns the same `hp::PropertyMeta` a reflected C++ field carries, so the inspector consumes one struct whichever side produced it, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D23, [../../documentation/09-gameplay-authoring.md](../../documentation/09-gameplay-authoring.md), [../completed/0112-string-identity-and-localisation.md](../completed/0112-string-identity-and-localisation.md) |

## Obligation from T0060 (2026-08-05) — percentages are the inspector's job

The owner asked whether PBR strengths should be **0-100**. They are stored
**0..1**, because they multiply straight into shader math and a second unit means
a conversion at exactly one edge — which is a bug the first time somebody
hand-writes a material and it comes out a hundred times too bright.

**The percentage belongs here.** `PropertyMeta` already carries `min`, `max` and
a tooltip per property (T0053), so this panel can show a 0–100% slider while the
file stays in the units the shader wants. That is the whole resolution, and it is
recorded on *this* ticket because this is where it has to be honoured.

Three properties must **not** be clamped to 1 by a naive percentage widget, and
clamping them would be a real authoring regression:

- **`Material::emissive`** is HDR — values above 1 are the entire point once
  T0096's tonemapping exists;
- **`Material::normalScale`** above 1 exaggerates a normal map, which is a common
  and legitimate authoring move;
- **`Light::intensity`** is unbounded for the same reason.

`Material::baseColour` genuinely *is* bounded to 0..1 — a surface reflecting more
light than falls on it is not a material, it is a bug — so the clamp is right
there and wrong on the three above. The distinction has to be per property, which
is what `PropertyMeta` is for.

## Why

Built as a pair, because the inspector is useless without the hierarchy feeding
it a selection. Together they are the first point at which the editor can
actually *edit* rather than only display.

## Done when

- [ ] The hierarchy lists all entities in the open scene, read live
- [ ] Selecting an entity writes its GUID into EditorState's temporary section
- [ ] The inspector reads that GUID and lists the entity's components
- [ ] Component fields are editable and changes show in the viewport immediately
- [ ] Entities can be created, renamed and deleted
- [ ] Deleting the selected entity clears the selection rather than dangling

## Subtasks

- [ ] 35.1 Hierarchy panel querying the scene each frame — do not cache (notes)
- [ ] 35.2 Selection writing to EditorState
- [ ] 35.3 Create empty entity, rename, delete
- [ ] 35.3b Duplicate an entity/subtree — uses T0071's reference remap (old→new
      GUID map), which is why it is called out rather than assumed
- [ ] 35.4 Inspector reading the selection and listing components
- [ ] 35.5 Editable widgets per component type, registered like serialization
      (T0022) rather than a central switch
- [ ] 35.6 Add/remove component UI
- [ ] 35.7 Handle a stale selection GUID after scene reload or entity deletion

## Notes / findings

**Read the entity list live from the scene, do not cache it in the panel.** A
cached list goes stale the moment anything else mutates the scene — script, undo,
asset reload — and produces a UI that disagrees with reality. Query per frame; it
is cheap at this scale.

Component editor registration should mirror component *serialization*
registration (T0022). Two parallel central switches over component types is
exactly the duplication that rots.

### Architecture review (2026-08-03)

The paragraph above predates T0053 and understates it: the inspector should
not "mirror" serialization registration — **both are generated from the same
reflection declaration** (T0053 lists the inspector as a consumer that uses
reflection "and nothing else"). 35.5's per-type widgets are the *presentation*
layer over reflected properties (plus metadata like ranges and tooltips), not
a second registry. Duplicate-entity was also missing from the ticket entirely
(now 35.3b) — it is a scene-authoring staple and it is the first editor
operation that exercises T0071's remap.

Undo/redo is deliberately not in scope, but it is much cheaper to design for now
than to retrofit: if edits go through a small command abstraction rather than
mutating components directly, undo becomes tractable later. Worth a deliberate
decision either way.


### Architecture decision (2026-08-03) — the inspector shows module-defined types (D12)

The inspector's job includes game-defined components and behaviours, which live
in the gameplay module the editor loads (see T0032's amendment). This binds the
inspector to two things it did not previously reference:

- **Reflection (T0053) must work for types defined in the module**, not only for
  engine types. Whatever the registry is, the module populates it at load and
  must depopulate on unload or a reload leaves stale entries describing types
  that no longer exist
- **Behaviours are addressed by stable name, not by `entt::type_index`** (D14).
  T0095 measured that the sequential index is a per-module runtime number which
  differs across the boundary and cannot be persisted. An inspector that keys
  anything on it will appear to work in-process and break on reload or reload
  order changes

"No module loaded" is a legitimate state the inspector must render sensibly
rather than treat as an error.


### Inherited from T0053 (closed 2026-08-04)

- **Draw components through reflection only.** T0053's Done-when "serialization,
  inspector and undo all consume this and nothing else" could not be met there —
  this ticket is half of meeting it. The inspector must enumerate
  `meta_type::data()` rather than switching on a component type, or adding a
  component means editing the inspector too.
- **`hp::PropertyMeta` is already carried and currently interpreted by nobody**:
  `min`/`max` for numeric ranges, `tooltip`, `read_only`, `hidden`. Acting on
  them is this ticket's job. Note `hidden` is a UI decision only — hidden
  properties are still serialised, so do not use it to exclude data.

### Cross-ticket obligation — T0112 (2026-08-04)

**A player-facing string field must show its resolved English text, not its
key.** T0112 decided that all player-facing text is authored as a string-table
key (`item.rusty_key.name`), and the convention is in
[`../../documentation/06-engine-conventions.md`](../../documentation/06-engine-conventions.md).

The failure it guards against is behavioural rather than technical: an inspector
that shows raw keys makes authoring miserable, and people who cannot see what
they are writing paste the English literal into the field instead — which
silently defeats the convention everywhere it matters most. Show the resolved
string, edit the key, and render a missing key as `[the.key]` exactly as the
convention specifies.

### From T0062 / D23 (2026-08-05) — there is no separate "Add Behaviour" dropdown

T0062 originally specified one (62.11). D23 removes it: a behaviour **is** a
reflected component, so **"Add Component" is the attach surface for gameplay
logic as well as for engine data**, and its fields render through the same
`PropertyMeta` path (min/max/tooltip/read_only/hidden) already in
`hp/Reflect.hpp`.

Two consequences for this panel:

- The type list must include behaviour types registered by the **gameplay
  module**, and must refresh on hot reload (T0048) — the set changes while the
  editor runs.
- `hp::Ref<T>` properties (T0071) need a picker, since that is how a switch is
  wired to its door.
