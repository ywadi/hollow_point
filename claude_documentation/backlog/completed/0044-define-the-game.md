# T0044 — Define the game

| | |
|---|---|
| **Status** | ❌ DROPPED |
| **Priority** | — |
| **Complexity** | — |
| **Phase** | 2 — Engine skeleton |
| **Created** | 2026-08-02 |
| **Dropped** | 2026-08-04 |
| **Supersedes** | part of T0006 |
| **Refs** | T0109, T0006, [../../../CLAUDE.md](../../../CLAUDE.md) |

## Dropped — the premise was wrong

This ticket opened with:

> The product is a game, with the editor built well enough to reuse.

**That is not what this project is.** HollowPoint is a game engine for a studio
that will ship several games. A game is a separate project with its own assets,
scenes and gameplay module, built against an installed engine (T0109);
`samples/sandbox/` exists only so the module boundary is exercised by this
repository's own CI, and CLAUDE.md says in as many words that it **is not "the
game"**.

So the ticket asked the wrong question, and asking it caused real harm to how
the engine was being planned: roughly fourteen tickets ended up deferring
engineering decisions to "whatever game we turn out to be building", and the
design-gap survey grew a column of questions shaped the same way. An engine
scoped to one game — or to a genre family, which was the obvious repair and is
no better — is an engine that cannot serve the second game the studio makes.
The framing invites exactly the wrong instinct: *drop what our game does not
need.* For a studio engine that is not a saving, it is a limitation shipped on
purpose.

### Why it existed

Not an oversight, a leftover. It was created **2026-08-02** in the initial
planning pass (`dbf21a7`, "Plan the engine: 35 tickets across 8 phases"), when
the working assumption really was "build a game, keep the editor". The
multi-game studio framing arrived a day or two later with T0109 (2026-08-03) and
CLAUDE.md (2026-08-04, `566f4b0`) — and nothing swept back over the tickets
written under the old assumption. This one is that sweep arriving late.

## What replaces it

**Nothing, as a single ticket.** There is no "define the game" decision to make,
and creating "define the genre family" instead would reproduce the error one
level up.

The questions this ticket collected were real, but they were never game
questions — they are **engine capability** questions, and each belongs to the
ticket that would build the thing:

| Question this ticket parked | Where it belongs |
|---|---|
| Skeletal animation needed? | T0041/T0049 — an engine for several games has it |
| LOD? | T0039/T0040 |
| Navigation / pathfinding? | T0098 |
| Terrain, water, vegetation | no tickets exist; that is a real gap, not a game question |
| Indoor/outdoor lighting mix | T0087.8 |
| Cutscene / sequencer subsystem | T0081 keeps the seam open; build when a game needs it |
| Ragdoll, morph targets | T0051, T0038 |
| Aspect-ratio policy | T0081/T0069 — an engine offers a policy, it does not pick one |
| Streaming | T0023/T0103 |
| Strings as keys or literals | **T0112.1 — and this one has a deadline**, see below |
| Multiplayer plausible? | T0070 |
| Store/platform integration | T0103, T0075 |

The right form of each is "does the engine support this, and when do we build
it" — answered on its own ticket, on engineering grounds, at the point the work
is scheduled. Not "does our game need it", answered once, up front, for a game
that does not exist.

**One of these does not wait: T0112.1**, keys vs. literals for user-facing
strings. It is needed at the head of Phase 3 because every literal authored
before the decision is a migration afterwards. It is an engine decision and
always was — T0112 owns it outright now, with no dependency on this ticket.

## Consequences, recorded deliberately

- **Dropped, not deleted.** The file stays so the reasoning survives and so the
  question cannot quietly return. A backlog that deletes its mistakes teaches
  nobody; this repository's history is a design record (CLAUDE.md), and "we
  planned this as a single game for two days" is exactly the kind of thing worth
  being able to find later.
- **The inbound references are a follow-up, not this ticket's work.** Around
  fourteen open tickets and `documentation/07-design-gaps.md` still say things
  like "T0044 decides whether the game has navigating NPCs at all". Each needs
  rewording to an engine-capability decision owned by the ticket itself. Filed
  as **T0126**, because leaving them is leaving the framing.
- **`documentation/01-project-overview.md` never stated what the product is**,
  and is stale in other ways besides (it says C++17, gives an absolute root path
  that has not been correct for some time, and claims `apps/` is empty although
  T0013 shipped both apps). T0126 covers it: the project overview should say
  plainly that this is a general-purpose engine for a studio's several games.
- **No phase plan changes.** The board's claim that this "only gates Phase 7"
  is void — nothing was gated on it, which is part of why it survived two days
  of planning without anyone noticing the premise had expired.
