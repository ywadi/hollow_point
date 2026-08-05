# T0029 — Tracy: vendor and CPU profiling

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 5 — Profiling |
| **Order** | 560 |
| **Created** | 2026-08-02 |
| **Refs** | [../completed/0046-frame-render-targets.md](../completed/0046-frame-render-targets.md), [../completed/0054-logging.md](../completed/0054-logging.md), [../completed/0057-time-system.md](../completed/0057-time-system.md) |

## Why

Profiling is going to be central to this project, and guessing at performance is
how engines end up slow in ways nobody can explain. Tracy is a frame profiler
with nanosecond-resolution CPU zones, a mature external viewer, and — critically —
GPU timing (T0030), which a hand-rolled `ScopeTimer` could never give us.

T0019 already put the macro surface in place and instrumented as code was
written, so this ticket is largely a mapping exercise rather than a retrofit.

## Done when

- [ ] Tracy vendored as a submodule at a pinned tag, cross-compiling to both
      targets
- [ ] `HP_PROFILE_*` macros map onto Tracy when profiling is enabled
- [ ] The Tracy viewer connects to a running build and shows the frame loop,
      layer updates and render submission as distinct zones
- [ ] Disabled builds contain no Tracy code at all — verified, not assumed
- [ ] Worker threads (T0026) appear named in the timeline
- [ ] `BUILDING.md` documents how to enable it and connect the viewer

## Subtasks

- [ ] 29.1 Vendor `wolfpld/tracy` at a pinned tag; confirm the client builds for
      `x86_64-windows-gnu` (see notes — this is the main risk)
- [ ] 29.2 Map `HP_PROFILE_ZONE` / `_NAMED` / `_FRAME` onto Tracy macros
- [ ] 29.3 Wire `HP_PROFILING` through the toolchain to `TRACY_ENABLE`
- [ ] 29.4 Name threads via Tracy's thread-naming API
- [ ] 29.5 Confirm a disabled build has no Tracy symbols (`nm`/`objdump`)
- [ ] 29.6 Document the workflow, including that the viewer is a separate app

## Notes / findings

### From T0046 (2026-08-05) — render-target memory is computed and has nowhere to go

`hp::FrameTargets::memoryBytes()` reports what the frame targets cost, computed
from the declared formats and sizes and asserted against exact byte counts on a
real device. **T0046 closed with the display side moved here**, because what is
missing is a profiler to show it in, not a number to show.

It is deliberately what was *asked for* rather than what the driver allocated —
alignment and tiling make the real figure somewhat larger. That is good enough
for "what is eating VRAM", which is the question it exists to answer, and the
distinction is worth keeping in whatever displays it.


### Inherited (2026-08-05) — two closed tickets left work here

Both closed rather than sitting BLOCKED near the head of the board, and both
recorded the remainder here rather than only on their own side.

- **T0054.6, the log → Tracy message bridge.** T0054's sink interface is the seam
  it lands on, with no change to any call site — a Tracy sink is just another
  sink. Nothing to bridge to until this ticket vendors the client.
- **T0057.6, the frame-time overlay** — the *display* rests on this ticket, the
  workflow on T0031. `hp::Clock` already exposes everything one would read
  (scaled and unscaled delta, elapsed, frame index, the fixed-step alpha), so
  T0057 needs no change when it is built.

**Main risk is the MinGW cross-build.** The Tracy client uses OS threading and
timing primitives; it supports MinGW, but this project's toolchain has already
turned up three separate MinGW-vs-MSVC issues (G2, G3, G4 — header case, import
library case, MSVC-style `.lib` names). Expect something similar and check early
rather than at the end.

Tracy's *client* is what we vendor and link. The *viewer* is a separate desktop
application; it does not need to be part of this build and probably should not be
(it pulls in its own GUI dependencies).

Tracy connects over TCP by default, which also means it works against the Windows
build running under wine.

Keep profiling **off by default** in Release and on in a dedicated profiling
configuration, so shipped builds carry no instrumentation.

### Architecture review (2026-08-03) — several Phase 4 tickets cannot close without this

T0045 ("culling cost visible in Tracy"), T0040 ("verified in Tracy, not
assumed") and T0050 ("threads named and visible in Tracy") all carry Done-when
conditions that require Tracy — and they are Phase 4, while this is Phase 5.
Either those verifications are deferred, or this ticket is pulled to the
*start* of Phase 4. The latter is the better plan: the renderer is exactly the
code that should be built with a profiler attached, and the T0019 macro
surface means the wiring cost is small. Flagged rather than re-phased —
owner's call.


### Architecture amendment (2026-08-03) — one Tracy client, and it lives in the engine

D12 makes the engine a shared library that the editor, the runtime and every
gameplay module link. Tracy's client has global state — a profiler singleton, a
queue, a background thread — and so falls under exactly the rule D12 exists to
enforce: **it must exist once per process.**

- **Compile Tracy into the engine shared library.** The gameplay module links
  the engine and imports those symbols; it must never embed its own copy. Two
  clients means two queues, the module's zones missing from the capture, and
  two things trying to own the listen socket.
- **Zones from game code are the point**, not a bonus. Verify a zone emitted
  inside the gameplay module appears in the same capture as engine zones, on
  both targets — that is the acceptance test for this ticket under D12.

**The sharp edge: Tracy does not copy its strings.** Each zone has a static
`SourceLocationData` holding pointers to `__FILE__`, the function name and the
zone name, and Tracy keeps those pointers, resolving them when the capture is
serialised. For a zone in the gameplay module, those strings live in *the
module's* memory. Unload the module and Tracy holds pointers into unmapped
memory — garbage names at best, a crash at capture time at worst.

This is the same constraint as the hot-reload problem, not a separate one.
T0095 found that a genuine `dlclose` already segfaults at exit under this
toolchain (zig links libc++ statically into every module), so modules are
currently loaded `RTLD_NODELETE` and stay mapped — which incidentally keeps
these strings valid. **Whatever T0048 decides about true unloading has to account
for Tracy's retained pointers**, and if modules ever genuinely unload, zone
names from a reloaded module are a correctness problem, not a cosmetic one.
