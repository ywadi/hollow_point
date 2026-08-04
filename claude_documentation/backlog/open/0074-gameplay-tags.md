# T0074 — Hierarchical gameplay tags

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Order** | 310 |
| **Created** | 2026-08-03 |
| **Refs** | T0075, T0077, T0095 |

## Why

A data-driven way to classify entities — `enemy.flying.boss`, `damage.fire`,
`state.stunned` — where publishing or querying `enemy` reaches every descendant.

Introduced as the addressing scheme for the message bus (T0075), but it earns its
place independently: filtering queries, gating abilities, categorising damage, and
authoring behaviour in the editor without writing a new component type for every
distinction.

## Done when

- [ ] Tags are hierarchical, dot-separated, and interned
- [ ] `enemy` matches `enemy.flying.boss` — ancestor matching, not string compare
- [ ] Entities carry a tag container; tags can be added and removed at runtime
- [ ] Matching is fast enough to run per-message and per-query without care
- [ ] Tags are authored as data, editable in the inspector, and serialized
- [ ] Query the scene for all entities matching a tag
- [ ] Unknown tags in a file fail loudly, not silently

## Subtasks

- [ ] 74.1 Tag registry: intern strings to ids, record parent chains
- [ ] 74.2 Tag type — a cheap value, comparable and hashable
- [ ] 74.3 Ancestor matching — see notes on making it O(1)
- [ ] 74.4 Tag container component, with add/remove/has
- [ ] 74.5 Serialization by string, interning on load (T0020)
- [ ] 74.6 Reflection integration so the inspector shows a tag picker (T0053)
- [ ] 74.7 Scene query: entities matching a tag or its descendants
- [ ] 74.8 Editor: browse and author the tag hierarchy
- [ ] 74.9 Tests, especially ancestor matching and round-tripping

## Notes / findings

**Do not match by string comparison.** `tag.starts_with("enemy")` is both slow and
wrong — it matches `enemyBase` as well as `enemy.flying`. Intern each tag to an id
and store its parent chain; matching is then a walk up a short chain, or a
precomputed ancestor bitset for O(1).

**Tags are not layers (T0085).** Layers are a small fixed bitmask (32) tested per
object per frame in culling, light selection and physics — they exist to be one
instruction. Tags are unlimited, hierarchical and data-authored, and exist to
express gameplay meaning. Engine filtering → layer; game semantics → tag. Both
exist; neither replaces the other.

**Tags versus components — both, deliberately.** entt already makes empty
components excellent compile-time tags, and those stay the right tool for
type-safe queries in systems. Gameplay tags are the *data-driven* counterpart:
authorable in the editor, serializable, hierarchical, and usable without a
recompile. Neither replaces the other; the rule is compile-time classification →
component, content-authored classification → tag.

**Register tags centrally rather than accepting arbitrary strings.** A typo in a
tag string is otherwise a silent no-op that is very hard to spot — the message
simply never arrives. A registry lets unknown tags fail loudly and gives the
editor something to populate a picker from.

Keep the hierarchy shallow. Deep chains are hard to reason about and the extra
levels rarely earn their cost.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0095** measured that per-module counters (the `entt::type_index` shape)
  are not identities across the module boundary or a reload, and named this
  ticket as one that "will be tempted by a dense integer id". Tag ids from
  74.1's intern table are process-local: anything persisted, compared across
  the boundary, or outliving a session is the string (74.5), never the number.
- **T0077.1** decides which resident scenes 74.7's query sees — decided once
  there, not here.
