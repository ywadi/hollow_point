# T0104 — Build id stamping and module compatibility checks

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 2 — Engine skeleton |
| **Order** | 130 |
| **Created** | 2026-08-03 |
| **Blocks** | T0048 |
| **Refs** | [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D12, T0095 |

## Why

D12 lets rich C++ cross the engine/gameplay boundary — real types, templates,
entt registries — instead of a C ABI with a generated binding layer. That is a
large ergonomic win and it rests on one assumption: **the module was compiled
against exactly this engine.**

When the assumption holds, everything works. When it does not, nothing announces
it. A module built against an engine whose class layout has since changed will
load, resolve its symbols, and then read fields at the wrong offsets. The
symptom is corrupted data or a crash somewhere unrelated, arbitrarily far from
the cause, and the usual debugging instinct — read the gameplay code — leads
away from the actual problem.

This ticket is the guard that makes D12 safe. It is deliberately small and
deliberately not optional.

## Done when

- [ ] The engine carries a build id derived from something that actually changes
      when the ABI changes
- [ ] Every gameplay module is stamped with the id of the engine it was built
      against
- [ ] Loading a module whose id does not match the running engine **fails at
      load**, before any of its code runs, with both ids in the message
- [ ] The failure is a clear diagnostic, not an assert or a crash — it names
      what to rebuild
- [ ] A test loads a deliberately mismatched module and asserts the refusal
- [ ] Works on both targets

## Subtasks

- [ ] 104.1 Decide what the id is derived from (see notes — this is the whole
      design)
- [ ] 104.2 Generate it at configure/build time and compile it into the engine
- [ ] 104.3 Stamp it into every gameplay module, via the same mechanism, so a
      module cannot accidentally be built without one
- [ ] 104.4 Check it in the module loader before calling any module entry point
- [ ] 104.5 Test the refusal path with an intentionally stale module — red-green,
      not by observing a pass
- [ ] 104.6 Make the message actionable: which module, which id it wants, which
      id is running, and what to rebuild

## Notes / findings

**What the id derives from is the entire design decision.** Options, roughly in
increasing order of accuracy and cost:

- **A version string bumped by hand.** Free, and worthless — the failure mode
  is forgetting to bump it, which is exactly the case that needs catching.
- **Git commit hash of the engine.** Cheap and automatic, but wrong in both
  directions: a docs-only commit invalidates every module, and an uncommitted
  local header edit invalidates nothing.
- **A hash of the public engine headers.** Closest to "did the ABI change",
  which is the actual question. Costs a build step that hashes the header set,
  and needs care that the set is complete.
- **Compiler identity and flags folded in as well.** D12 assumes one toolchain,
  and the pinned toolchain (D5) makes that near-certain — but a Debug engine and
  a Release module differ in ways a header hash will not catch.

A defensible first answer is the header hash plus build type plus target triple.
Start there and record why in the ticket rather than the code.

**Do not let this become a version negotiation system.** The check is
equal-or-refuse. Ranges, compatibility windows and "minor versions are
compatible" are all how this grows into something that has to be maintained and
reasoned about, and D12's lockstep means there is exactly one right answer at
any moment. If lockstep is ever abandoned, this ticket's design is the wrong
starting point anyway.

**The editor is a module host too** (see the T0032/T0035 amendments). The check
belongs in the shared loader, not in the runtime's copy of it, or the editor
will happily load a stale module and blame the inspector.
