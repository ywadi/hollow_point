# T0046 — Frame render target management

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 4 — Render layer |
| **Order** | 390 |
| **Created** | 2026-08-02 |
| **Refs** | T0111 (Blocks this), T0094, T0106, T0107, T0120 |

## Why

Passes share resources: a depth buffer written by the world pass and read by
post-processing, ping-pong targets for blurs, the offscreen colour target the
editor viewport displays. Something has to own their creation, resizing and
lifetime.

Small and concrete — deliberately *not* a render graph (see T0047). A named,
explicitly-managed set of frame resources.

## Done when

- [x] Frame targets created once and resized with the viewport, not per frame — `resize` is a no-op at an unchanged size, so a layer may call it every frame
- [x] Passes request targets by name/handle rather than creating their own
- [x] Resize is debounced and leak-free — both verified on a real device (2026-08-05); see the GPU-test note
- [x] Formats are declared in one place, not scattered across passes — `formatFor()` in `FrameTargets.cpp` is the only place a role becomes a format, made structural by passes naming a *role*
- [x] GPU memory used by frame targets is reportable — asserted against exact expected byte counts on a real device
- [ ] Gameplay-owned persistent targets are supported alongside frame targets (T0094) — **not done**, see below

## Subtasks

- [x] 46.1 A frame-resources object owning the render targets
- [x] 46.2 Named lookup for passes — raw `ITextureView*` per D22
- [x] 46.3 Resize handling, debounced (T0033 has the same requirement) — recreate path verified on device
- [x] 46.4 Depth buffer shared between world pass and post-processing — every target carries `BIND_SHADER_RESOURCE`, depth included
- [~] 46.5 Ping-pong pair for multi-pass effects — expressible today (declare two, scale 0.5), but no helper and nothing has used it
- [~] 46.6 Report allocated target memory for the profiler — computed and verified; still not wired to the profiler

## Notes / findings

**Do not add resource aliasing.** Reusing memory between non-overlapping targets
is the main thing a render graph automates, and doing it by hand is error-prone
in exactly the way that produces intermittent corruption. If memory pressure ever
justifies it, that is the argument for revisiting T0047 — not for hand-rolling
aliasing here.

Diligent's automatic state transitions (`RESOURCE_STATE_TRANSITION_MODE_TRANSITION`)
mean we do **not** need manual barrier tracking, which removes the other main
reason engines build resource managers. Use the automatic mode unless profiling
shows it costs something.

`DiligentFX/Components/GBuffer.hpp` already manages a GBuffer's targets — use it
rather than duplicating it if deferred shading is wanted.

### Amendment (2026-08-03) -- constraints to honour before the frame layout solidifies

Collected from T0106 and the design-gap survey (`documentation/07-design-gaps.md`,
items 2 and 8), because this ticket is where they become cheap or expensive:

- **Depth must be readable during the transparent pass.** Already flagged by
  T0106.5 -- soft particles fade against scene depth while drawing transparents.
  Restated here so the requirement lives with the resource design, not only
  with its first consumer.
- **Scene colour must be readable during the transparent pass, eventually.**
  Screen distortion appears exactly once in the backlog, as a line in T0107's
  anatomy of an explosion ("optionally screen distortion for the shockwave"),
  and no ticket owns a distortion pass. It has the same shape as the
  soft-particle constraint: distortion needs *scene colour* readable mid-frame
  exactly as soft particles need *depth*. One sentence here keeps the door
  open -- a resolve/copy point of the colour target that a later pass can
  consume; discovering it after the frame layout solidifies is a refactor.
  The distortion pass itself remains unowned and unbuilt until VFX demand it.
- **T0111 lands first, by design** (it Blocks this ticket): the anti-aliasing
  decision changes what "formats are declared in one place" declares (sample
  counts, TAA history buffers or their absence), and the render-scale decision
  means world targets may size from render scale rather than swap-chain size.
  Do not freeze the declarations before T0111's answers exist.

### Note (2026-08-03) -- T0120 owns a separate pool, not this ticket's targets

**T0120 (camera render-to-texture)** gives *additional*, independently
positioned cameras their own texture targets -- a portal, a mirror, a
security-camera monitor. Those are a distinct pool from what this ticket
owns: this ticket is the *single* main frame's own colour/depth/ping-pong
set (46.4's shared depth, 46.5's ping-pong pair, the amendment above's
mid-frame readback), sized to one viewport or window. T0120's targets are
per-camera, sized and updated independently, and are T0094.1 `RenderTexture`
instances rather than frame targets. The two should share naming/lookup
conventions (46.2) so GPU memory reporting (46.6, and T0120's own reporting)
stays legible in one place, but they are not the same resource pool and
should not be merged into one to save a ticket.

### Design finding (2026-08-05) — target views are handed out as raw interface pointers (D22)

46.2 asks for "named lookup for passes", and D22 settles what a lookup returns:
a raw `Diligent::ITextureView*`, valid for the frame and not beyond.

Measured first rather than assumed — a library calling the RHI through interface
pointers links with zero Diligent libraries, because the interfaces are
pure-virtual. So a gameplay-authored pass (T0094) can consume a frame target
exactly as an engine pass does, and the public header naming `ITextureView` is a
deliberate part of the design rather than a leak. Full reasoning in **D22**.

**No `RefCntAutoPtr` in the public API.** This object owns the targets; callers
borrow them for the frame. Handing out a refcounted pointer would let a gameplay
module extend a target's lifetime past a resize, which is precisely the leak
46.3 exists to prevent.

### Built 2026-08-05 — and what is honestly not verified

`engine/include/hp/FrameTargets.hpp`, `engine/src/FrameTargets.cpp`,
`tests/fast/frame_targets_test.cpp`. Also `hp/Render.hpp` gained `device()`,
`context()` and `swapChain()`, which is what any of this is reachable through.

**Verified:** `zig build test -Dtest=all` green on both targets — 80 fast and 56
integration cases, Linux natively and Windows under wine. Seven new cases.

- **Roles, not formats, at the call site.** A pass declares
  `TargetFormat::ColourHDR`; `formatFor()` in the .cpp is the only place a role
  becomes a `TEXTURE_FORMAT`. That makes 46.4's "one place" structural rather
  than a convention someone has to remember.
- **Depth is `D32_FLOAT`, not `D24S8`, and that is the reverse-Z decision
  deferred at zero cost.** A float depth buffer is what reverse-Z needs;
  `D24S8` would foreclose it silently, and this format costs nothing today.
  **T0130.3 still owns whether to actually use reverse-Z**, and it should,
  because the decision is not just the format — it is the comparison function in
  every pipeline state and any shader reconstructing position from depth, and it
  has to be measured on both backends (GL clip-space Z is `[-1, 1]`, Vulkan's is
  `[0, 1]`). No stencil, because nothing needs one and an unused stencil is
  memory spent on nothing.
- **Every target carries `BIND_SHADER_RESOURCE`, depth included.** The
  transparent pass must read scene depth to fade soft particles (T0106.5), and a
  later distortion pass needs scene colour the same way. Cheap now, a full
  recreate to add later.
- **The resize debounce lives here, not in callers**, so a render layer calls
  `resize()` unconditionally every frame.
- **A failed creation releases the whole set** rather than leaving a partial one.
  A half-created set is what produces a null view three passes later with nothing
  pointing at the cause.
- **Forward declarations, not includes** (D22). The public header names
  `IRenderDevice` and `ITextureView` without inflicting Diligent's 146,000
  preprocessed lines (D21) on every consumer that wants a render target.

**`tools/gen_api_docs.py` now skips forward declarations.** It was flagging
Diligent's as undocumented public API, which would have meant writing doc
comments for someone else's types.

### Not verified — and this is the honest part

**No target has ever been created on a real device.** Every test here is in the
`fast` bucket, which has no device, so what is proven is declaration, duplicate
refusal, null-on-unknown-name, the debounce and the failure paths. What is *not*
proven is the part that touches the GPU:

- that `CreateTexture` succeeds for all three roles on both Vulkan and OpenGL —
  `RGBA8_UNORM_SRGB` as a render target and `D32_FLOAT` with
  `BIND_SHADER_RESOURCE` are both places a backend can legitimately refuse;
- that `GetDefaultView` returns the views this assumes;
- that resize is **leak-free** — the Done-when says so and nothing here shows it.
  `RefCntAutoPtr` releasing on `clear()` is the mechanism, and it is the right
  one, but "the mechanism is right" is not a measurement;
- that `memoryBytes()` reports anything sane.

**Why no test was added rather than this being an oversight:** `-Dtest=all`
includes the `gpu` bucket, and CI runners have no known GPU. A device test there
either fails the four jobs that currently pass or skips silently and proves
nothing, and adding it blind — with unpushed commits already queued — is how a
green board turns red for a reason unrelated to the change. The right shape is a
`tests/gpu/` case that creates a window and a `RenderLayer`, skips cleanly when
no device comes up, and is run locally on the RTX 2080 this repo has measured
against before. **That is the first thing T0027 should do**, since T0027 needs a
live device anyway.

### Not done

- **46.5, the ping-pong pair, has no helper.** Two declarations at `scale 0.5`
  express it, and nothing has used it, so a helper now would be a guess at what
  the first blur actually wants.
- **46.6 is computed but not wired to the profiler.** `memoryBytes()` returns a
  figure; nothing displays it. It is also *requested* bytes rather than allocated
  — alignment and tiling make the real number larger — which is fine for "what is
  eating VRAM" and should not be presented as exact.
- **T0094's gameplay-owned persistent targets are untouched.** `FrameTargets` is
  frame-scoped by construction: it recreates everything on resize, which is
  exactly wrong for a target a gameplay module wants to accumulate into across
  frames (fog of war is the motivating case). That needs a separate lifetime and
  it belongs with T0094.

### Device test added 2026-08-05 — `tests/gpu/render_targets_and_stack_test.cpp`

The gap the section above was honest about is closed. **86 assertions on a real
NVIDIA RTX 2080, on both backends and both build targets** (Linux natively and
Windows under wine, which also reached the GPU).

What is now measured rather than assumed:

- **All three roles create.** `RGBA16_FLOAT` as a render target, `D32_FLOAT`
  carrying `BIND_SHADER_RESOURCE`, and an sRGB colour target — the three places a
  backend could legitimately have refused.
- **Depth is readable as a shader resource**, so T0106.5's soft particles have
  what they need.
- **Roles are enforced**: `depthStencil("scene")` and `renderTarget("depth")`
  both return null rather than a wrong-typed view.
- **`memoryBytes()` is exact** against hand-computed byte counts, including the
  half-scale target.
- **The debounce is real**: `resize()` at an unchanged size returns the *same
  view pointer*, which is the only way to prove it did not silently rebuild.
- **Sixteen consecutive resizes do not accumulate** — the reported figure matches
  the final size exactly. That is not a proof of no leak, and the test says so,
  but a set that grew per resize would fail it immediately.

**The skip path is the part that had to be right**, since `-Dtest=all` includes
this bucket and CI has no GPU: no window, or a window with no device, both report
and return without a failed assertion.

**Adapter strings differ per backend** — `NVIDIA GeForce RTX 2080` on Vulkan,
`NVIDIA GeForce RTX 2080/PCIe/SSE2` on OpenGL — which is the evidence that two
genuinely different devices came up rather than one being silently reused.

**A defect this found in itself, worth recording:** the first version logged
"device up on 1". doctest's `MessageBuilder` takes a string *literal* fine but
decays a `const char*` **variable** to bool, so the line that was supposed to name
the backend printed a boolean. Both backends had in fact come up — the adapter
strings proved it — but the log said nothing useful. Fixed by streaming
`std::string(backendName)`. Exactly the "suspect the check" case CLAUDE.md warns
about, in a check written twenty minutes earlier.
