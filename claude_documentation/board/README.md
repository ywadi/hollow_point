# Backlog board

A Kanban view of `claude_documentation/backlog/`, for watching progress without
reading files.

```sh
node claude_documentation/board/server.js        # http://localhost:8071
node claude_documentation/board/server.js 9000   # or pick a port
```

Node stdlib only — nothing to install.

## How it works

**The folders are the source of truth.** A task's column is the directory it
sits in, not a field inside the file:

| Directory | Column |
|---|---|
| `backlog/open/` | Open |
| `backlog/inprogress/` | In Progress |
| `backlog/completed/` | Completed |

Move the file, and the card moves. The board therefore cannot drift out of sync
with the repository, and progress is tracked by `git mv` rather than by editing
two places and hoping they agree.

`server.js` reads the header table of each task for the title, priority and
status, and counts `- [ ]` / `- [x]` items for the progress bar. `index.html`
polls `/api/tasks` every 10 seconds and renders the card's full markdown when you
click it.

The poll only repaints when something actually changed — it compares task IDs and
file mtimes — so an open detail panel does not flicker or lose its scroll
position every ten seconds.

Phase groups (in a column like Open, where tasks carry a `Phase`) are
collapsible — click the header, or the chevron rotates to show state. Each
phase header also shows an aggregate progress bar: total subtask checks done
over total checks across every ticket in that phase (`3/27 checks`), computed
client-side from the same data the cards use. The bar stays visible when the
group is collapsed, so you can watch phase progress without expanding it.
Collapse state is saved in `localStorage` per phase (keyed by column + phase
number) and survives both page reloads and the 10-second poll's repaints.

A red dot in the header means the server is unreachable; the page keeps retrying.

## The current ticket sequence, at the top

The board answers *what is there*. It cannot answer *what is happening now*,
because a sequence is an ordering **across** tickets and the folders have no way
to express one. That lives in the `## Current ticket sequence` section at the top
of [`../backlog/README.md`](../backlog/README.md), and the panel above the
columns is that section rendered.

**It is parsed out of the README, never restated here**, and that is the whole
design rather than an implementation detail. A copy in `server.js` or
`index.html` would be a second source that drifts the first time somebody edits
the document and not the board — and the section's own rule is that a stale
sequence is worse than none, *because it is trusted*. A rendered page is the
most trusted copy there is, so it is the worst possible place to keep a copy.
The panel header prints the source path for the same reason: it tells you where
to go and edit, instead of leaving you to guess that a wrong row is a board bug.

The parser bends to the prose, not the other way round. It needs only the shape
that section already had — a `**Set <date>` line, and a numbered table whose
second cell links the ticket. `/api/sequence` returns the date, and per row the
position, ticket id, kind, link and the "why" text.

**The `decision` row is styled to be impossible to skim past.** One item in the
sequence is a question for the owner rather than work to pick up, and an agent
that reads it as a queue item and implements a branch does damage that is
expensive to undo. So it gets its own colour — used nowhere else on this board,
because amber already means "in progress" and red already means "blocked" — a
solid `DECISION` badge, and a line of text saying so in words. Colour alone was
not judged enough.

**A parse failure is loud, never blank.** `/api/sequence` answers `200` with the
failure in the payload (the same way `/api/ci` reports a GitHub outage), and the
panel renders the error where the rows would be. An empty panel would read as
"nothing planned", which is the one meaning this must never accidentally have.
Two kinds are distinguished: a *hard* failure — no section, or a section listing
no tickets — replaces the rows, while *staleness* — no `Set` date, a row whose
ticket file is missing, a row pointing into `completed/` — shows a banner above
rows that still render. The problems are also printed by `server.js` at startup,
since the person launching it is the one who can fix them.

`tools/check_backlog.py` enforces the same rules as a gate and stays
authoritative; this is the visible half, for whoever is looking at the board
rather than running the checker.

## Moving a task

```sh
git mv claude_documentation/backlog/open/0005-*.md \
       claude_documentation/backlog/inprogress/
```

Then update **Status** inside the file to match. The column comes from the
folder, but the status line is what a reader sees first, and a task whose two
disagree is worse than one with neither.

`inprogress/.gitkeep` keeps that directory in git while it is empty. Leave it
there.

## Sorting and bulk collapse

The header carries a **Sort** control and **Collapse all** / **Expand all**.

| Sort | Behaviour |
|---|---|
| Phase (default) | Grouped under collapsible phase headings; cards within a group follow `Order`, so the top card is still what to work next |
| Execution order | Flat, by each ticket's `Order` field — the same queue without the phase scaffolding |
| Priority | Flat, High → Medium → Low, phase then order as tiebreaks |
| ID | Flat, ticket order |

The `Order` field is an `| **Order** | N |` row in each open ticket's header
table — the execution order from `backlog/README.md`, kept in the tickets so
that table and this board read one field and cannot disagree. `server.js` does
not parse it; the client derives it from the full markdown the server already
sends. Tickets without one (completed, in progress) sort after those with, and
each card shows its number on the id line. Execution order respects phases, so
the Phase view and the default view agree on what sits on top — they differ
only in whether the phase headings (and their aggregate bars) show.

Phase grouping only applies while sorting by phase — the other modes render
flat: Priority and ID deliberately cut across phases, and Execution order is
the "what's next" list, where headings would just be in the way.

Phase is the default rather than Execution order, and the reason is worth
recording because the other way was tried first. Both put the *same* ticket on
top, since phase groups order their cards by the same `Order` field — so the
"what next" question is answered either way, and the difference is everything
else on screen. The phase view keeps the headings and their aggregate progress
bars, which is how ninety-one open tickets stay navigable; Execution order
renders flat, and a flat list of ninety-one is a worse first impression than a
grouped one that starts in the same place. The `localStorage` sort key was
bumped to `v2` when this was corrected, so anyone who loaded the board while
the flat view was the default is not left permanently stuck in it.

The sort choice persists in `localStorage`, as does per-phase collapse state.
With 91 open tickets in one column, Collapse all (in the Phase view) is the
fastest way to see the phase structure at a glance.

## Search

The header carries a search box next to Sort. It filters live as you type, no
submit button, and matches ticket id, title, phase name, priority and
complexity, case-insensitively, against all three columns at once. The id
match is a little smarter than plain substring: "93", "0093" and "T0093" all
find T0093, since the query is also checked against the ticket's numeric id.
Everything else is deliberately dumb substring matching rather than fuzzy
search — with about a hundred tickets, substring is fast enough and its
results are predictable, which matters more than being clever.

A card that doesn't match is hidden rather than removed from the DOM, so a
phase's aggregate progress bar keeps showing the phase's real totals — search
narrows what you see, it does not change what's true. A phase group with no
matches is hidden entirely; one with a match is forced open even if you had
it collapsed, but that's a display-only override — it never writes to the
collapse state in `localStorage`, so clearing the search (the ✕ in the box,
or Escape) puts every group back exactly where you left it. Each column shows
its own "no tickets match" message when a search comes up empty there.

The search term is not persisted. Every reload starts with an empty box,
because a stale filter left over from last time you looked would just be
confusing — unlike sort and collapse state, which are meant to stick.

## GitHub CI status

The header carries the state of two GitHub Actions workflows, next to Sort and
search, plus a plain link to the repository. Each one links to the run it is
describing; the repo link goes to `github.com/ywadi/hollow_point` itself. All
three open in a new tab (`target="_blank" rel="noopener"`) — this is a working
dashboard, and a click that navigates it away from where you left it is just
annoying.

Both workflows are shown, not just one, because both matter for different
reasons: `ci.yml` ("CI") is the fast suite that runs on every push and is the
thing that should basically always be green, while `full-build.yml` ("Full
build") is the ~1100-target nightly build that catches breakage nothing else
compiles against. Showing one would hide whichever it left out — if only CI
were shown, a broken nightly build could go unnoticed for a long time, and that
is exactly the kind of drift this board exists to surface.

### Why this is not GitHub's badge.svg

It was, and the badge is wrong for this repository in a way that matters.

`ci.yml` sets `cancel-in-progress: true`, so pushing again while a run is in
flight cancels the previous one. That is routine here — the intended
behaviour, not an incident. GitHub's badge renders the newest **completed** run
on the default branch and paints anything that is not `success` red, so a
cancelled run makes the badge say `CI - failing` at the exact moment nothing
whatsoever is known about the code. Measured on 2026-08-04: run 30943449243
was cancelled by the next push, and `badge.svg` served
`<title>CI - failing</title>` with the red gradient while run 30941522246, the
last one that actually finished on its merits, had succeeded. `CLAUDE.md`
already warns that **`cancelled` is not `failed`**, because reading it that way
has produced a confident and wrong conclusion here before — and the board was
repeating the mistake in its own header.

The badge has no in-flight state either: while a run executes it keeps showing
the previous verdict, so "building" and "finished" look identical.

Neither is fixable in an `<img>`, so the status is computed in `server.js` from
the Actions API, where the two things the badge conflates are separate fields:
`status` is `queued | in_progress | completed`, and `conclusion` is `null`
until a run completes and only then says `success | failure | cancelled |
skipped | timed_out | …`. Treating "`conclusion !== 'success'`" as failure
buckets both a cancelled run and a running one into red, which is the bug.

### What it shows

| State | Dot | Reads |
|---|---|---|
| `success` | solid green | `passing` |
| `failure`, `timed_out`, `startup_failure` | solid red | `failing` |
| `in_progress` | pulsing amber | `building` |
| `queued` | hollow amber | `queued` |
| `cancelled` | grey ring | `cancelled` |
| `action_required` | solid red | `action required` |
| no runs / API unreachable | grey | `no runs` / `unavailable` |

Amber is the same amber the In Progress column uses, so the header and the
board mean the same thing by the same colour. Cancelled, skipped and
unavailable stay **grey**: each is the *absence* of a verdict, not a bad one.

### Which run the pass/fail comes from

The state is **not** simply the newest run. Three things are read separately:

- **In flight** — the newest run that has not completed. If there is one, that
  is the state, because it is what is happening now.
- **Verdict** — the newest completed run whose conclusion is a statement about
  the code (`success`, `failure`, `timed_out`, `startup_failure`,
  `action_required`). Cancelled and skipped runs are **skipped over**: a run
  killed before it finished says nothing, and must not overwrite one that does.
- **Newest** — used only when there has never been a verdict at all, at which
  point `cancelled` genuinely is the whole story and is shown as such.

Whenever the verdict is not from the newest run, the badge says so in small
grey text rather than presenting a stale green as if it covered the latest
push:

- `building · last passing` — a run is going; the green is from the one before it
- `passing · latest cancelled` — the newest run was cancelled, so this verdict
  is about an earlier commit

The note slot is always reserved, empty or not, so the header does not gain a
whole row the moment a build starts. Hovering gives the full picture: run
numbers, commit subjects, shas and ages for the in-flight, verdict and newest
runs.

### Rate limits, caching and offline

`/api/ci` is the only endpoint that leaves the machine, so it is the only one
that can fail, and a failure must not read as a CI failure — it reads
`unavailable`, grey. This matters here specifically because offline configure
is a verified property of this project (T0010): a board that goes red when
you unplug the network would be lying.

- The response is cached server-side and the client polls on the TTL the
  server advertises. The TTL shortens to 20s while a run is in flight (and
  only when authenticated), because that is the one state that changes on its
  own.
- Requests are **conditional** — the ETag of the last response is replayed as
  `If-None-Match`. A `304` does not count against GitHub's rate limit, so a
  board left open on a quiet afternoon costs nothing. Measured: twelve
  uncached workflow requests spent two units of rate limit; the other ten were
  `304`s.
- A token is optional. The repo is public and this works anonymously, but
  anonymous is 60 requests/hour per IP, so the uncached TTL doubles without
  one. `GH_TOKEN`/`GITHUB_TOKEN` are read first, then `gh auth token`, then
  `gh auth status --show-token` for gh older than 2.9 — which is what is
  installed here, and it prints the token on **stderr**, so both streams are
  searched. The startup banner says which one it got.
- One workflow failing does not blank the other, and a GitHub outage returns a
  200 with the failure in the payload rather than a 500 that would take the
  whole board down.

`HP_BOARD_REPO`, `HP_BOARD_BRANCH`, `HP_BOARD_CI_TTL_MS` and `HP_BOARD_GH_BIN`
override the defaults. The branch is scoped to `main` for the same reason
GitHub's badge is: the header answers "is the project green", and a run on a
scratch branch is not that.

## Superseded and dropped tickets

A ticket closed as `❌ SUPERSEDED` or `❌ DROPPED` was closed **without the work
being done**, so its checkboxes are legitimately unticked. The board therefore:

- tags the card *Superseded* / *Dropped* and dims it
- replaces its progress bar with "closed without completing"
- **excludes it from the phase aggregate**, so a phase can still reach 100%

Ticking those boxes to make a bar look tidy would misrepresent what happened —
the ticket records why it was closed instead. See
`completed/0006-define-real-application.md` for an example.

## Architecture map

`/architecture` renders the same tickets grouped by **where they sit in the
stack** rather than by when they are scheduled — a phase is *when*, a layer is
*where*. Eleven layers from Foundation up to Shipping, each split into
subsystems, with per-subsystem progress.

Clicking a ticket shows what it **depends on** and what **depends on it**.

Those dependencies are not hand-maintained: any `T00NN` mention in a ticket body
is treated as a reference, so the graph is derived from the backlog and cannot
drift from it. Currently 286 edges across 94 tickets.

The layer/subsystem mapping lives in `ARCHITECTURE` at the top of `server.js`.
Every ticket must appear in exactly one group — any that do not are listed as a
warning banner on the page rather than silently omitted, so adding a ticket
without mapping it is visible immediately.
