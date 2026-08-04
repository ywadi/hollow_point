# T0126 — Remove the single-game framing from the backlog

| | |
|---|---|
| **Status** | ✅ DONE |
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

- [x] No open ticket defers a decision to T0044 or to "the game" — verified by grep, see below
- [x] Each parked question is restated as an **engine capability** decision owned
      by the ticket that would build it: does the engine support this, and when
      is it scheduled — decided on engineering grounds, not on a hypothetical
      game's requirements
- [x] `documentation/07-design-gaps.md` no longer routes questions through a
      game definition
- [x] `documentation/01-project-overview.md` states plainly what the product is —
      a general-purpose 3D engine for a studio's several games — and its other
      staleness is fixed. It was worse than the three items listed here; see
      below
- [x] The backlog README's "That decision (T0044) only gates Phase 7" is removed
      — **already done** by the commit that dropped T0044; verified, not redone
- [x] CLAUDE.md's research-agent guidance no longer cites T0044 as the example of
      an owner-only decision — **already done** by the same commit. Its live
      examples are now mods, platforms, and what the studio will maintain, and
      it names T0044 only to say the question was dropped. Verified, not redone
- [x] `python3 tools/check_backlog.py` passes

## Subtasks

- [x] 126.1 Sweep the eleven-plus tickets listed above; reword each deferral
- [x] 126.2 `07-design-gaps.md` — rewrite the T0044-routed items
- [x] 126.3 `01-project-overview.md` — say what the product is; fix C++17, the
      root path, and the "apps/ is empty" claim
- [x] 126.4 README and CLAUDE.md references — both already correct; verified
- [x] 126.5 Re-read the result for tickets that *scope themselves down* on
      game-specific grounds even without naming T0044 — it had spread further,
      and three more sites were found this way

## Notes / findings

**T0093 is already done (2026-08-04), and is the worked example for the rest.**
It was "Visibility, vision cones and fog of war", High and Very Complex — a
ticket to *build* a stealth mechanic in the engine. It is now a **capability
validation**: prove a vision mechanic can be built in `samples/sandbox/` with
zero engine changes, and file any gap against the ticket that owns the missing
capability (T0060, T0079, T0086, T0094...). Nothing about the mechanic ships in
the engine.

That is the shape to aim for everywhere else in this sweep. Note what it did
*not* do: it did not delete the technical content. The architectural constraints
T0093 imposes on T0060/T0079/T0086 are the reason it is worth keeping, and they
are binding on tickets built earlier than it. **Reframing ownership is not the
same as discarding requirements**, and the second is easy to do by accident
while doing the first.

**Still open from it: T0098's dependency on T0093.** Navigation refs it as
evidence the engine needs pathfinding ("vision cones and alert states exist").
That is T0044-shaped reasoning — inferring engine scope from a hypothesised
game — and 126.1 should correct it. Navigation is an engine capability that
stands or falls on its own merits.

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

## Done (2026-08-04)

### What was swept

**Eleven tickets**, each deferral reworded into an engine-capability decision
with a named owner — not deleted:

| Ticket | Was deferred to a game | Now owned by, on engineering grounds |
|---|---|---|
| T0031 | "T0044's content-scale answer" | a resident working set; streaming is T0023/T0058 and unowned |
| T0038 | "if the game needs facial animation" | T0038 + T0049, on three-place retrofit cost |
| T0051 | "the game decision is T0044's" | T0051: does the engine offer ragdoll, and which of the three |
| T0069 | HUD inherits "the policy T0044 decides" | T0081 owns the policy; the safe-area inset holds under all three |
| T0075 | "if the game ever ships on a store platform" | nobody — the rule preserves the option for free |
| T0081 | aspect ratio: "T0044 decides" | 81.10 decides *and* implements; likely offers the policy rather than picking one |
| T0081 | "whether this game has scripted scenes" | a sequencer needs its own ticket; 81.9 is the seam |
| T0087 | "if the game turns out to be all-outdoor" | T0087 owns both halves; the question is which is built first |
| T0098 | "confirm with T0044 that the game has NPCs" | T0098, on recast's cost vs what else assumes a navmesh |
| T0103 | "whether the game ships on Steam" | nobody — the write-directory constraint is free either way |
| T0112 | "product-adjacent, like T0044" | T0112, on migration asymmetry: literals migrate, keys cost one table |
| T0117 | "informed by T0044/T0112" | T0112 alone; coverage is an atlas-size decision |

### 126.5 found three more that the string search would have missed

The ticket predicted this, and it was right:

- **T0069** — "Deciding needs to know how UI-heavy the game is." The same error
  without the citation. It now turns on where the ceiling sits: whether the
  engine ships a retained UI system, or exposes T0027's UI layer and T0068's
  input contexts and lets a game build its own. That is decidable today.
- **T0106** — "The game is 3D (D15)." The *engine* is 3D.
- **07-design-gaps.md §10** — "Whether the game ships on Steam at all is
  unasked." Not this repository's question to answer.

Also found and removed: `claude_documentation/board/server.js` carried a **"Game
definition"** group in the Gameplay layer, containing only T0044 (dropped) and
T0006 (superseded). The framing was in the board UI, not only in prose.

### 01-project-overview.md was worse than the three items listed

Beyond C++17, the absolute root path and "apps/ is empty", it also claimed
**"There is no application and therefore no executable"**, and that `dist` would
"stage libraries only until T0006 lands" — T0006 is superseded, and the tree has
had `hp_editor`, `hp_runtime` and `libhp_sandbox` for some time. It described
the engine as "engine static libraries" where D12 makes it shared, and its tree
listing had no `engine/`, `samples/`, `docs/` or `claude_documentation/`.
Rewritten against the actual tree rather than patched.

### Deliberately not changed

**`02-decision-log.md` D15 says "The game is 3D".** Same framing, in the one
document that is binding, and the decision it supports is unaffected by the
wording. Changing the decision log is the owner's call rather than a sweep's, so
it is flagged here instead of done quietly. It is the last known site.

**T0093's three T0044 mentions stay.** All three explain *why* T0044 was wrong.
That is the worked example this ticket was told to follow, not a deferral.

**T0039's "gates Phase 7 only, as originally intended"** stays: a scheduling
statement about T0039's own mesh-container decision, not a deferral to a game.

**07-design-gaps.md's "Read in full" ticket list** still names T0044. It records
which tickets a survey read on a given day; editing it would falsify the record.

### Verified

```
$ grep -rn "T0044" claude_documentation/backlog/open/ claude_documentation/backlog/inprogress/
0093: three mentions, all explaining why T0044 was wrong        (intended)

$ grep -rniE "our game|this game|whether the game|if the game" backlog/open/ backlog/inprogress/
0073:88  "state machine from this gameplay one"                 (false positive)

$ python3 tools/check_backlog.py
backlog consistent: 128 tickets, folder/Status/board agree, links resolve
```
