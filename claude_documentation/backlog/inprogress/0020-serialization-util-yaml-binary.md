# T0020 — Serialization util: rapidyaml + binary cook

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 3 — Data model |
| **Order** | 190 |
| **Created** | 2026-08-02 |
| **Refs** | T0016, T0082, [../inprogress/0068-input-mapping.md](../inprogress/0068-input-mapping.md) |

## Why

Everything in Phase 3 serializes — scenes, asset metafiles, project files,
EditorState. Building that on an ad-hoc basis per system produces four
inconsistent formats. One util, used everywhere.

The chosen model: **YAML is the source of truth, binary is a derived cache.**
YAML is what gets written on save, diffs in git and can be hand-edited; a binary
form is cooked from it for fast loads, and only the binary ships. That gets
authoring ergonomics and load speed without choosing between them.

## Done when

- [x] rapidyaml vendored as a submodule and cross-compiling to both targets
- [x] A wrapper API that engine code uses, so rapidyaml is swappable
- [~] Round-trip: object → YAML → object, value-identical
- [~] Cook: YAML → binary, and load binary → object, value-identical
- [x] Staleness detection — binary is rebuilt when its YAML changes
- [~] Tests round-trip every supported type through both paths

## Subtasks

- [x] 20.1 Vendor rapidyaml (`biojppm/rapidyaml`) at a pinned tag; confirm it
      cross-compiles to `x86_64-windows-gnu` (it is CMake C++, expected fine)
- [x] 20.2 Wrapper API — do not let `ryml::` types escape into engine headers
- [ ] 20.3 Serialization concept/traits so types opt in uniformly
- [x] 20.4 Binary writer/reader with a version header and endianness decision
- [x] 20.5 Staleness: content hash of the YAML stored in the binary, not mtime
- [x] 20.6 Tests for both paths and for a deliberately corrupt binary

## Notes / findings

## Progress — 2026-08-05

**The foundation is in; 20.3 is not.** `hp/Yaml.hpp` and `hp/Cook.hpp` are built,
tested on both targets and documented. What does not exist is the traits and
reflection layer that turns a *component* into YAML — see "Not done".

### 20.1 — "expected fine" was wrong, and the reason is worth keeping

The ticket guessed rapidyaml would cross-compile without trouble because it is
CMake C++. It does not, and the cause is a genuine upstream bug rather than a
configuration miss.

`c4core/src/c4/compiler.hpp` nests its MinGW test **inside the GCC branch**:

```cpp
#elif defined(__clang__)         // <- zig cc lands here
#    define C4_CLANG
#elif defined(__GNUC__)
#    ifdef __MINGW32__
#        define C4_MINGW         // <- never reached
```

zig cc targeting `x86_64-windows-gnu` defines `__clang__`, `__MINGW32__` **and**
`__MINGW64__` simultaneously — verified with `zig cc -target x86_64-windows-gnu
-dM -E`. So it takes the clang branch, `C4_MINGW` is never defined, and
rapidyaml's `common.hpp` falls through to `#include <alloca.h>`, which MinGW does
not have — it puts `alloca` in `<malloc.h>`. Every translation unit touching ryml
fails to compile, Windows only.

Fixed by defining `C4_MINGW` on the target rather than patching the vendored
tree: the condition is genuinely true, and the symbol has exactly **one** other
use in all of rapidyaml and c4core — the `malloc.h`/`alloca.h` switch it exists
for. `PUBLIC`, so our own translation units agree with the library; disagreeing
would be an ODR problem rather than a compile error.

It brings c4core, which brings three more submodules (cmake, debugbreak,
fast_float). That is the deepest transitive vendoring in the tree and it is the
price of the parser.

### 20.2 — the wrapper carries real weight

No `ryml::` or `c4::` type appears in a public header. Two things it buys beyond
swappability:

- **The document owns its text.** rapidyaml parses *in place* and its nodes point
  into the source buffer — fast, and a footgun, because a node outliving its
  string reads freed memory. `YamlDocument` holds the buffer so callers cannot
  get it wrong.
- **Everything written is arena-copied.** rapidyaml stores spans, so passing it a
  temporary leaves a dangling span that emits garbage much later somewhere else
  in the file. A test covers exactly this.

Also: rapidyaml's default error callback calls `abort()`. A malformed file is
*data*, so that would take the editor down precisely when someone has hand-edited
a scene badly and needs a message. The callback is replaced with one that throws,
and `parse` catches and returns `nullopt`.

**rapidyaml is more permissive than the YAML spec in places.** A leading tab is
invalid indentation per spec; rapidyaml accepts it as scalar content. A test
asserted rejection and the parser proved otherwise, so the test now records the
real behaviour.

### 20.4 / 20.5 — the container

`kCookFormatVersion`, the caller's schema version (T0082.5), an FNV-1a hash of
the source, and a payload length. Little-endian **chosen and asserted**, not
assumed — both targets are x86-64 so it costs nothing today, and writing it down
makes a future big-endian target a conversion in one place instead of a silent
corruption everywhere.

Every failure mode returns a `CookStatus`, never an exception, because the only
correct response to any of them is to re-cook. 20.6's corrupt-binary cases sweep
a flipped byte through the whole header and assert none of them reads as `Ok`.

`readString` refuses a length exceeding the remaining bytes rather than trusting
it — the classic deserializer failure where a corrupt length makes the reader
allocate gigabytes or walk off the buffer, both of which surface as a crash with
no connection to the file that caused it.

### Evidence

`tests/fast/serialization_test.cpp`, 22 cases. Full suite on both targets: fast
145, integration 73, gpu 2. `zig build docs` passes.

The GUID round-trip T0016 deferred ("there is no serializer yet") is closed here
and treated as a first-class case rather than an incidental uint64.

## 20.3 groundwork — reflection did not expose property names (2026-08-05)

**Found on the first step of 20.3, and it had been wrong since T0053 shipped.**

`TypeBuilder::property` passed the name to entt only as a hashed id:

```cpp
factory_.template data<Member>(entt::hashed_string{name});   // id only
```

entt's `data()` takes the name as a **second** parameter. Without it, every
property enumerated through `meta_data::name()` returned **nullptr**. Measured:
all three properties of `Transform` reported null.

**Nothing caught it because everything that existed looked properties up by
id**, which worked perfectly. The breakage is entirely on the *enumeration*
path — which is exactly the path 20.3 is built on, and which the other two
T0053 consumers need too:

| Consumer | What a null name costs |
|---|---|
| Serialization (20.3) | No readable YAML key — the whole point of YAML as source of truth |
| Inspector (T0035) | No field label |
| Undo/redo (T0065) | Cannot describe what changed |

Fixed by passing the name alongside the hash in `property`, `readOnlyProperty`
and `value`. **The id is unchanged** — still the hash of the same string — so
this is not a data-format change and nothing already written becomes
unreadable. Only the name became available.

`tests/fast/reflect_test.cpp` now pins it, asserting the enumerated name *and*
that the enumeration key still equals the hash, because the point is that the
two agree.

This is why 20.3 is listed as "not started" rather than "in progress": the
groundwork it needed turned out to be a defect somewhere else.

## Not done

- **20.3 is not started, and it is the largest remaining piece.** The
  reconciliation with T0053 stands: reflected types get serialization *derived
  from* property enumeration, and hand-written traits exist only for the leaf
  types reflection bottoms out in — `Guid`, `float3`, `Quaternion`, `float4x4`,
  `std::string`, containers. Neither layer exists. Until it does, "object → YAML
  → object" is true only for the primitives a caller writes by hand.
- **Nothing cooks an object yet.** `hp/Cook.hpp` is a container plus primitives;
  what goes *in* the payload is 20.3's business.
- **No VFS integration.** Neither header reads or writes files. That is
  deliberate — they operate on strings and byte vectors, so they are testable
  without a filesystem — but it means the "load a cooked asset" path does not
  exist end to end. T0023 is where the two meet.
- **T0068.7 (input binding files) is still waiting.** The serializer it needed
  now exists for scalars and sequences; whether it needs 20.3 depends on how
  `InputMap` is written.
- **No streaming or partial parse.** Whole documents only.


**Hash, not mtime.** Timestamps lie after a git checkout, a copy, or a clock
change, and a stale binary that loads without error is a genuinely nasty class of
bug. Store the source hash inside the binary and compare.

**Version the binary format from the first commit.** A binary with no version
field cannot be migrated, only discarded — and it will need migrating.

The binary format is a *cache*, so it must always be safely discardable: if it is
missing, stale or unreadable, the correct behaviour is silently re-cooking from
YAML, never failing. That property is also exactly what Phase 8 export needs.

rapidyaml is chosen over yaml-cpp for speed and active maintenance; the wrapper
is what makes that reversible. Its API is lower-level (tree-based), so the
wrapper carries more weight than it would with yaml-cpp — that is expected.

### Architecture review (2026-08-03) — reconcile 20.3 with reflection (T0053)

This ticket predates T0053 by a day and subtask 20.3 ("serialization
concept/traits so types opt in uniformly") now half-overlaps it. If traits and
reflection both exist as opt-in mechanisms, components get serialized two ways
and they drift — the exact four-switches problem T0053 exists to kill. The
reconciliation: **reflected types get serialization *derived from* T0053's
property enumeration automatically**; hand-written traits are only for the
leaf/primitive types reflection bottoms out in (GUID, math types, strings,
containers). 20.3 should be read as "define the leaf-type layer reflection
sits on", not as a parallel per-component mechanism.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0016** deliberately left the GUID binary round-trip untested — "there is
  no serializer yet (T0020)". The every-type round-trip in 20.6 is what closes
  that gap; treat GUID as a first-class case there, not an incidental uint64.
- **T0082.5** ties the binary cache to the schema version: the version header
  in 20.4 must participate in staleness alongside the content hash, or a
  schema change silently loads a stale cook that still parses.

### Cross-ticket obligation — T0068 (2026-08-04)

**T0068.7 is waiting here, and it is a loader rather than a design.** The action
layer is built and its bindings are deliberately *data* — an `InputMap` is a
list of (action, physical input) pairs plus per-axis tuning — so serializing
them needs a serializer to write against and nothing else.

Two properties of that data are worth knowing before designing the format:

- **An action's identity is a name hash (FNV-1a of e.g. `"Jump"`)**, not an
  index. A binding file must therefore store the *name*; storing the hash would
  work and would be unreadable and unfixable by hand, and storing an index would
  break the moment someone inserts an action.
- **User-rebindable means round-tripping**, so the format needs to survive being
  written by the engine and edited by a person, which is the argument for the
  text side of 20.x rather than the binary cook.

Key *display* names for a rebinding UI are T0112's concern, not this one — see
T0068's own note.

### Cross-ticket obligation — T0112 (2026-08-04)

**The English string table is a plain data asset, and this ticket picks its
format.** T0112 settled that all player-facing text is authored as a key
resolved through one authoritative English table, and deliberately did *not*
decide how that table is stored — it is an ordinary asset through the VFS (D13),
so whatever this ticket lands is what it uses. Nothing new is required of the
format; the point is that the table must not grow its own bespoke loader when
the time comes.

Two properties it needs, both of which the rebinding-file argument above already
implies: it round-trips human edits (translators and writers edit it by hand),
and it survives entries being inserted and reordered, since keys are the identity
and line position means nothing.
