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
| Phase (default) | Grouped under collapsible phase headings, ascending |
| Priority | Flat, High → Medium → Low, phase as tiebreak |
| ID | Flat, ticket order |

Phase grouping only applies while sorting by phase — the other modes deliberately
cut across phases, so they render flat rather than showing groups that no longer
reflect the ordering.

The sort choice persists in `localStorage`, as does per-phase collapse state.
With 39 open tickets in one column, Collapse all is the fastest way to see the
phase structure at a glance.

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

## GitHub status badges

The header also carries two GitHub Actions badges, next to Sort and search,
plus a plain link to the repository. Each badge is a link straight to that
workflow's run history (`/actions/workflows/<file>`); the repo link goes to
`github.com/ywadi/hollow_point` itself. All three open in a new tab
(`target="_blank" rel="noopener"`) — this is a working dashboard, and a click
that navigates it away from where you left it is just annoying.

Both workflows get a badge, not just one, because both matter for different
reasons: `ci.yml` ("CI") is the fast suite that runs on every push and is the
thing that should basically always be green, while `full-build.yml` ("Full
build") is the ~1100-target nightly build that catches breakage nothing else
compiles against. A single badge would hide whichever workflow it left out —
if only CI were shown, a broken nightly build could go unnoticed for a long
time, and that is exactly the kind of drift this board exists to surface.

The badge `<img>` is the one thing in this file that depends on the network —
everything else is served from disk or computed client-side. That matters
here specifically because offline configure is a verified property of this
project (T0010), so a header that shows a broken-image icon reads as a bug in
the board, not as "you're offline." But a failed image load isn't only an
offline signal: the same failure happens if github.com is unreachable for any
other reason, or — not a network problem at all — if the repository is private,
because GitHub serves `badge.svg` anonymously only for public repositories and
an `<img>` request is always anonymous (it is a cross-site subresource, and
GitHub's session cookie is `SameSite=Lax`, so even a logged-in viewer sends
none). This was not hypothetical: the badges were written while the repo was
still private and rendered as the fallback for exactly that reason. The repo is
public now and both badges serve `image/svg+xml` and read `passing`. The
fallback therefore doesn't claim a specific cause — it survives the repo going
private again, which is the point. Two things handle it:

- `.gh-badge-img` is a fixed-size box, not just an `<img>` with intrinsic
  dimensions. The box is reserved before the image ever starts loading, so
  there is no layout shift between "loading," "loaded" and "failed to load."
- an `error` listener on each `<img>` (in the script, not an inline
  `onerror`, to match how every other handler in this file is wired up)
  swaps the image for a small "status unavailable" text fallback inside that
  same box, instead of letting the browser render its own broken-image icon.

The badges are unauthenticated `badge.svg` requests, so they also render
correctly for anyone with a network path to github.com and read access to the
workflow — the fallback exists for "the request didn't succeed," not for a
specific reason why.

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
