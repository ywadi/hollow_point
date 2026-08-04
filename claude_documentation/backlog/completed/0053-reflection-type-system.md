# T0053 — Reflection and type system

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 2 — Engine skeleton |
| **Order** | 140 |
| **Created** | 2026-08-03 |
| **Blocks** | T0022, T0035, T0062 |
| **Refs** | T0095, T0104, T0105, T0048, T0022, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D12 |

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

- [x] Types register their properties once, in one place
- [x] Properties are enumerable at runtime: name, type, get, set
- [ ] Serialization, inspector and undo all consume this and nothing else —
      **not met, and cannot be here**: T0022, T0035 and T0065 do not exist.
      Moved to those tickets; see the closing note
- [x] Adding a component means touching **one** location
- [x] Nested structs, enums, containers and asset GUID references all supported
- [x] Attributes/metadata: ranges, tooltips, hidden, read-only
- [x] Zero or near-zero runtime cost for code that does not reflect
- [x] Thoroughly unit tested — everything downstream depends on it

## Subtasks

- [x] 53.1 Choose the mechanism — see notes, this is the decision
- [x] 53.2 Registration API for types and properties
- [x] 53.3 Runtime type info: name, size, property list, construct/destruct
- [x] 53.4 Typed get/set with a safe fallback for mismatches
- [x] 53.5 Property metadata (range, tooltip, hidden, read-only)
- [x] 53.6 Containers, nested structs, enums, GUID references
- [ ] 53.7 Component registration hooked into the ECS — **moved to T0021**,
      which builds the registry this would hook into
- [ ] 53.8 Round-trip through serialization — **moved to T0022**, which builds
      the serializer. The reflection half is tested here

## Notes / findings

### Correction (2026-08-05) — properties were registered without their names

`TypeBuilder::property` passed the name to entt only as a hashed id, so
`meta_data::name()` returned nullptr for every property. Everything that looks a
property up **by id** worked, which is every test this ticket shipped; the
breakage was entirely on the **enumeration** path, and enumeration is what three
of the four consumers this ticket exists for actually do — serialization
(T0020.3), the inspector (T0035) and undo/redo (T0065).

Found on the first step of T0020.3, fixed by passing the name alongside the hash
in `property`, `readOnlyProperty` and `value`. The id is unchanged, so no data
format moved. `tests/fast/reflect_test.cpp` pins the enumerated name and checks
that the enumeration key still equals the hash.

Worth remembering as a *class* of gap rather than a one-off: a facade that is
only ever exercised the way its own tests use it will hide whatever its other
consumers need.


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

### Blocks field recorded (2026-08-03)

The header now carries `Blocks: T0022, T0035, T0062` — not a new judgement,
just the promotion of what this file and those tickets already state in prose:
"this must land before T0022 (serialization) and T0035 (inspector), or both get
written twice", and T0062 calls reflection "a hard prerequisite" because its
hot-reload cycle only works if behaviour state can be serialized generically.
The same getting-it-wrong-means-redoing-work test that justified the other
Blocks fields (T0095, T0103, T0104) applies here.

## Mechanism decided (2026-08-04) — `entt::meta`, measured across the boundary

53.1 is answered. **Use `entt::meta`.** It was absent from the options list
above, which was the gap: it is *already vendored* at `third_party/entt`
(3.16.0), pinned through `FETCHCONTENT_SOURCE_DIR_ENTT` in the root
`CMakeLists.txt`, header-only, and reachable via the `EnTT::EnTT` target
DiligentFX already supplies. Adopting it costs **no new dependency, no submodule
and no build wiring.**

### Why it wins on this project's own criteria

Its identity model is the one this ticket already mandates, rather than merely
being compatible with it. `meta_context` is keyed on `type_hash` — the
name-based hash T0095 measured as stable across the module boundary — and the
per-module sequential `type_index` is never used as a key anywhere in meta.
`meta_factory<T>{}.type("Position")` sets identity to
`hashed_string::value(name)`. That is the rule in the amendment above,
implemented.

### Measured here, not searched — both targets

A prototype mirroring `tests/fixtures/` (shared engine `.so`/`.dll`, a module
that links it, a host executable, `-fvisibility=hidden`, pinned toolchain),
rebuilt and re-run independently of the research that proposed it:

```
[EXE(after module load)] ctx@... types:
   id=903561675  name=EnginePos size=12      <- registered by the ENGINE
      .x id=4245442695  [has PropMeta]
   id=2616116277 name=ModHealth size=8       <- registered by the MODULE, same ctx
--- module reads back an ENGINE-registered type ---
   MODULE: resolved EnginePos, y=2.0, x.tooltip=world X
   MODULE: after set, y=42.0
--- after mod_unload (meta_reset ModHealth) ---
   id=903561675 name=EnginePos                <- module's type gone, engine's intact
```

Confirmed working across the boundary: one shared context, module→engine resolve
by name hash, **typed get and set** (53.4), user metadata carrying
min/max/tooltip/hidden (53.5), and clean deregistration on unload.

**The ids are byte-identical on Linux and Windows.** That is the property that
actually matters, because those ids are what gets serialised (T0022) — a
reflection key that differed per target would be discovered at load time, in a
save file, months later.

### The trap, and it fails silently

`entt::locator<entt::meta_ctx>` is **per-module under `-fvisibility=hidden`,
and the executable is a module too.** A host reading its own default-constructed
locator sees an empty context and reports engine types as unresolvable — no
error, no crash, just nothing there. The fix is one line at boot:

```cpp
entt::locator<entt::meta_ctx>::reset(engine_boot().meta_handle);
```

Every participant adopts the handle, or every call passes `meta_ctx&`
explicitly (entt provides both forms). **This belongs in
`06-engine-conventions.md` and in a boundary test**, not in whoever-remembers:
it is exactly the failure mode this ticket's amendment warns about, arriving
through a different door.

### What entt::meta does *not* do

| Subtask | Covered | Note |
|---|---|---|
| 53.2 registration API | yes | macro-free, non-intrusive; still **manual** — no member-name deduction |
| 53.3 runtime type info | yes | id/name/size/data/func/base, construct, `meta_any` owns and destroys |
| 53.4 typed get/set + fallback | yes | measured across the boundary |
| 53.5 property metadata | yes | `custom<PropMeta>(...)` plus a traits bitmask for hidden/read-only |
| 53.6 containers/nested/enums | mostly | std containers auto-detect; **enums need each value registered by hand** |
| 53.7 ECS integration | **no — ours** | ~30 lines of `emplace_or_replace` glue |
| 53.8 serialization round-trip | **no — ours** | entt's snapshot API is byte-level and separate from meta |

So it collapses 53.2–53.6 to registration lines. 53.7 and 53.8 were always ours.

### Two decisions taken with it

- **Wrap registration behind a thin `hp::reflect<T>()` facade on day one.** entt
  moved meta `prop()` to `custom()`/`traits()` during the 3.14 cycle, and this
  ticket's own note says the mechanism becomes "a permanent part of how every
  engine type is declared". A facade makes an entt upgrade one file instead of
  every type. This is the single most valuable line of insurance available here.
- **Add `magic_enum` for 53.6's enum half.** entt requires every enum value
  registered individually; magic_enum derives them and can *drive* the entt
  registration. MIT, single header, actively maintained. Its known limit is the
  `[-128, 127]` default value range — fine for gameplay enums, wrong for sparse
  bitflags, and that constraint should be written down where enums get declared.

### Rejected, with reasons

**RTTR** — eight years since a release, and its own plugin documentation is
disqualifying for T0048: *"Make sure you throw away all retrieved items when
unloading. Otherwise UB may occur"*, plus a `-fno-gnu-unique` requirement and
open bugs on deregistration and on cross-DLL teardown crashes. It would import a
worse version of a problem this project has already measured and fixed.
**refl-cpp** — dormant since 2022. **Boost.PFR** — aggregate-only, no member
names before C++26; a possible helper later, not the mechanism. **libclang
codegen** — still the right answer only when hand-registration becomes
untenable, and it is not.

**C++26 static reflection (P2996)** is not available: the pinned zig 0.16.0 is
clang 21.1.0, where `^^S` is a syntax error and `-freflection` is unknown. It
does not change this decision, and that is the point — because entt::meta keys
on stable name hashes and funnels registration through one call site per type, a
future P2996 migration replaces the registration lines and leaves every consumer
(serialization, inspector, undo/redo) talking to the same `meta_type`/`meta_data`.

### Still to measure before this is safe at scale

- **Allocation across the boundary into a shared registry/context.** entt's own
  docs warn that a plugin creating storage in a shared registry "is now managing
  memory from different spaces". Zig links libc++ **statically into every
  artifact**, so each module carries its own C++ runtime. The prototype touched
  this without incident; it did not stress it. Needs a dedicated case in
  `tests/integration/module_boundary_test.cpp`.
- **Compile-time cost on a realistic component count.** Not measured — the quick
  timings were polluted by zig's cache and are not quoted here.

## Closed 2026-08-04 — the layer is built; its consumers are not

Closed on what it delivers rather than held open for tickets that do not exist,
following the pattern the backlog README sets out (T0095 → T0105,
T0054/T0056 → T0025). Leaving it open at Order 140 would park a blocker at the
top of the queue that is blocking nothing.

### What landed

`hp::reflect<T>()` over `entt::meta`, in `engine/include/hp/Reflect.hpp`:

```cpp
hp::reflect<Position>("Position")
    .property<&Position::x>("x")
    .meta({.min = -1000.0, .max = 1000.0, .tooltip = "world X"})
    .property<&Position::y>("y");
```

- **`adoptMetaContext()` is header-only on purpose.** `entt::locator`'s storage
  is a static per binary, so an exported function would reset the *engine's*
  locator and leave the caller's untouched. Being inline, it is compiled into
  the caller and resets the caller's.
- **`readOnlyProperty<Getter>()`** exists because the house style keeps data
  private — `hp::Guid` exposes `value()`, not a member. A reflection layer that
  only handled public members would have been unusable on the engine's own types.
- **`value<Enumerator>()`** for enums, which must be named individually: neither
  entt nor C++20 can enumerate an enum's enumerators.
- **Container support is included by default** (`entt/meta/container.hpp`), so a
  `std::vector` member is reachable as a sequence without every type saying so.

### Evidence

Both targets, 36 fast cases and 41 integration cases:

```
Build Summary: 18/18 steps succeeded; 21/21 tests passed
+- test (linux-x86_64, fast)          natively                              success
+- test (linux-x86_64, integration)   natively                              success
+- test (windows-x86_64, fast)        as a real Windows process via interop success
+- test (windows-x86_64, integration) as a real Windows process via interop success
```

**53.6 is covered by tests that would fail if it were not**: a nested struct
resolves *as a reflected type* so a serializer can recurse; an enum's values are
named (`team: Hostile`, not `team: 2`); a `std::vector` is reachable as a
sequence container; and a `hp::Guid` round-trips exactly, which matters because a
GUID truncated through a double is a broken asset reference nobody notices until
load.

### The cross-boundary guarantee, proven by mutation

The sharp edge of this subsystem is that it fails **silently**: `entt::locator`
is per binary, so a participant that does not adopt the engine's context sees an
empty one — every type unresolvable, no error, no crash.

Removing `adoptMetaContext()` from the sandbox module:

```
FATAL ERROR: REQUIRE( static_cast<bool>(from_engine_side) ) is NOT correct!
FATAL ERROR: REQUIRE( static_cast<bool>(hp::resolveType("SandboxHealth")) ) is NOT correct!
[doctest] test cases: 41 | 39 passed | 2 failed
```

Restored: 41/41. So those cases are guards, not decoration.

**That took two attempts, and the first was wrong.** The initial mutation
"passed", because the sandbox module was staged beside the test binary with a
`POST_BUILD` copy — which only runs when the *test* target relinks, so editing
only the module left a stale copy. Confirmed by hash: built module `05bc…`,
staged copy `b937…`. A test exercising the previous build is indistinguishable
from a passing test. Fixed by loading the module through a path relative to the
test binary, which cannot go stale.

### What moved, and to where

| Left undone | Now owned by |
|---|---|
| 53.7 component registration in the ECS | **T0021** — there is no registry to hook into yet |
| 53.8 serialization round-trip | **T0022** — the reflection half is tested here |
| "serialization, inspector and undo consume this and nothing else" | **T0022 / T0035 / T0065** |

Each of those has been told what it inherits, rather than this ticket merely
listing them (CLAUDE.md rule 5).

## What is not verified

**Nothing consumes this yet.** Every downstream system is Phase 3 or later, so
the claim is "the layer works and is tested", not "the engine reflects its own
types". There are no engine components to register — T0021 brings the first.

**Compile-time cost is unmeasured.** entt::meta is template-heavy and every
registration instantiates. On the current type count this is invisible; at a
realistic component count it may not be. D19's PCH work (T0048) is the obvious
mitigation and the natural place to measure it.

**Thread safety is not addressed.** entt::meta is not safe for concurrent
mutation; registration must happen on one thread before concurrent access. That
is entt's documented convention and it is not currently enforced or asserted
anywhere — T0050 owns thread ownership rules and should cover it.

**`PropertyMeta` is carried, not interpreted.** `hidden` and `read_only` are
stored and retrievable; nothing acts on them, because the inspector that would
is T0035.
