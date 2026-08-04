# T0126 — Remove the single-game framing from the backlog

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 1 — Harden the build |
| **Order** | 5 |
| **Created** | 2026-08-04 |
| **Found by** | T0044 (dropped) |
| **Refs** | T0044, T0109, T0112, [../../../CLAUDE.md](../../../CLAUDE.md), [../../documentation/07-design-gaps.md](../../documentation/07-design-gaps.md) |

## Why

T0044 "Define the game" was dropped because its premise was wrong: this is a
general-purpose engine for a studio that will ship several games, not an engine
for one game with a reusable editor. Dropping the ticket does not undo its
effect. **The framing it introduced is still spread across the backlog**, and it
is the framing that does the damage, not the file.

Roughly fourteen open tickets defer engineering decisions to a game that does
not exist:

```
0098  "Confirm with T0044 that the game has navigating NPCs at all"
0081  "Implement whichever aspect-ratio policy T0044 decides"
0069  "HUD anchoring inherits the aspect-ratio policy T0044 decides"
0103  "whether the game ships on Steam is undecided (now a T0044 question)"
0075  "If the game ever ships on a store platform (undecided -- now on T0044's ...)"
0087  "the indoor/outdoor question is now on T0044's list"
0051  "The game decision is T0044's; the mechanism, if ..."
0038  "now on T0044's list. If the answer is no, one rejection line there closes"
0031  "sized against T0044's content-scale answer"
0117  "informed by whatever T0044/T0112 have settled on"
0112  "is a product-adjacent decision, like T0044"
```

plus `documentation/07-design-gaps.md`, which has an entire column of
T0044-shaped questions, and the backlog README's "That decision (T0044) only
gates Phase 7."

Each of these reads as: *find out what our game is, then decide whether the
engine needs this.* For an engine meant to power several games that is exactly
backwards, and it invites the specific failure of dropping a subsystem because
"our game" does not need it — shipping a limitation on purpose, discovered by
the second game.

## Done when

- [ ] No open ticket defers a decision to T0044 or to "the game"
- [ ] Each parked question is restated as an **engine capability** decision owned
      by the ticket that would build it: does the engine support this, and when
      is it scheduled — decided on engineering grounds, not on a hypothetical
      game's requirements
- [ ] `documentation/07-design-gaps.md` no longer routes questions through a
      game definition
- [ ] `documentation/01-project-overview.md` states plainly what the product is —
      a general-purpose 3D engine for a studio's several games — and its other
      staleness is fixed (it currently says **C++17**, gives an absolute root
      path that is no longer correct, and claims `apps/` is empty although T0013
      shipped both the editor and the runtime)
- [ ] The backlog README's "That decision (T0044) only gates Phase 7" is removed
- [ ] CLAUDE.md's research-agent guidance no longer cites T0044 as the example of
      an owner-only decision — the example should be one that is actually still
      live
- [ ] `python3 tools/check_backlog.py` passes

## Subtasks

- [ ] 126.1 Sweep the eleven-plus tickets listed above; reword each deferral
- [ ] 126.2 `07-design-gaps.md` — rewrite the T0044-routed items
- [ ] 126.3 `01-project-overview.md` — say what the product is; fix C++17, the
      root path, and the "apps/ is empty" claim
- [ ] 126.4 README and CLAUDE.md references
- [ ] 126.5 Re-read the result for tickets that *scope themselves down* on
      game-specific grounds even without naming T0044 — the framing may have
      spread further than the string search shows

## Notes / findings

**T0093 is the one to look at hardest.** "Visibility, vision cones and fog of
war" is High priority and Very Complex, and T0044's notes leaned on it as
evidence about the game ("the vision-cone requirement in T0093 implies moving
NPCs"). That is genre-specific reasoning sitting in an engine backlog, one
ticket over from the one just dropped. Whether it belongs here at all is a real
question: a general-purpose engine can offer visibility queries without
committing to stealth-game fog of war. **Do not answer it in this ticket** —
raise it, and let it be decided deliberately rather than swept.

**Rewording is not the same as deleting the question.** Several of these are
genuine engine decisions with real consequences — streaming (a Phase-4-shaped
retrofit if added late), keys-vs-literals (T0112.1, a migration if decided
late), determinism (T0070's constraints on T0021/T0051/T0057). The work is to
move each onto engineering ground and give it an owner, **not** to strip the
question out because the ticket that indexed it is gone.

**Watch for the obvious wrong repair.** Replacing "what is our game" with "what
genre family does the studio make" reproduces the same error one level up, and
was rejected when T0044 was dropped. An engine capability is worth building or
it is not; that is answered by cost, by schedule, and by whether it is
foundational — not by a genre guess.
