# T0053 — Reflection and type system

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Very Complex |
| **Phase** | 2 — Engine skeleton |
| **Created** | 2026-08-03 |

## Why

**The most important missing piece in the architecture.** Four separate systems
need to enumerate and manipulate a type's properties generically:

| System | Needs |
|---|---|
| Serialization (T0022) | read/write every property by name |
| Inspector (T0035) | list properties, render an editor per type, write back |
| Undo/redo (T0065) | record a property path + before/after value |
| Any future scripting | expose properties across a boundary |

Without one shared reflection layer, each builds its own central switch over
component types — four places to edit when adding a component, drifting apart
immediately. That is precisely the "hacking the engine to get something done"
outcome to avoid, and it is very expensive to unpick later.

## Done when

- [ ] Types register their properties once, in one place
- [ ] Properties are enumerable at runtime: name, type, get, set
- [ ] Serialization, inspector and undo all consume this and nothing else
- [ ] Adding a component means touching **one** location
- [ ] Nested structs, enums, containers and asset GUID references all supported
- [ ] Attributes/metadata: ranges, tooltips, hidden, read-only
- [ ] Zero or near-zero runtime cost for code that does not reflect
- [ ] Thoroughly unit tested — everything downstream depends on it

## Subtasks

- [ ] 53.1 Choose the mechanism — see notes, this is the decision
- [ ] 53.2 Registration API for types and properties
- [ ] 53.3 Runtime type info: name, size, property list, construct/destruct
- [ ] 53.4 Typed get/set with a safe fallback for mismatches
- [ ] 53.5 Property metadata (range, tooltip, hidden, read-only)
- [ ] 53.6 Containers, nested structs, enums, GUID references
- [ ] 53.7 Component registration hooked into the ECS
- [ ] 53.8 Tests, including round-trip through serialization

## Notes / findings

**Mechanism choice is the crux and worth real deliberation:**

- **Manual registration macros** — explicit, no build-step magic, no extra
  dependency. Verbose, and drifts from the struct if someone forgets a field.
- **Compile-time reflection via templates/`constexpr`** (e.g. a `describe()`
  static per type) — type-safe, no macros, no codegen; some template complexity.
- **Code generation** from a parser (libclang) — zero annotation burden, but adds
  a build step and a heavyweight dependency. Note the build already
  `pip install`s `libclang` for Diligent's own generation, so it is not unheard
  of here — but it is a big commitment.

Manual/`constexpr` registration is the sane default for a project this size.
Codegen is what you reach for when the type count makes hand-registration
untenable, and we are far from that.

**This must land before T0022 (serialization) and T0035 (inspector)**, or both
get written twice. It is the one ordering constraint in Phase 2/3 that really
matters.

C++ has no built-in reflection, so whatever is chosen is a permanent part of how
every engine type is declared. Prototype two approaches on a real component
before committing.


### Architecture decision (2026-08-03) — never key reflection on `entt::type_index` (D12/D14)

Measured in T0095, against the vendored entt 3.16.0: `type_index<T>::value()` is
a **per-module** sequential number. The same type observed from the engine and
from a gameplay module gets different values, because the memoising static is
per-module even when the underlying counter is shared. It is a runtime number
with no meaning outside the module that produced it.

Therefore, for this ticket:

- **A reflected type's identity is a stable name** (or a hash of one), never the
  sequential index, and never a raw pointer to a static
- Nothing derived from `type_index` may be **serialised**, compared across the
  module boundary, or used as a key that outlives a process
- `type_hash` (name-based) *is* stable across the boundary on both targets and
  is a legitimate key — it is what entt keys its own pools on

**The registry must be an instance handed across the boundary, not a global
reached by symbol.** With the engine as a shared library (D12) a global would in
fact resolve to one instance — but designing it as a context object passed in is
robust to that changing and is the convention T0095 recommends for every
stateful engine service, alongside the log sinks (T0054) and the autoload
registry (T0076).

Types defined in the gameplay module must register on load and deregister on
unload, since the module is reloadable (T0048).
