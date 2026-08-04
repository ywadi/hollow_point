# T0054 — Logging and diagnostics

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 2 — Engine skeleton |
| **Order** | 40 |
| **Created** | 2026-08-03 |

## Why

Diligent provides only `DebugOutput.h` — no levels, categories, sinks or file
output. Every subsystem needs to report, and retrofitting logging across an
existing codebase is grim, so it goes in with the skeleton.

The editor console panel (T0066) is a sink on this; so is the log file that makes
a bug report from a user actionable.

## Done when

- [x] Levels: trace, debug, info, warning, error, fatal
- [x] Categories per subsystem, filterable independently
- [x] Multiple sinks — sink interface plus console and file. **Editor panel (T0066) and Tracy (T0029) do not exist yet**; they are sinks on this interface when they do
- [x] Compile-time level floor (`HP_LOG_MIN_LEVEL`) — arguments are not evaluated below it
- [x] Thread-safe — shared_mutex over sinks and categories, atomic levels
- [ ] Diligent's own `DebugOutput` is routed into it — **blocked**: the engine deliberately links nothing from Diligent yet (T0013's 13.3), so there is no callback to install
- [x] Formatting is type-safe — `std::format`, verified on both targets

## Subtasks

- [x] 54.1 Core log API with levels and categories
- [x] 54.2 Sink interface; console and rotating file sinks
- [x] 54.3 Compile-time minimum level
- [~] 54.4 Thread safety without blocking the caller — **half done and stated as
      such**. Correct under concurrency (shared_mutex, atomic levels), but a
      caller still writes to sinks on its own thread. The file sink relies on
      stdio buffering and flushes only on Error/Fatal, which removes the common
      case of a synchronous write per line. A queued sink with a flush thread is
      deliberately deferred: there are no worker threads yet (T0026), so it
      would be untestable speculation, and the sink interface is the seam where
      it lands
- [ ] 54.5 Install a Diligent debug-message callback — **blocked on the engine linking Diligent** (T0015/T0025)
- [ ] 54.6 Bridge to Tracy messages — **blocked on T0029**
- [x] 54.7 Formatting: **`std::format`**. The toolchain is happy with it — verified including `vformat`/`make_format_args` on both targets

## Notes / findings

**Do not let logging block the calling thread.** A synchronous file write inside
a hot loop or a worker job produces mysterious frame spikes. Queue and flush on a
dedicated thread, or at minimum buffer.

**Route Diligent's messages in rather than beside.** Diligent takes a debug
message callback; wiring it here means validation-layer output, shader compile
errors and engine warnings land in the same stream and the same editor console,
which is worth a lot when debugging.

Categories matter more than they look: with everything under one category the
only realistic filter is "off", which means it gets turned off.


## Findings

**`std::format` works fully on both targets**, including the runtime path
(`vformat` + `make_format_args`) a logger needs. That settles 54.7 with no
third-party formatting library. One trap found while checking: newer libc++
removed the rvalue overload of `make_format_args`, so `make_format_args(1, 2)`
with literals is ill-formed and must be lvalues. **My first test failed for that
reason and briefly looked like "std::format is unavailable"** — the second
verification error of this kind today, and the lesson is the same: when a check
fails, suspect the check.

**`LogCategory` is a 16-bit id into engine-owned storage, not an object.** This
is a hot-reload decision (T0048). A category owning its own name and level, with
the engine holding a pointer to it for the editor's filter list, would dangle
the moment a gameplay module that declared one unloaded. An id cannot dangle:
the worst case after an unload is a registered name nobody logs to. It also
makes the same category name in two translation units resolve to one channel,
which is asserted by a test.

**Sinks are registered non-owning, deliberately.** Taking a `unique_ptr` would
mean the engine freeing memory a gameplay module allocated, which the
conventions forbid — each library carries its own statically linked libc++.
Built-in sinks are created *by* the engine (`logAddConsoleSink`,
`logAddFileSink`) so nothing allocated crosses the boundary at all.

**Filtering happens before formatting**, and there is a test asserting it. The
macro checks `logEnabled` first, so a suppressed line costs a comparison rather
than a `std::format` call. The test passes a side-effecting argument and asserts
the counter stays at zero — the same shape as T0019's proof, because it is the
same failure.

**Warnings and worse go to stderr.** A CI log that captures only stdout would
otherwise silently drop exactly the lines worth reading.

## Evidence

```
$ zig build test -Dtest=integration
[doctest] test cases: 14 | 14 passed | 0 failed | 0 skipped
[doctest] assertions: 61 | 61 passed | 0 failed |          (x2 -- Linux, Windows under wine)
```

Seven logging cases: sink delivery, filter-before-format, independent category
filtering, same-name-same-channel, removal stopping delivery, double
registration not duplicating, and the file sink writing and flushing on error.

Exercised for real by both apps, on both targets:

```
$ ./build/linux-x86_64-release/apps/editor/hp_editor
[info ] editor: HollowPoint editor
[info ] editor: engine 0.0.1-skeleton, 1 instance(s), 1 consumer(s)
$ wine build/windows-x86_64-release/apps/runtime/hp_runtime.exe
[info ] runtime: HollowPoint runtime
[info ] runtime: engine 0.0.1-skeleton, 1 instance(s), 1 consumer(s)
```

## Not done

**54.5 is blocked, and it is worth understanding why rather than treating it as
an oversight.** Routing Diligent's `DebugOutput` into this logger needs the
engine to link Diligent, and T0013 deliberately linked nothing from it (13.3),
because nothing needs a device yet. It becomes possible with T0015/T0025 and
should be done *then* — validation-layer output and shader compile errors in the
same stream is most of the value of this ticket.

**54.6 (Tracy bridge) is blocked on T0029**, which has not started.

**54.4 is half done** — see the subtask. Correct, not yet asynchronous.


## Closing note

Closed on what it exists for: levels, categories, sinks, a compile-time floor,
thread safety and type-safe formatting, all working and tested on both targets.

**Two items were moved rather than ticked**, and the distinction matters:

- **54.5** (route Diligent's `DebugOutput` into this) is now recorded on
  [T0025](../inprogress/0025-render-layer.md), which is where the engine first links
  Diligent at all. It is most of this ticket's remaining value and should be
  done *then*, not later.
- **54.6** (Tracy message bridge) waits on T0029, which owns the Tracy client.

**Why moved rather than left BLOCKED:** this ticket sits at order 40 and the
prerequisite lands at order 380. A ⏸ BLOCKED ticket near the head of an
execution-ordered board, unable to move for the whole of Phases 2 and 3, makes
the board lie about what to work next. Closing it and recording the remainder
where it will actually be picked up keeps both honest.

**54.4 remains half done and is not carried anywhere**, because it is not
blocked — it is deferred on purpose. Logging is correct under concurrency, but
the caller still writes to sinks on its own thread. A queued sink with a flush
thread needs worker threads to be worth building and testing (T0026), and the
sink interface is the seam where it lands with no change to any call site.
