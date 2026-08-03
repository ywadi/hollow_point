# T0113 — Device loss: decide the policy, fail distinguishably

| | |
|---|---|
| **Status** | 🔜 TODO |
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

- [ ] Policy recorded in the decision log: device loss is **fatal**, with
      recreate-and-continue explicitly rejected for now and the revisit
      trigger named (e.g. evidence of frequent recoverable losses in the wild)
- [ ] Detection actually exists: the error paths from `Present` and command
      submission are checked in T0025's device code on the Vulkan backend, and
      what the OpenGL backend can even report is investigated and recorded
      (GL's equivalent is context loss / robustness, and Diligent's surface
      for it is currently unknown -- find out, do not assume)
- [ ] The failure routes through the fatal path with a **distinguishable
      message** -- naming the condition as a GPU/driver failure, distinct from
      an engine crash, so a player report of "device lost" is immediately
      recognisable and not triaged as memory corruption
- [ ] The log is flushed on this path (T0054's flush-on-fatal hook -- T0099.4)
- [ ] A development story for triggering it exists at least on paper: what was
      tried (a deliberately hanging compute shader trips the OS driver
      timeout, at the cost of a rough couple of seconds for the machine), and
      whether a cheaper simulation hook is available

## Subtasks

- [ ] 113.1 Write the policy sentence and the rejection into the decision log
- [ ] 113.2 Detection points in T0025's present/submission code, Vulkan first
- [ ] 113.3 Message and routing: through the same fatal path T0099 formalises,
      with the crash-report file naming the condition and the GPU/driver
- [ ] 113.4 Investigate the OpenGL side: what Diligent exposes for GL context
      loss, and record whether the GL backend can do better than "undefined"
- [ ] 113.5 Testing story, including whether a deliberate-hang shader is worth
      keeping behind a debug flag

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
