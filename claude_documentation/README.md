# claude_documentation

Working memory for this project. The point is that a fresh session — or a fresh
person — can read these files and pick the work up without re-deriving anything.

Read them in order:

| File | What it holds |
|---|---|
| [01-project-overview.md](01-project-overview.md) | What HollowPoint is, what's in the tree, what exists and what doesn't |
| [02-decision-log.md](02-decision-log.md) | Decisions made, the options rejected, and **why** — the reasons matter more than the choices |
| [03-build-harness.md](03-build-harness.md) | How the build harness works internally |
| [04-cross-compile-gotchas.md](04-cross-compile-gotchas.md) | Every defect hit getting this to cross-compile, with root cause and fix |
| [05-verification-status.md](05-verification-status.md) | What is proven to work, what merely appears to, with the evidence |
| [backlog/](backlog/README.md) | Open work — one file per task, each with its own subtasks |

`BUILDING.md` in the repo root is different — that's user-facing "how do I build
this" documentation. These files are the *why* and the *state*.

## Maintaining this

- **Record the reasoning, not just the outcome.** "Pinned CMake 3.31.12" is
  nearly useless a month from now; "CMake 4 rejects `cmake_minimum_required(VERSION <3.5)`
  and Diligent's vendored libraries still declare 2.8" prevents someone
  helpfully upgrading it and breaking the build.
- **Write down what was verified and how**, separately from what is merely
  believed to work. A claim with no evidence behind it is a liability — see the
  status markers in `05-task-log.md`.
- **Record corrections.** When something recorded here turns out to be wrong,
  fix it *and* note that it was wrong, so the same wrong conclusion is not
  reached twice.
- Update `05-verification-status.md` when something becomes proven — or turns out
  not to be. Update the relevant `backlog/` file as work progresses, appending
  findings rather than overwriting them.
- **Keep the two apart.** `05-verification-status.md` is *what is true*;
  `backlog/` is *what to do*. An open task does not mean something is broken, and
  a closed task does not mean something is verified.

## Conventions used in these files

| Marker | Meaning |
|---|---|
| ✅ VERIFIED | Observed working, with the command and output that showed it |
| ⚠️ UNVERIFIED | Believed to work, never actually exercised |
| ❌ BROKEN | Known broken |
| 🔜 TODO | Not started |
