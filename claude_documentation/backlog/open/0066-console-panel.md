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


### Amendment (2026-08-03) — gameplay code writes to this console, and that is tested

The question this panel exists to serve is not "can the engine log" but "can a
game developer see their own output". It can, and the boundary suite asserts it
rather than leaving it to be discovered:

```
tests/integration/module_boundary_test.cpp
  "gameplay code can write to the engine's log across the boundary"
```

A category declared **inside the loaded module** (`game.sandbox`) logs a line,
and an engine-side sink receives it with the message and the category name
intact. Two earlier decisions make that work, both taken for other reasons:

- The engine is a **shared** library (D12), so the module and its host share one
  logger instead of each linking a private copy. Under a static engine the
  module's log lines would go into a second, invisible sink list.
- `LogCategory` is an **id into engine-owned storage**, not an object (T0054). A
  category declared in a module that can unload would otherwise leave the engine
  holding a pointer into a library that is gone -- and the editor's filter list
  is exactly the thing that would hold it.

**What this panel therefore owns**: rendering, filtering by category and level,
search, and copy-out. It does *not* own message capture -- it is a sink on
`hp::ILogSink`, registered by the editor. Sinks are non-owning, so the panel
must outlive its registration or call `logRemoveSink` first.

**Category filtering is the feature that makes it usable.** `logCategoryCount`
and `logCategoryAt` exist to enumerate categories for exactly this UI, and
gameplay categories appear in that list alongside engine ones automatically --
there is nothing for a game developer to register.

Not to be confused with [T0114](0114-runtime-developer-console-and-cvars.md),
which is a *command* console for typing at the running game. This one is the log
viewer.
