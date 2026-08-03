# Engine conventions

The rules engine and gameplay code are written against. Unwritten conventions
are how a codebase ends up needing to be hacked, and decisions like "do we use
exceptions" are nearly free now and extremely expensive once a hundred thousand
lines assume an answer.

## This document changes

**It is expected to be edited as the engine is built, and that is not a
failure.** Several rules below could only be decided once something existed to
decide them against, and several more are marked provisional because nothing
has exercised them yet. Writing them down early is still worth it: a provisional
rule that everyone follows is better than an unwritten one everyone guesses at,
and a rule that turns out wrong is cheap to change while it is the only thing
depending on itself.

Two kinds of statement appear here:

- **Settled** — decided, and in several cases measured. Changing one is a
  decision-log entry, not an edit.
- **Provisional** — a reasonable default, adopted so there is one answer rather
  than three. Marked 🔶. Expect these to move.

When you change a rule, say why in the commit. A convention whose rationale is
lost gets re-litigated every six months.

---

## Language and standard

**C++20 for engine, editor and gameplay code.** Third-party code keeps whatever
it sets — the root `CMakeLists.txt` sets `CMAKE_CXX_STANDARD 17`, which
DiligentEngine and the vendored libraries inherit, and our own targets raise
themselves with `target_compile_features(... cxx_std_20)`.

Verified rather than assumed: the test targets and the module-boundary fixtures
build and run at C++20 on **both** targets with Diligent at 17, full suite
green. Mixing standards across a link boundary is legal and works here, but it
is the kind of thing that fails on a compiler change, so it is a rule with a
test behind it rather than a preference.

**Not available, despite being C++20-adjacent:** `std::expected` is C++23 and
`std::print` is C++23. Do not reach for them.

---

## Exceptions

**Engine code does not throw.** 🔶 as to the exact boundary helper, settled as
to the policy.

Three forces decide this:

- **Diligent throws internally** (`LOG_ERROR_AND_THROW`), so exceptions cannot
  be disabled build-wide. `-fno-exceptions` is not an option available to us.
- **Throwing across the gameplay module boundary is not safe to rely on.** Each
  shared library carries its own statically linked libc++ (measured — see the
  module boundary section), and unwinding across that is exactly the kind of
  thing that works until it does not.
- Predictable cost matters more than convenience in a frame loop.

So:

1. Our code does not `throw`.
2. Calls into Diligent that can throw are wrapped at the call site, and the
   exception is converted into our error type there. Diligent exceptions never
   propagate into engine control flow, and never reach a module boundary.
3. `catch (...)` at the outermost frame of any callback the engine hands to a
   third-party library, and at every module entry point. An exception escaping
   into foreign code is undefined behaviour, not a stack trace.

---

## Errors that are not exceptional

Distinguish three cases, because conflating them is what produces either
error-code soup or exceptions-as-control-flow:

| Case | Mechanism |
|---|---|
| Absence is normal — "no such component", "not loaded yet" | `std::optional<T>` |
| Failure is expected and recoverable — file missing, parse failed | 🔶 `hp::Expected<T, Error>` |
| The program's invariants are broken | assert, then abort — see below |

🔶 `hp::Expected` does not exist yet and belongs to T0056. It is listed here so
the *shape* is agreed before twenty functions invent their own status enum.
Whether it is a hand-rolled type or a vendored one is T0056's call; **do not
write a bespoke `bool okOut(T* out)` convention in the meantime.**

**Never return a bare `bool` for "did it work".** It loses why, and every caller
then invents a way to find out.

---

## Assertions

Three macros, and the difference between them is *what happens in a shipped
build*:

| Macro | Release build | Use for |
|---|---|---|
| `HP_ASSERT(cond)` | compiled out | Internal invariants; cost is irrelevant because it is gone |
| `HP_CHECK(cond)` | **stays** | Anything whose violation silently corrupts data |
| `HP_UNREACHABLE()` | stays | Marks impossible paths; abort if reached |

**Assertions that all vanish in release are half a policy.** The class of thing
that must survive: preconditions at the module boundary, threading-ownership
rules (T0050), and anything where continuing would write wrong data to a file
the user keeps. A corrupted save that is discovered a week later is worse than a
crash at the moment of the bug.

`HP_ASSERT` must not have side effects, since it is compiled out —
`HP_ASSERT(init())` is a bug that only appears in release.

🔶 Failure behaviour (log-and-abort versus breakpoint-and-continue under a
debugger) is decided with T0054.

---

## Ownership and lifetime

| Idiom | Meaning |
|---|---|
| `std::unique_ptr<T>` | This owns it. The default for heap ownership |
| `T&` / `T*` | Borrowed, non-owning, and outlives the call. **A raw pointer never implies ownership** |
| `RefCntAutoPtr<T>` | Diligent RHI objects — their scheme, not ours; do not wrap it in a second one |
| `std::shared_ptr<T>` | Genuinely shared lifetime, which is rarer than it looks. Justify it |
| Handle (GUID or index) | Assets and entities. Survives reload and serialization; a pointer does not |

**Prefer a handle to a pointer for anything that can be reloaded**, which is
most content. T0058 makes assets hot-reloadable and T0048 reloads the gameplay
module; a raw pointer into either is a dangling pointer waiting for a
convenient moment.

---

## The module boundary

Gameplay is a shared library that links the engine shared library (D12). These
rules are measured, not assumed — see `tests/integration/module_boundary_test.cpp`,
which runs on both targets in `zig build test`.

**Allowed across the boundary:**

- Rich C++ — real types, templates, `entt::registry`. D12's lockstep means
  engine and module are always built by the same pinned toolchain with the same
  flags, which is what makes this safe and is the reason we need no C ABI and no
  generated binding layer.
- **Standard library types.** Same reasoning: one toolchain, one libc++ version.
  This is a real benefit of lockstep and is worth using.
- **RTTI.** `dynamic_cast` and `typeid` on types crossing the boundary **work on
  both targets** — measured, including under PE, where it holds because the
  module imports the typeinfo from the engine DLL. Types crossing the boundary
  must be marked default-visibility (both fixtures build
  `-fvisibility=hidden`); without that their vtables and typeinfo stay local to
  each module and this stops being true.

**Forbidden across the boundary:**

- **Exceptions.** See above.
- **Freeing memory on the other side from where it was allocated.** Each library
  carries its own statically linked libc++, so an allocation crossing the
  boundary must come back to its owner to be released. Prefer handing out
  handles, or an interface with a `destroy()` that the allocating side
  implements.
- **`entt::type_index` as any kind of key.** It is a per-module runtime number:
  the same type gets different values in the engine and in a module (measured,
  T0095). Never persist it, never serialize it, never compare it across the
  boundary. `entt::type_hash` is name-based, is stable across the boundary, and
  is what entt keys its own pools on — use that, or a stable name.

**Stateful engine services are passed, not reached.** A logger, a reflection
registry (T0053), an autoload registry (T0076) should be handed across as a
context object rather than found via a global symbol. With a shared engine a
global *would* resolve to one instance — this rule is about not depending on
that, so the linkage model can change without rewriting every service.

**Unloading a module does not currently work**, and code must not assume it
will. `dlclose` of a library holding any static needing destruction segfaults
the process at exit under this toolchain; modules are loaded `RTLD_NODELETE` and
stay resident. T0048 owns the real answer. Practical consequence today:
**avoid statics requiring destruction in gameplay modules**, and do not write
teardown logic that only runs via a static destructor.

---

## Naming and layout

🔶 Adopted so there is one answer; cheap to change before there is code.

```
engine/
  include/hp/…      public headers — consumers write #include <hp/Application.hpp>
  src/…             implementation
```

| Thing | Style |
|---|---|
| Types, structs, enums | `PascalCase` |
| Functions and methods | `camelCase` |
| Variables and parameters | `camelCase` |
| Private data members | `trailing_` |
| Constants and enum values | `PascalCase` |
| Macros | `HP_SHOUTY` |
| Namespace | `hp`, one level; sub-namespaces only where a name genuinely collides |
| Files | Match the primary type: `Application.hpp` / `Application.cpp` |

**Include order:** own header first, then engine headers, then third-party, then
standard library. Own-header-first means a header that forgets its own includes
fails immediately rather than in the one translation unit that includes it late.

Headers use `#pragma once`. Third-party headers are included with `<>` and
angle-bracket include paths marked `SYSTEM` in CMake, so their warnings are not
ours.

---

## Formatting

`.clang-format` at the repository root is authoritative. Run it; do not argue
with it — the value of a formatter is that it ends the discussion.

```sh
clang-format --style=file -i <files>                    # format
clang-format --style=file --dry-run -Werror <files>     # check, exit 1 if not formatted
```

LLVM style with 4-space indent, 100 columns, and the pointer bound to the type
(`void* p`). It was derived from the code already in `tests/` rather than
imposed on it, then applied to that code, which is now in canonical form —
verified idempotent, and the whole suite still passes on both targets after
reformatting.

**Only our code.** Nothing under `third_party/` is formatted; not modifying
vendored trees is a build-harness rule (D1) as much as a style one.

Validated with **clang-format 14**. The version matters: several options here
(`SeparateDefinitionBlocks`, `ReferenceAlignment`, `EmptyLineBeforeAccessModifier`)
were added in 13–14, so an older binary will reject the config rather than
silently ignore them. It is not part of the pinned toolchain (D5) — it is a
developer tool, not a build input, and the build does not depend on it.

---

## Math, containers and memory

**Math is Diligent's** — `BasicMath.hpp` and `AdvancedMath.hpp`. 🔶 as to which
helpers we wrap; settled as to the library.

Not glm, and the reason is not preference. Every renderer call takes Diligent
math types, so a second vector and matrix library means conversions at every
boundary — cheap individually, everywhere, forever, and a constant source of
"which convention is this in" bugs. The two also disagree about row versus
column major, which is the kind of mismatch that produces a transposed matrix
once a month. Diligent ships `BasicMathSSE.hpp` and `BasicMathNEON.hpp`, so
using it is not a performance concession.

The corollary: **gameplay uses the same types**, not an engine-specific wrapper
over them. They are header-only value types with no linkage, so they cross the
module boundary safely (D12) — the boundary hazard is *stateful* engine code and
RHI interface pointers, not PODs.

**Containers are the standard library's.** No EASTL, no bespoke vector. The
justification for replacing them is allocator control and debug-build
performance, and neither has been measured here — adopting a container library
on the strength of other projects' measurements is how a codebase acquires a
dependency nobody can remove. Revisit with a profile, not an argument.

`std::vector`, `std::unordered_map` and friends until something demonstrates
otherwise. Where a hot path needs different behaviour, change *that* path.

**Memory** 🔶 — the strategy is: default to ordinary allocation, and introduce
pools and scratch allocators only where a profile justifies them. Diligent
provides `DynamicLinearAllocator` for per-frame scratch and
`FixedBlockMemoryAllocator` for pooling, so those are adoptions rather than
implementations when the time comes. **Adopting them is blocked** on the engine
linking Diligent at all, which T0013 deliberately deferred.

One rule that is not provisional, because it is a correctness constraint rather
than a performance one: **memory does not cross the module boundary to be
freed** — see the module boundary section above.

## Profiling instrumentation

Zones are added **as code is written**, not retrofitted. That is the whole
reason the macro surface (T0019) landed before the systems it instruments —
retrofitting instrumentation across an engine is the kind of sweep nobody ever
schedules.

```cpp
#include <hp/Profiling.hpp>

void Renderer::submit() {
    HP_PROFILE_ZONE();                       // named after the function
    for (const auto& batch : batches_) {
        HP_PROFILE_ZONE_NAMED("batch");      // explicit name
        ...
    }
    HP_PROFILE_VALUE("draws", drawCount_);   // correlate a spike with its cause
}
```

Where they belong: any function that runs per frame, any loop over entities or
assets, anything doing IO, and every thread entry point (`HP_PROFILE_THREAD`).
Where they do not: trivial accessors, and inside a zone that already covers the
same work at a useful granularity — a profile with ten thousand zones per frame
is harder to read than one with fifty.

**Disabled means absent, not skipped.** With `HP_PROFILING` off the macros
expand to nothing and do not name their arguments, so `HP_PROFILE_VALUE("n",
expensiveCount())` does not call `expensiveCount()`. Verified: the emitted
assembly for an instrumented function is identical to the same function without
the macros, and a test asserts arguments are never evaluated.

The consequence to be aware of: a variable used *only* inside a profiling macro
looks unused when profiling is off. That trade is deliberate — a warning is
cheaper than a call in a shipped build.

**`HP_PROFILING` is PUBLIC, and part of the ABI.** Every consumer compiles
against the same setting as the engine, because enabling it changes the engine's
symbol surface. A gameplay module built with profiling against an engine built
without it is exactly the mismatch T0104 refuses at load.

**Two switches, and only one preserves performance.** `HP_PROFILING` is compile
time and is the one that removes cost. An editor toggle controls *capture* —
whether a connected profiler is recording — and cannot recover shipped-build
cost, because the instrumentation is still compiled in. Say so in the UI.

## Documenting public API

Public headers under `engine/include/hp/` are the API surface for gameplay code
— and, increasingly, for a coding agent writing it (T0118). `zig build docs`
generates `docs/api/` from them and **checks them**, so this is enforced rather
than encouraged.

**Every parameter gets an `@param`.** Not because tags are inherently good, but
because the generator can verify them: it knows the real parameter names, so it
catches both a missing tag and — the one that matters — an `@param` naming a
parameter that was since renamed.

```cpp
/// Registers `name`, or returns the existing id if already registered.
///
/// @param name the channel name, e.g. "render" or "game.combat". Two
///        declarations of the same name resolve to one channel.
explicit LogCategory(std::string_view name);
```

**Keep writing the prose.** The tags are for the checker; the paragraph
explaining *why* is what stops an agent misusing the API, and it is reproduced
in full in the generated reference. A tag-only comment is a worse comment.

**`@returns` where the return is not obvious from the signature.** `int frame()
const` needs nothing. `std::optional<Guid> parse(std::string_view)` needs to say
what `nullopt` means.

**Trivial accessors and special members need no comment.** A doc comment on
`Clock& clock()` saying "returns the clock" is noise, and noise in a reference an
agent reads costs more than silence. The checker tracks these in
`tools/api_docs_baseline.txt`, a ratchet that may shrink but never grow.

**A stale `@param` is never acceptable** and cannot be baselined. A missing
comment leaves a reader to work it out; a wrong one tells them something untrue
with confidence. That distinction is the whole reason the check exists.

## What is deliberately not decided here

- **Math, memory and container policy** — T0056. Whether we use Diligent's math
  types in gameplay, what allocator strategy exists, whether any container is
  replaced.
- **Logging** — T0054, including what assertion failure actually does.
- **Threading ownership rules** — T0050. The rule that "`HP_CHECK` survives
  release for threading invariants" is here; what those invariants *are* is
  there.
