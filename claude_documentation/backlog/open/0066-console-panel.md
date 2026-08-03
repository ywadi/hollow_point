# T0066 — Console panel

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 6 — Editor |
| **Order** | 670 |
| **Created** | 2026-08-03 |

## Why

A sink on the logging system (T0054), surfacing engine and gameplay output inside
the editor. Without it, diagnosing anything means running from a terminal and
losing the messages the moment the window scrolls.

Also the natural home for Diligent's validation-layer output, which is where
graphics bugs announce themselves.

## Done when

- [ ] Log messages appear live in a dockable panel
- [ ] Filter by level and by category
- [ ] Text search across messages
- [ ] Errors and warnings visually distinct, with counts
- [ ] Clear, and auto-scroll with a pause
- [ ] The panel does not degrade frame rate under heavy logging
- [ ] Clicking a message reveals its source location where available

## Subtasks

- [ ] 66.1 Register a sink on T0054 feeding a ring buffer
- [ ] 66.2 Panel rendering with per-level colouring
- [ ] 66.3 Level and category filters
- [ ] 66.4 Search
- [ ] 66.5 Error/warning counters in the panel header
- [ ] 66.6 Auto-scroll with pause-on-manual-scroll
- [ ] 66.7 Bound the buffer — see notes

## Notes / findings

**Bound the buffer and use ImGui's clipper.** An unbounded log is a slow memory
leak, and rendering ten thousand text lines per frame is far more expensive than
the work being logged — a genuinely common way to make an editor feel broken.

Auto-scroll should pause when the user scrolls up and resume when they return to
the bottom. Fighting the scroll position while reading an error is a small thing
that makes a console feel hostile.

Worth surfacing Diligent's validation output prominently; those messages are easy
to miss in a terminal and usually indicate a real bug.
