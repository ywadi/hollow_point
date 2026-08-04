# T0110 — Presentation: vsync, present modes, frame pacing and focus loss

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 375 |
| **Created** | 2026-08-03 |
| **Blocks** | T0025 |
| **Refs** | T0014, T0015, T0031, T0052, T0057, T0078, T0100, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D16, [../../documentation/07-design-gaps.md](../../documentation/07-design-gaps.md) item 1, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md) , [../inprogress/0068-input-mapping.md](../inprogress/0068-input-mapping.md) |

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
- [ ] A frame-rate cap exists independent of vsync -- menus and background
      windows must not render at 3000 fps
- [ ] The **editor** does not render uncapped. An editor burning a laptop
      battery at maximum fps is a daily-life bug; the chosen mechanism (vsync,
      cap, or render-on-demand) is recorded
- [ ] Focus-loss policy is decided and implemented: what a backgrounded game
      does -- cap hard, pause, mute, some combination. "Focus" appeared in five
      files at survey time, all about input focus or focus-on-selection, none
      about this. Interacts with pause (T0057) and, later, audio (T0052)
- [ ] T0100's frame anatomy names the present/pacing step explicitly, so pacing
      has a defined place in the frame rather than being wherever `Present`
      landed
- [ ] Verified on both backends and both targets: no tearing under the vsync
      mode, the cap holds within measurement error, the background cap engages
      on focus loss

## Subtasks

- [ ] 110.1 Present-mode selection at swap-chain creation and a runtime vsync
      toggle path -- lands inside T0025's device code, driven by this ticket's
      policy
- [ ] 110.2 Frame-rate cap. The wait strategy matters: a naive `sleep` has
      millisecond-scale jitter and produces pacing *worse* than none; expect a
      sleep-then-spin or OS-precision-timer approach, and measure it
- [ ] 110.3 Focus-loss handling: SDL3 window focus events (D16) feeding the
      chosen policy
- [ ] 110.4 Editor pacing -- decide and implement the editor's cap/vsync
      behaviour, including while a modal or background window is up
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
