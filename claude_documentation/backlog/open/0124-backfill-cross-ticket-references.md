# T0124 — Backfill cross-ticket references across the whole backlog

| | |
|---|---|
| **Status** | 🔜 TODO |
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

- [ ] Every ticket that another ticket constrains carries the reference in its
      own `Refs`, with a one-line statement of what it must honour — not a bare
      ticket id
- [ ] Decision-log entries that bind a ticket are linked from that ticket,
      D17 and D18 included
- [ ] `python3 tools/check_backlog.py` passes, so every added link resolves
- [ ] A spot-check sample is read by a human and confirms the one-liners say
      something useful, rather than restating the title of the ticket they
      point at

## Subtasks

- [ ] 124.1 Sweep all tickets and build the dependency graph that is currently
      implicit in prose — "X must happen before Y", "Y assumes X's format",
      "this is decided in D_n"
- [ ] 124.2 Add the missing back-references, each with its one-line obligation
- [ ] 124.3 Reconcile against the `Blocks` / `Blocked by` fields already in use
      (T0095→T0105, T0053→T0022/T0035/T0062, T0110→T0025, T0111→T0046) so the
      two mechanisms agree rather than duplicating
- [ ] 124.4 Flag, without inventing answers, any dependency found that
      contradicts the current `Order` — a ticket that must come first but is
      sequenced later is a real problem, and this sweep is the cheapest time to
      notice it
- [ ] 124.5 Run `check_backlog.py`; fix what it catches

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
