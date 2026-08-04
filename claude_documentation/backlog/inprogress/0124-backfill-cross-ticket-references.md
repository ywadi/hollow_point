# T0124 — Backfill cross-ticket references across the whole backlog

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 1 — Harden the build |
| **Order** | 3 |
| **Created** | 2026-08-04 |
| **Found by** | T0100 |
| **Refs** | T0100, T0102, T0121, T0122, [../../../CLAUDE.md](../../../CLAUDE.md), [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D17, D18 |

## Why

`CLAUDE.md` now requires that tickets constraining each other point **both**
ways:

> If your ticket constrains another one — you defined a contract it must obey,
> named a phase it must fill, or decided something it will have to live with —
> add the reference to *that* ticket's `Refs` [...] A one-way reference is not
> enough. Nobody reads the ticket they have already closed.

That rule was written during T0100 and applied only to T0100's own 15 consumers.
**The other 120-odd tickets predate it.** The backlog is full of one-way
references and of constraints recorded in prose that the constrained ticket
never learns about.

The failure this prevents is not theoretical — it already happened twice in one
day:

- **T0121** was worked from a state where `17f50b1` had added a build-tree cache
  to the Windows job only. Nothing in the Linux job's ticket said the Linux job
  was the one that mattered, so the gap survived a commit whose message claimed
  to have fixed it.
- **T0102** fixed host-keying for `.harness/` and stopped there. No ticket
  recorded that `.zig-cache/` and `build/` were left sharing a path, so T0122
  had to rediscover it — and then got the cause wrong on the first attempt
  because the T0102 precedent was the nearest available story.

Phases 3 and later are months out. That is precisely when a reference is worth
the most and least likely to be remembered.

## Done when

- [x] Every ticket that another ticket constrains carries the reference in its
      own `Refs`, with a one-line statement of what it must honour — not a bare
      ticket id *(every constraint judged real in the sweep; the deliberate
      omissions are listed under "Considered and not asserted" below)*
- [x] Decision-log entries that bind a ticket are linked from that ticket,
      D17 and D18 included *(D17: all 15 frame consumers already linked it;
      D18 binds developer workflow, not any open ticket, and is linked from
      T0102/T0122 which it governed; D15 was the one-way case — fixed at
      T0025/T0080)*
- [x] `python3 tools/check_backlog.py` passes, so every added link resolves
- [ ] A spot-check sample is read by a human and confirms the one-liners say
      something useful, rather than restating the title of the ticket they
      point at — **cannot be self-satisfied; awaiting the owner's read**

## Subtasks

- [x] 124.1 Sweep all tickets and build the dependency graph that is currently
      implicit in prose — "X must happen before Y", "Y assumes X's format",
      "this is decided in D_n"
- [x] 124.2 Add the missing back-references, each with its one-line obligation
- [x] 124.3 Reconcile against the `Blocks` / `Blocked by` fields already in use
      (T0095→T0105, T0053→T0022/T0035/T0062, T0110→T0025, T0111→T0046) so the
      two mechanisms agree rather than duplicating
- [x] 124.4 Flag, without inventing answers, any dependency found that
      contradicts the current `Order` — a ticket that must come first but is
      sequenced later is a real problem, and this sweep is the cheapest time to
      notice it
- [x] 124.5 Run `check_backlog.py`; fix what it catches

## Notes / findings

**Requested by the owner as a Fable-agent pass.** The work is broad, mechanical
in shape but not in judgement, and touches every file in the backlog — a good
fit for a dedicated agent with the whole set in context rather than incremental
edits.

**The judgement is the hard part, not the linking.** A back-reference that says
"see T0100" is worthless; the value is entirely in the one line stating what the
receiving ticket must do. T0100's 15 back-references are the worked example of
the standard: each names the phase and the failure it prevents.

**Do not invent dependencies.** If the sweep is unsure whether X constrains Y,
it should say so rather than assert it. A confidently wrong dependency is worse
than a missing one — it will be trusted and sequence work incorrectly, which is
exactly how T0122's first diagnosis went astray.

### 2026-08-04 — the sweep, done

All 94 open, 1 in-progress and 30 completed tickets were read in full, plus the
decision log. Every claimed gap was verified by grep against the receiving file
before editing — 31 candidate gaps checked, **all 31 confirmed real** (zero
false positives; the discipline of checking caught nothing, which is itself
evidence the prose-reading was careful).

**Added: 51 one-line obligations across 32 tickets**, each under a
`### Cross-ticket obligations (2026-08-04, T0124 backfill)` heading (T0044's is
titled for its owner-decision list), with the constraining ticket added to the
receiving ticket's `Refs`. Three more tickets (T0035, T0046, T0062) received
Refs-header promotions only, because their obligations were already stated in
body prose. Edited: T0020, T0022, T0023, T0025, T0028, T0031, T0035, T0036,
T0037, T0043, T0044, T0045, T0046, T0048, T0051, T0052, T0059, T0061, T0062,
T0063, T0064, T0066, T0068, T0073, T0074, T0075, T0078, T0079, T0080, T0081,
T0083, T0086, T0096, T0099, T0109.

The highest-value single find: **T0118 → T0109** (the installed-engine SDK must
ship `docs/api`; T0118's closing note stated the expectation and T0109 never
heard of it — Phase 8, months out, exactly the case this ticket exists for).
Close behind: **T0103 → T0083** (T0103's own `Blocks` field names T0083, yet
T0083 carried no acknowledgment at all — the only genuine gap among the
existing Blocks pairs) and **D15 → T0025** (the decision log tells T0025 to
check its GL fallback against the 4.3 compute floor; T0025 never mentioned it).

### 124.3 — reconciliation results

- **T0053 → T0022/T0035/T0062**: all three acknowledged it in prose; none
  carried it in `Refs`. Promoted as `T0053 (Blocks this)` on all three.
- **T0110 → T0025** and **T0111 → T0046**: same — prose yes, header no.
  Promoted.
- **T0104 → T0048**: prose yes ("T0104 owns it and blocks this ticket");
  promoted into T0048's Refs.
- **T0103 → T0023/T0043**: already carried in prose (the D13 notes); Refs
  promoted. **T0103 → T0083** was the genuine gap — see above.
- **T0095 → T0105** and **T0054/T0056 → T0025**: already two-way. Nothing
  done.

The mechanisms now agree: every `Blocks` field has a matching reference on the
blocked side.

### 124.4 — Order findings

**No hard contradiction.** Every Blocks pair sequences correctly:
T0104(130) < T0048(150); T0053(140) < T0022(250)/T0062(270)/T0035(630);
T0103(180) < T0023(220) < T0043(770) < T0083(780); T0110(375) < T0025(380);
T0111(385) < T0046(390); T0106(545) < T0080(550). Nothing was renumbered.

Two flags worth the owner's attention, neither resolved here:

1. **T0045.6 vs T0050 — parallel culling lands before the threading rules that
   make it safe.** T0045 (Order 440) subtask 45.6 parallelises culling via the
   job system; T0050 (Order 520) is where the thread-ownership rules and debug
   asserts get written — and T0050.4 *also* claims "parallel frustum culling
   (T0045)" as its own work. The same work is claimed twice, and the Order
   does it on the risky side: parallelism before the ownership rules. Either
   45.6 defers to T0050, or 50.1/50.2 (rules + asserts) are pulled ahead of
   T0045. Flagged, not renumbered — the split is a judgement call.
2. **T0029 (560) vs Phase 4's Tracy-dependent Done-whens** — T0045 (440)
   "visible in Tracy", T0050 (520) "named and visible in Tracy", T0086 (480)
   "visible in Tracy (T0030)". T0029 already flags this itself ("pulled to the
   start of Phase 4 — owner's call") and it remains unresolved; restated here
   so it is visible from the sweep, not only from inside T0029.

Soft phase-spans judged fine (the README explicitly permits acceptance spanning
phases): T0048(150)'s "open scene survives reload" cannot close before
T0021(200); T0068(170)'s "bindings are serialized (T0020)" waits for 190;
T0116(655)'s collision-authoring item is conditional on T0051(800); T0113(382)'s
crash-routing closes with T0099(790). In each case the bulk of the ticket sits
where its Order puts it.

### Considered and deliberately not asserted

- **T0038 name-stability for sub-asset identity.** T0023's sub-asset design
  says GUIDs are "ideally keyed by name" — *not yet decided*. If it lands on
  names, T0038 inherits keep-node/mesh-names-stable-through-conversion. Left
  for T0023 to record when the design is actually chosen; asserting an
  obligation from an undecided design is inventing.
- **T0070's constraints on T0021/T0051/T0057/T0020.** Real only if multiplayer
  is ever wanted, and that decision is explicitly open and the owner's. Added
  the question to T0044's owner list instead of back-referencing constraints
  that may never exist.
- **T0031 ← T0120** (RTT camera cost in frame budgets): T0120 already says
  "cross-reference, do not duplicate"; a budget line for an unbuilt mechanism
  felt speculative. Skipped.
- **T0035 ← T0115.5** (hierarchy search): T0115 extends the panel later; no
  design obligation on T0035 today. Skipped.
- **T0107 → T0079** (effect lights cheap/unshadowed): thin — 107.6 owns the
  choice and per-object selection already handles transient lights. Skipped.

### The three files another session owns

T0122, T0123 and T0125 were read but not touched, per instruction. The sweep
found **no back-reference any of them needs**: T0122↔T0123 is already two-way
(`Blocks`/`Blocked by`), T0125's `Found by`/Refs are two-way, and none of the
three constrains any open ticket — D18, which T0122 produced, binds developer
workflow rather than ticket work and is already linked from T0102, T0122 and
`CLAUDE.md`.

### Verification

```
$ python3 tools/check_backlog.py
backlog consistent: 125 tickets, folder/Status/board agree, links resolve
```

Run after every batch of edits, including the new decision-log and
frame-anatomy relative links added to Refs rows. The scripted pass asserted an
exact unique match for every replacement (`assert old in t and t.count(old)
== 1`) and refused to append a duplicate obligations section, per the
assert-on-every-scripted-edit rule.

**Left in `inprogress/`**: Done-when 4 (a human reads a spot-check sample and
confirms the one-liners are useful) cannot be self-satisfied. Everything else
is done; move to `completed/` once that read has happened.
