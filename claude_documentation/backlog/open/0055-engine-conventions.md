# T0055 — Engine conventions and error handling policy

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Trivial |
| **Phase** | 2 — Engine skeleton |
| **Created** | 2026-08-03 |

## Why

Unwritten conventions are how a codebase ends up needing to be hacked. Decisions
like "do we use exceptions" are nearly free to make now and extremely expensive
to change once there is a hundred thousand lines assuming an answer.

Cheap ticket, disproportionate payoff.

## Done when

- [ ] A conventions document exists and is short enough to actually be read
- [ ] Error handling policy decided and applied consistently
- [ ] Assertion policy decided, including what survives into release
- [ ] Naming, file layout and include conventions written down
- [ ] Ownership idioms decided: raw pointers, `unique_ptr`, ref counting, handles
- [ ] The C++ standard and which library features are in bounds

## Subtasks

- [ ] 55.1 **Exceptions: on or off** — see notes, this is the consequential one
- [ ] 55.2 Error handling for recoverable failures: expected/optional/status
- [ ] 55.3 Assert macros: debug-only vs always-on, and their failure behaviour
- [ ] 55.4 Naming and file layout
- [ ] 55.5 Ownership and lifetime idioms, especially across the engine/app boundary
- [ ] 55.6 Write it into `claude_documentation/documentation/`
- [ ] 55.7 Add `.clang-format` and, if worthwhile, `.clang-tidy`

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
