# T0054 — Logging and diagnostics

| | |
|---|---|
| **Status** | 🔜 TODO |
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

- [ ] Levels: trace, debug, info, warning, error, fatal
- [ ] Categories per subsystem, filterable independently
- [ ] Multiple sinks: console, file, editor panel, and Tracy messages
- [ ] Compile-time level floor so verbose logging costs nothing in release
- [ ] Thread-safe — worker threads will log
- [ ] Diligent's own `DebugOutput` is routed into it, not left separate
- [ ] Formatting is type-safe

## Subtasks

- [ ] 54.1 Core log API with levels and categories
- [ ] 54.2 Sink interface; console and rotating file sinks
- [ ] 54.3 Compile-time minimum level
- [ ] 54.4 Thread safety without blocking the caller (see notes)
- [ ] 54.5 Install a Diligent debug-message callback that forwards into ours
- [ ] 54.6 Bridge to Tracy messages (T0029) so logs appear on the timeline
- [ ] 54.7 Decide the formatting mechanism — `std::format` if the toolchain is
      happy with it, otherwise a small alternative

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
