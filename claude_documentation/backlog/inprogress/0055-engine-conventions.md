# T0055 — Engine conventions and error handling policy

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Trivial |
| **Phase** | 2 — Engine skeleton |
| **Order** | 10 |
| **Created** | 2026-08-03 |

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
