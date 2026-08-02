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
