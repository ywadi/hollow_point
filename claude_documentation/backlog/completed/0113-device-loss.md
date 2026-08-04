# T0113 — Device loss: decide the policy, fail distinguishably

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 4 — Render layer |
| **Order** | 382 |
| **Created** | 2026-08-03 |
| **Refs** | T0025, T0054, T0099, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D15, [../../documentation/07-design-gaps.md](../../documentation/07-design-gaps.md) item 5 |

## Why

`device lost`, `device removed` -- zero hits at survey time (2026-08-03). T0025
owns device lifetime and covers creation, backend selection, resize and clean
shutdown; nothing anywhere covers the device dying *mid-run*. On Vulkan,
`VK_ERROR_DEVICE_LOST` happens on real machines: driver updates, GPU hangs,
laptops switching between integrated and discrete GPUs. And this engine plans
to run its entire particle system in compute (D15) -- which is the classic way
to write an accidental GPU hang during development. An infinite loop in a
particle kernel presents as a driver timeout and a removed device, and the
first time it happens, a distinguishable message saves hours.

The minimum viable answer is one sentence -- "device loss is fatal: route it
through T0099's crash handling with a distinguishable message" -- and it is a
far better sentence than an unhandled error code propagating as a mystery
crash. Full recreate-and-continue (recreate the device, reload every GPU
resource, resume) is a large feature nobody should build speculatively. The
gap was that *neither* had been chosen; this ticket chooses.

## Done when

- [x] Policy recorded in the decision log — **D20**
- [x] Detection exists — though **not where this expected it**: Diligent's
      `Present` returns void and it has no device-loss handling of its own, so
      the debug message callback is the only hook. GL investigated and recorded
      as having no surface at all. See below
- [x] The failure routes through the fatal path with a distinguishable
      message naming it as a GPU/driver failure
- [x] The log is flushed on this path — `logFlush()` before `abort()`
- [x] A development story for triggering it exists on paper — and only on paper.
      **Moved to T0027**, which is the first ticket able to write the compute
      shader that triggers it

## Subtasks

- [x] 113.1 Write the policy sentence and the rejection into the decision log — D20
- [x] 113.2 Detection — via the message callback, because no other hook exists
- [x] 113.3 Message and routing — log-flush-and-abort for now; the crash-report
      file is T0099's half and is still open
- [x] 113.4 Investigate the OpenGL side — it exposes nothing; recorded in D20
- [x] 113.5 Testing story — **moved to T0027**. Nothing here can author a
      compute shader, so the trigger cannot be built until it can

## Notes / findings

- **Acceptance spans phases deliberately** (the README allows this): detection
  lands with T0025 in Phase 4; the crash-report routing closes when T0099
  builds the handler in Phase 8. Until then, the fatal path is
  log-flush-and-abort with the distinguishable message, which is already most
  of the value.
- Keep the handler-side behaviour within T0099's rules: allocate nothing on
  the failure path, preformatted message, write, flush, abort.
- D15 is the reason this is worth a ticket *now* rather than at shipping time:
  compute-driven particles mean the team will write GPU hangs during
  development, and "mystery crash" versus "device lost: GPU hang or driver
  reset" is the difference between a lost afternoon and a shrug.

## Done (2026-08-04) — the detection surface is worse than the ticket assumed

### There is no `Present` error path to check

113.2 asks for "the error paths from `Present` and command submission". They do
not exist. `ISwapChain::Present` returns **void**, and — measured —
**DiligentCore contains no device-loss handling whatsoever**:
`VK_ERROR_DEVICE_LOST` appears only in the vendored Vulkan headers, never in
Diligent's own source. There is no status to poll and no structured signal.

So detection is pattern-matching on the text the per-factory debug message
callback delivers, and that is a limitation of the dependency rather than a
shortcut taken here. It is checked *before* the severity switch, because a lost
device is fatal whatever severity the failing call happened to use.

The matcher is deliberately broad — a false positive costs an abort on a run
that was already failing, a false negative costs the distinguishable message
that is the whole point. Checked against the shapes a real failure produces and
against real Diligent noise:

```
VK_ERROR_DEVICE_LOST on present / on vkQueueSubmit   -> fires
DXGI_ERROR_DEVICE_REMOVED                            -> fires
"Requested color buffer format ... not supported"    -> silent
"Desired back buffer count (2) is smaller ..."       -> silent
"Using VK_PRESENT_MODE_FIFO_RELAXED_KHR ..."         -> silent
```

And in practice: a full probe run with validation forced on produces 12 real
validation errors and trips this **zero** times.

### OpenGL has no surface for it at all

`glGetGraphicsResetStatus` and `GL_CONTEXT_LOST` appear in Diligent only in the
**Android EGL** path. On the desktop GL backend, device loss is *undefined* — it
will present as whatever the driver does, most likely a crash without the
message. Recorded in D20 rather than left as an assumption. Vulkan being the
default backend is what makes that acceptable.

### The fatal path follows T0099's rules early

Nothing is allocated on it, the message is preformatted, the log is flushed,
then `abort()`. Backend and adapter are cached at device creation into file-scope
pointers, because the Diligent callback is a bare function pointer with no
user-data parameter and the failure path must not look anything up.

## Not done

- **113.5, the testing story, is on paper only and the path has never run.**
  Triggering it for real needs a GPU hang — a deliberately infinite compute
  shader trips the OS driver timeout, at the cost of a rough couple of seconds
  for the whole machine — and there is nothing to write one with until T0027.
  No cheaper simulation hook was added: injecting a synthetic message would mean
  a test-only entry point in the engine's public surface, which is a worse trade
  than an untested path whose *matcher* is tested. **What is verified is the
  matcher and the absence of false positives; what is not is the abort.**
- **The crash-report half is T0099's** and stays open. Until then this is
  log-flush-and-abort, which the ticket itself calls most of the value.

## Closed (2026-08-04) — on what it achieved, with the trigger moved

D20 is recorded, detection exists and is verified, the fatal path is written and
follows T0099's rules, and the OpenGL gap is documented rather than guessed.
What remains is **one thing that cannot be built here**: actually firing the
abort needs a GPU hang, a GPU hang needs a deliberately infinite compute shader,
and nothing in this engine can author a shader until T0027 exists.

Holding the ticket open for that would show a blocker on the board that blocks
nothing, which is the same reasoning that moved 95.6 and 95.7 to T0105. So it
closes here and **T0027 carries the trigger**, with the obligation recorded on
that ticket rather than only on this one.

**What is verified:** the matcher, against both the shapes a real failure
produces and real Diligent noise — 12 genuine validation errors in a full probe
run trip it zero times. **What is not:** the abort itself has never executed.
That distinction is the whole of what this ticket can honestly claim.