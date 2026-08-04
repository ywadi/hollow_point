# T0055 — Engine conventions and error handling policy

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Trivial |
| **Phase** | 2 — Engine skeleton |
| **Order** | 10 |
| **Created** | 2026-08-03 |
| **Refs** | [../completed/0127-exceptions-across-the-module-boundary.md](../completed/0127-exceptions-across-the-module-boundary.md) (amends this) |

## Why

Unwritten conventions are how a codebase ends up needing to be hacked. Decisions
like "do we use exceptions" are nearly free to make now and extremely expensive
to change once there is a hundred thousand lines assuming an answer.

Cheap ticket, disproportionate payoff.

## Done when

- [x] A conventions document exists and is short enough to actually be read — [06-engine-conventions.md](../../documentation/06-engine-conventions.md)
- [x] Error handling policy decided — **not yet "applied consistently", because there is no engine code to apply it to.** That half is verified when Phase 2 code exists
- [x] Assertion policy decided, including what survives into release
- [x] Naming, file layout and include conventions written down
- [x] Ownership idioms decided: raw pointers, `unique_ptr`, ref counting, handles
- [x] The C++ standard and which library features are in bounds — C++20 for our code, measured against Diligent at 17 on both targets

## Subtasks

- [x] 55.1 **Exceptions: on or off** — see notes, this is the consequential one
- [x] 55.2 Error handling for recoverable failures: expected/optional/status
- [x] 55.3 Assert macros: debug-only vs always-on, and their failure behaviour
- [x] 55.4 Naming and file layout
- [x] 55.5 Ownership and lifetime idioms, especially across the engine/app boundary
- [x] 55.6 Write it into `claude_documentation/documentation/`
- [x] 55.7 Add `.clang-format` — written, validated with clang-format 14, and
      applied to every file under `tests/`. Verified idempotent and verified
      able to *fail* on an unformatted file, so the check is real.
      **`.clang-tidy` deliberately not attempted**: it is worth having only
      once there is engine code to lint, and a tidy config with no code to run
      against is a guess

## Notes / findings

**Exceptions are the decision to make deliberately.** Many engines disable them
for predictable performance and simpler ABI boundaries. Two constraints here:

- **Diligent uses exceptions internally** (`LOG_ERROR_AND_THROW`), so they cannot
  be disabled outright — the choice is whether *our* code throws.
- The **hot-reloadable gameplay module** (T0048) crosses a shared-library
  boundary, and throwing across that is fragile. That argues for a
  non-throwing interface at least at that boundary.

A workable position: engine code does not throw; failures are returned; Diligent's
exceptions are caught at the boundary where we call into it. Whatever is chosen,
write it down.

**Assertions that vanish in release are half a policy.** Decide what class of
invariant is important enough to check in shipped builds — the threading
ownership rules in T0050 are a good candidate.


## Notes / findings

**Two rules were measured rather than asserted**, using the T0095 boundary
fixtures:

- **RTTI works across the module boundary** on *both* targets — `dynamic_cast`
  from the module on an engine-created object succeeds and `typeid` names
  agree. The convention is therefore "RTTI is allowed", not "RTTI is banned",
  which was the pessimistic expectation going in. It holds on PE because the
  module imports the typeinfo from the engine DLL, so it is contingent on D12's
  shared engine rather than being free.
- **C++20 for our code works alongside Diligent at 17.** The test targets and
  fixtures were raised to `cxx_std_20` and the full suite stayed green on both
  targets. Mixing standards across a link boundary is legal but fails in
  interesting ways on compiler changes, so it is recorded as a rule with a test
  behind it.

**The document says it will change, deliberately.** Several rules could not be
decided without something to decide them against, and provisional ones are
marked. A conventions doc that presents guesses as settled is one people stop
trusting the first time a guess turns out wrong.

**What is deferred, and to where:** `hp::Expected`'s implementation to T0056
(the shape is fixed here so twenty functions do not invent their own status
enum), assertion *failure behaviour* to T0054, threading invariants to T0050.

**`.clang-format` is now validated and applied.** clang-format 14 was installed
mid-ticket, which turned 55.7 from a config file nobody had executed into one
that is verified: it loads, it is idempotent over `tests/`, and it correctly
*rejects* a deliberately unformatted file — the last check being the one that
proves the dry-run mode is worth putting in CI later. The suite still passes on
both targets after the reformat.

The version dependency is worth knowing: `SeparateDefinitionBlocks`,
`ReferenceAlignment` and `EmptyLineBeforeAccessModifier` were added in
clang-format 13–14, so an older binary rejects this config outright rather than
ignoring the keys. It is deliberately not added to the pinned toolchain (D5) —
formatting is a developer tool, not a build input.

`.clang-tidy` is left undone on purpose rather than forgotten: a lint config
written before there is any engine code to lint is a guess at what will matter.


## Closing note

**One Done-when is ticked with a caveat, and it is worth being explicit about
rather than quietly counting it.** "Error handling policy decided **and applied
consistently**" — the policy is decided and written down; *applied* cannot be
true, because there is no engine code to apply it to. That half is inherited by
the first code that lands (T0013, T0014), and the condition as worded can never
be "done" for a living convention document anyway. The ticket is closed on the
decisions being made and recorded, which is what it existed for.

### Evidence

Document: [`documentation/06-engine-conventions.md`](../../documentation/06-engine-conventions.md).

Two rules measured rather than assumed, using the T0095 boundary fixtures:

```
$ zig build test -Dtest=integration
module_boundary_test.cpp:336: MESSAGE: dynamic_cast across the boundary from the module: WORKS
module_boundary_test.cpp:338: MESSAGE: typeid names agree: 1
[doctest] test cases:  7 |  7 passed | 0 failed | 0 skipped
[doctest] assertions: 38 | 38 passed | 0 failed |        (x2 -- Linux, and Windows under wine)
```

C++20 for our code alongside Diligent at 17 — test targets and fixtures raised
to `cxx_std_20`, full suite green on both targets.

`.clang-format`, validated with clang-format 14 rather than merely written:

```
$ clang-format --style=file --dump-config >/dev/null && echo ok
ok
$ clang-format --style=file --dry-run -Werror $(git ls-files 'tests/**/*.cpp' 'tests/**/*.h' 'tests/*.cpp')
(no output -- idempotent, every file already canonical)

$ printf 'int  f( )  {return  1;}\n' > ugly.cpp && clang-format --style=file:.clang-format --dry-run -Werror ugly.cpp
ugly.cpp:1:4: error: code should be clang-formatted [-Wclang-format-violations]
```

That last one is the check that matters — it proves the dry-run gate can fail,
so it is worth adding to CI later. Applied to all six files under `tests/`, and
the full suite passes afterwards.

## Amendment landed — T0127 (2026-08-04)

**55.1's exception policy is now measured, and it has a platform split.** This
ticket argued that "each library carries its own statically linked libc++ and
throwing across that is fragile", and chose a non-throwing interface on that
reasoning. T0127 measured *how* it fails: on the **Linux** target a typed
exception thrown in a gameplay module is caught only by `catch (...)` — both
`catch (const std::runtime_error&)` and `catch (const std::exception&)` are
silently not taken — while the identical source on the **Windows** target
matches by type correctly.

**The evidence above is why nobody caught it.** The measurement pasted in this
ticket is `typeid names agree: 1`. libc++ on ELF selects **pointer** comparison
for typeinfo, not the deep string comparison it uses on COFF, so the assertion
that passed was never the one that governs `catch` matching. The rule this
ticket wrote is right; the reason recorded for it was weaker than the truth.

**Amended 2026-08-04 (T0127.4).** `06-engine-conventions.md` no longer hedges:
the "not safe to rely on" bullet now states the platform split as a fact, and a
new *Exceptions and the module boundary* section carries the measured table, the
symbol-level cause, and what is enforced versus merely conventional.

The amendment also records something 55.1 could not have known: **a typed catch
across the boundary is not uniformly broken on Linux.** It works for an
engine-owned type with default visibility and an out-of-line key function — one
typeinfo, shared — and fails for every `std::` type and for any engine-owned
type caught through a `std::` base. So the shape that works is the one nobody
writes, and the conclusion 55.1 reached ("a non-throwing interface at least at
that boundary") is right for a sharper reason than the one given.

Still not enforced: rule 3's `catch (...)` at module entry points is a
convention with nothing checking it. That is handed to **T0048**, which defines
what a module entry point is.

**A dangling pointer found while checking this:** the conventions doc routes
recoverable failures to `hp::Expected` and says it "belongs to T0056" — but
T0056 is **closed** and never mentions `Expected`, and no `hp::Expected` exists
in `engine/`. So the house style for module-to-engine error reporting is agreed
in shape and has no type behind it. That is T0127.5's question, answered here:
the shape is decided, the type is not built, and the ticket the doc points at is
already closed.
