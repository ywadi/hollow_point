#!/usr/bin/env python3
"""Backlog consistency check.

Three things can disagree and nothing was checking that they don't:

  the folder a ticket sits in   -- the source of truth (`backlog/README.md`)
  the Status field inside it    -- what a reader sees first
  the board table row           -- what the board renders

This exists because all three drifted in one session: T0118 had its Status set
to IN PROGRESS and its board row updated while the file stayed in `open/`. The
board rule is that state *is* the folder, so a ticket claiming otherwise is the
one kind of inconsistency the whole scheme is supposed to make impossible.

Also checks every relative markdown link resolves -- that has caught a stale
link on four separate ticket moves, always after the fact.

And it refuses an unticked `- [ ]` in a ticket marked DONE. Fifteen completed
tickets carried one, which makes a green DONE read as abandoned rather than as a
ticket that closed and handed work on. Only two states are legal there:

  - [x]   done and verified
  - [~]   partly achieved, with the shortfall stated on the line

Anything else belongs to another ticket, in which case the line leaves this
checklist entirely and goes in a `## Descoped` section naming where it went --
"moved" has to mean moved, or the box sits unticked forever. SUPERSEDED and
DROPPED tickets are exempt: unfinished work is what those words mean.

Finally it checks the `## Current ticket sequence` at the top of the board. That
section exists so a session that lost its context knows what happens next, which
means it is acted on *without* being verified -- so a stale one is worse than
none. Two ways it rots silently and both are checkable: no date to judge it by,
and an item whose ticket has already closed.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BACKLOG = ROOT / "claude_documentation" / "backlog"
DOCS = ROOT / "claude_documentation"

ALLOWED = {
    "completed": ("✅ DONE", "❌ SUPERSEDED", "❌ DROPPED"),
    "inprogress": ("🚧 IN PROGRESS", "⏸ BLOCKED"),
    "open": ("🔜 TODO", "⏸ BLOCKED"),
}

ROW = re.compile(
    r"\| \[(T\d{4})\]\((open|inprogress|completed)/([^)]+)\) \|[^\n]*?\| "
    r"(✅ DONE|🔜 TODO|🚧 IN PROGRESS|⏸ BLOCKED|❌ SUPERSEDED|❌ DROPPED)"
)
STATUS = re.compile(r"\| \*\*Status\*\* \| ([^|]+?) \|")
LINK = re.compile(r"\]\(([^)#\s]+\.md)\)")
UNTICKED = re.compile(r"^\s*[-*] \[ \]")

SEQ_HEADING = "## Current ticket sequence"
SEQ_DATE = re.compile(r"^\*\*Set (\d{4}-\d{2}-\d{2})", re.M)
# Only the numbered rows of the sequence table, never a ticket named in its prose
# -- the prose cites completed tickets on purpose and flagging those would train
# everyone to ignore this check.
SEQ_ROW = re.compile(
    r"^\| \d+ \| \[(T\d{4})(?:\.\d+)?\]\((open|inprogress|completed)/([^)]+)\)", re.M
)


def check_sequence(readme: str) -> list[str]:
    """The sequence at the top of the board says what happens next, in order."""
    start = readme.find(SEQ_HEADING)
    if start < 0:
        return [
            f"the board has no '{SEQ_HEADING}' section -- it is what a session "
            "reads first after a context reset, so restore it rather than "
            "deleting it; an empty sequence is a sentence, not a missing heading"
        ]
    section = readme[start + len(SEQ_HEADING):]
    end = section.find("\n## ")
    if end >= 0:
        section = section[:end]

    problems: list[str] = []
    if not SEQ_DATE.search(section):
        problems.append(
            "the current ticket sequence has no '**Set YYYY-MM-DD' line -- an "
            "undated sequence cannot be judged stale, and this one is trusted "
            "without being checked; re-date it whenever you change it"
        )

    rows = SEQ_ROW.findall(section)
    if not rows:
        problems.append(
            "the current ticket sequence lists no tickets -- if there genuinely "
            "is no sequence, say so in prose; an empty table reads as an "
            "oversight rather than a state"
        )
    for tid, folder, fname in rows:
        if not (BACKLOG / folder / fname).exists():
            problems.append(
                f"{tid}: the current ticket sequence points at {folder}/{fname}, "
                "which does not exist"
            )
        elif folder == "completed":
            problems.append(
                f"{tid}: the current ticket sequence lists it as upcoming, but it "
                "sits in completed/ -- drop the row and re-date the sequence, or "
                "the next session works something already done"
            )
    return problems


def main() -> int:
    problems: list[str] = []
    readme = (BACKLOG / "README.md").read_text()

    seen = set()
    for tid, folder, fname, board_state in ROW.findall(readme):
        seen.add(tid)
        path = BACKLOG / folder / fname
        if not path.exists():
            problems.append(f"{tid}: board points at {folder}/{fname}, which does not exist")
            continue
        if board_state not in ALLOWED[folder]:
            problems.append(f"{tid}: sits in {folder}/ but the board says {board_state}")
        text = path.read_text()
        own = STATUS.search(text)
        own_state = own.group(1).strip() if own else "(no Status field)"
        if not any(own_state.startswith(s) for s in ALLOWED[folder]):
            problems.append(f"{tid}: sits in {folder}/ but its Status field says '{own_state}'")

        # A DONE ticket with an unticked box reads as abandoned, whatever the
        # prose next to it says -- and the board renders the boxes, not the prose.
        if own_state.startswith("✅ DONE"):
            for lineno, line in enumerate(text.splitlines(), 1):
                if UNTICKED.match(line):
                    excerpt = line.strip()[:72]
                    problems.append(
                        f"{tid}: DONE but {folder}/{fname}:{lineno} is unticked -- "
                        f"'- [~]' it with the shortfall stated inline, or remove the "
                        f"line and record it under '## Descoped': {excerpt}"
                    )

    # A ticket on disk with no board row is invisible to anyone reading the board.
    for folder in ALLOWED:
        for path in (BACKLOG / folder).glob("[0-9]*.md"):
            tid = "T" + path.name[:4]
            if tid not in seen:
                problems.append(f"{tid}: {folder}/{path.name} has no row in the board table")

    problems += check_sequence(readme)

    for md in DOCS.rglob("*.md"):
        for target in LINK.findall(md.read_text()):
            if not (md.parent / target).resolve().exists():
                problems.append(f"broken link in {md.relative_to(ROOT)}: {target}")

    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
    if problems:
        print(f"\n{len(problems)} backlog inconsistency/ies", file=sys.stderr)
        return 1
    print(
        f"backlog consistent: {len(seen)} tickets, folder/Status/board agree, "
        "links resolve, no DONE ticket has an unticked box, the current "
        "sequence is dated and nothing in it is already closed"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
