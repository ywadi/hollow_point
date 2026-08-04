# T0110 — Presentation: vsync, present modes, frame pacing and focus loss

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 375 |
| **Created** | 2026-08-03 |
| **Blocks** | — (was T0025; withdrawn 2026-08-04, see the correction below) |
| **Refs** | T0014, T0015, T0031, T0052, T0057, T0078, T0100, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D16, [../../documentation/07-design-gaps.md](../../documentation/07-design-gaps.md) item 1, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md) , [../inprogress/0068-input-mapping.md](../inprogress/0068-input-mapping.md) , [../open/0129-display-modes-and-window-control.md](../open/0129-display-modes-and-window-control.md) |

## Correction (2026-08-04) — measured against the vendored Diligent

Two claims in this ticket were wrong about the API it has to drive. Both were
found by reading `DiligentCore` before writing any code, and both change what
the ticket can promise.

**1. This does not block T0025, and the `Blocks` field is withdrawn.**

The stated reason was that "the present mode is chosen where the swap chain is
created and presented", so hardening T0025 first would force a retrofit. In the
vendored Diligent, vsync is a **per-call argument**:

```cpp
VIRTUAL void METHOD(Present)(THIS_ Uint32 SyncInterval DEFAULT_VALUE(1)) PURE;
```

The only creation-time knob touching presentation is `SwapChainDesc::BufferCount`
(default 2). So the coupling is one integer per present call plus one field —
two seams to leave, not a policy that must exist first. T0025 and this ticket
are now being done together, which is what the real coupling supports.

**2. "No tearing under the vsync mode" is not something this engine can promise.**

Diligent does not expose present-mode selection at all. It derives the mode from
a boolean (`SwapChainVkImpl.cpp:332-350`):

| vsync | preferred, in order |
|---|---|
| on | `FIFO_RELAXED`, then `FIFO` |
| off | `MAILBOX`, then `IMMEDIATE`, then `FIFO` |

With vsync **on** it prefers `FIFO_RELAXED`, and Diligent's own comment says that
mode "still shows [a late frame] even if VSync has already passed, **which may
result in tearing**". So the Done-when as originally written fails on any
machine that misses a frame — not because the implementation is wrong, but
because it asks for a guarantee the chosen mode does not make.

Restated above. If genuine tear-free presentation is wanted, that is a decision
to force plain `FIFO`, and it cannot be expressed through Diligent's boolean —
it needs a patch or a bypass, which is 110.5's "advanced option" question with a
real cost attached.

**3. Toggling vsync rebuilds the swap chain.** `SwapChainVkImpl.cpp:746` forces
`VK_ERROR_OUT_OF_DATE_KHR` when the flag differs from the last present, then
recreates. The device survives, so "applied at runtime without recreating the
device" still holds — but 110.1 and T0025's resize handling (25.3) are the *same
recreate path* and should be built as one thing.

## Why

No ticket owns how a rendered frame reaches the screen. `vsync`, `swap
interval`, `MAILBOX`, `FIFO`, `tearing`, `frame pacing`, `frame-rate cap`
returned zero hits across every ticket and every documentation file when the
design-gap survey ran (2026-08-03). The one occurrence of "present mode" in the
repository is a log line pasted as evidence into completed T0003:

```
Diligent Engine: Info: Using VK_PRESENT_MODE_IMMEDIATE_KHR swap chain present mode
```

That line is worth staring at: the engine's *current observed behaviour* is
uncapped rendering with tearing -- Diligent's default, chosen by nobody. This is
not a hypothetical absence; the wrong value is already running.

Where it fell between tickets: T0025 owns the swap chain (creation, resize,
shutdown) but has no present policy. T0014's loop is "poll → update layers →
render → present" with present as an unexamined step. T0100 owns "the frame's
anatomy... the single ordered list of what runs when" and never mentions
presentation or pacing. T0078's game options are "resolution, volume, keybinds"
with no display section. T0015's display-modes note claims window-side display
state (fullscreen, resolution, DPI) but not presentation -- and D16's SDL3
decision brings display capability but says nothing about present modes either,
because presentation is the swap chain's side of the fence: Diligent's
`ISwapChain` creation parameters and `Present(SyncInterval)`, per backend.

Why it is ordered before T0025: the present mode is chosen where the swap chain
is created and presented. If T0025 hardens first, the policy gets retrofitted
into code that already picked a default -- which is exactly how the
`IMMEDIATE` line above happened. The settings plumbing (T0078), the swap-chain
code (T0025) and the loop (T0014) are all still unwritten, so owning this now
is cheap.

## Done when

- [ ] Present mode is selected deliberately per backend -- FIFO/MAILBOX/IMMEDIATE
      on Vulkan, swap interval on OpenGL -- and the mapping from the user-facing
      vsync option to per-backend behaviour is recorded. This is per-backend
      code, the same shape as T0096's sRGB-framebuffer handling
- [ ] Vsync is a player-facing display option (T0078's display section, see the
      amendment there), applied at runtime without recreating the device
- [x] A frame-rate cap exists independent of vsync — and works headless, so
      it does not depend on a window existing at all
- [x] The **editor** does not render uncapped — measured 116 fps → 57.6 fps.
      Mechanism: **a cap**, not vsync and not render-on-demand; reasons below
- [x] Focus-loss policy decided and implemented: **cap hard, and drop held
      input**. Pause and mute explicitly *not* included — reasons below
- [ ] T0100's frame anatomy names the present/pacing step explicitly, so pacing
      has a defined place in the frame rather than being wherever `Present`
      landed
- [ ] Verified on both backends and both targets: the vsync mode behaves as the
      *chosen* Diligent path implies (see the correction — "no tearing" is not
      available as written), the cap holds within measurement error, and the
      background cap engages on focus loss

## Subtasks

- [ ] 110.1 Present-mode selection at swap-chain creation and a runtime vsync
      toggle path -- lands inside T0025's device code, driven by this ticket's
      policy
- [x] 110.2 Frame-rate cap — sleep-then-spin, measured at 30/60/120 Hz
- [x] 110.3 Focus-loss handling — background cap + input reset; simulation
      pause and audio mute deliberately excluded, see below
- [x] 110.4 Editor pacing — capped at 60 focused / 10 background
- [ ] 110.5 Display options wired into T0078: vsync on/off, cap value
      (off/30/60/120/custom), and whether raw present-mode selection is exposed
      as an advanced option or kept internal
- [ ] 110.6 Verification pass on both backends: tearing absent, cap accuracy
      measured, focus-loss behaviour observed, and the results pasted here

## Notes / findings


### Frame anatomy — phase 11 — present (T0100, D17)

Present is **phase 11**. This ticket owns the *policy* (present mode, vsync,
cap, focus loss); the frame anatomy owns the *position*. Neither document should
duplicate the other's half.

The full order is in [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md); the decision and what it rejected is **D17** in the
decision log. If this ticket needs a phase that does not exist, that is a change
to T0100's document and to D17 — not a new call bolted into `Application::run`.

- **The mode names are the policy vocabulary.** FIFO is classic vsync (no
  tearing, latency up to a frame); MAILBOX is no tearing with lowest latency
  but renders continuously (no power saving -- not a laptop default); IMMEDIATE
  is tearing and uncapped, defensible only as an explicit "vsync off" choice.
  OpenGL has only a swap interval (0/1, sometimes -1 for adaptive) -- the
  user-facing options must be expressible on *both* backends, which is why the
  mapping is a recorded decision and not per-backend improvisation.
- **Pacing feeds two other systems and they are noted here so they are not
  re-derived:** T0057's fixed-step accumulator consumes whatever frame time
  pacing produces (a paced frame keeps the accumulator well-behaved; an
  unpaced one exercises the delta clamp constantly), and T0031's frame budgets
  are meaningless against an unpaced frame -- a budget of 16.6 ms assumes
  something is holding frames near 16.6 ms.
- The focus-loss policy is deliberately *this* ticket's decision, not T0015's:
  the window layer reports the event; what the game does with it (render rate,
  simulation, audio) is presentation-and-frame policy.

### Cross-ticket obligation — T0068 (2026-08-04)

The action layer is built, and two of its loose ends are **this ticket's policy
call**, not its own:

- **`InputSystem::reset()` exists and nothing calls it.** A window that loses
  focus while a key is held never receives the key-up, so the action stays held
  forever — the character keeps walking into a wall while the player is in
  another application. The hook is deliberately provided without a policy,
  because *what focus loss means* is this ticket's decision: clear all input, or
  clear only held state, or suppress the whole input context stack.
- **Relative mouse capture must be released on focus loss and re-acquired on
  focus gain**, or the pointer is trapped in a window the user is not looking
  at. T0068's amendment flags this as the case that reliably breaks. Cursor
  control is not built yet, so the constraint lands here before there is code to
  retrofit it into.

### Cross-ticket obligation — T0129 (2026-08-04)

**T0129 waits on 110.3, and the dependency is one-way.** This ticket decides what
focus loss means — cap hard, pause, mute, some combination — and fullscreen adds
"and release the display mode" to that same answer. Deciding it here once is the
difference between one policy and two that must later be reconciled.

Also: **exclusive fullscreen interacts with presentation.** On some drivers it is
what enables a true immediate/mailbox present path, so 129.6's decision wants
this ticket's present-mode work in view rather than after it.

## 110.2, 110.3 and 110.4 done (2026-08-04)

### The cap holds, and the wait strategy is why

The subtask warned that a naive `sleep` has millisecond-scale jitter and gives
pacing *worse* than none. Sleep-then-spin — sleep to 1.5 ms short of the
deadline, then yield-spin the rest — measured over 230 frames after warm-up:

| cap | target | mean | p50 | p99 | max abs error | achieved |
|---|---|---|---|---|---|---|
| 30 | 33.333 ms | 33.333 | 33.333 | 33.334 | 0.022 ms | 30.0 fps |
| 60 | 16.667 ms | 16.667 | 16.667 | 16.697 | 3.953 ms | 60.0 fps |
| 120 | 8.333 ms | 8.333 | 8.333 | 8.360 | 1.316 ms | 120.0 fps |

Mean and median land on the target to three decimal places and p99 is within
0.03 ms. The `max abs error` column is honest about the tail: a rare scheduler
hiccup still costs several milliseconds, and no user-space wait can prevent
that. What it does *not* do is accumulate — the deadline advances by exactly one
period from the **previous deadline** rather than from "now", so an overshoot is
absorbed by the next frame instead of dragging the average down. Falling more
than a whole period behind resets the baseline rather than trying to catch up,
because catching up means a burst of unpaced frames.

**Measured headless**, which matters: the cap must not depend on a window
existing, or "a hidden window must not render at 3000 fps" is unenforceable
exactly when it is needed.

### The editor: a cap, not vsync, and not render-on-demand

Measured 116 fps → 57.6 fps with the cap at 60.

Vsync alone was rejected as the mechanism: it does nothing for a hidden or
minimised window and a driver may ignore it, so it is not a battery guarantee.
Render-on-demand is the right long-term answer for an editor and is a much
larger change — it needs every panel to know when it is dirty — so it is not
attempted here. The cap is the cheap thing that removes the daily-life bug now,
and it does not block render-on-demand later.

Runtime is uncapped while focused, deliberately: a game decides its own frame
rate and vsync already holds it to the refresh rate. Its *background* cap is not
a game's decision, because nothing benefits from a minimised game rendering flat
out.

### Focus-loss policy: cap hard, drop held input — and nothing else

Two things happen on focus loss, and the second is the one that would otherwise
be a bug report nobody could reproduce:

1. The background cap applies (10 Hz by default).
2. **`InputSystem::reset()` is called**, closing the hook T0068 deliberately left
   without a policy. A window that loses focus while a key is held never
   receives the key-up, so without this the action stays held forever — the
   character walks into a wall while the player is in another application.

Focus is observed in the application's own pump handler rather than in a layer,
because a layer that consumed the event would silently take both behaviours
away.

**Pausing simulation and muting audio are deliberately excluded.** Pausing is
T0057's clock and a game's decision — a server-authoritative or
multiplayer-adjacent game must not pause on alt-tab, and the engine should not
make that choice for it. Muting is T0052's, and there is no audio yet. Choosing
those here would be inventing policy for subsystems that do not exist, which is
how a default becomes a constraint nobody agreed to.

## Still open

- **110.1's vsync toggle works but its policy is not decided** — the runtime
  toggle is implemented and exercised (T0025), and what the *user-facing* vsync
  option maps to per backend is still unwritten.
- **110.5 is blocked on T0078** (display options).
- **110.6, the verification pass on both backends**, is not done: the cap and the
  focus behaviour are measured on Linux only, and the tearing question was
  restated rather than tested.
- **Focus loss itself has not been observed end to end** — the code path is
  wired and the reset is called from it, but no test alt-tabs a window. The
  behaviour of the two pieces it calls is verified; the trigger is not.
