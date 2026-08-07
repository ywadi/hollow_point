# Design gaps -- what no ticket says

Written 2026-08-03. A systematic hunt for *absences*: whole capabilities a real
3D game engine needs that no ticket owns, prompted by two finds earlier today --
particle texturing (T0080 simulated ten thousand correctly white squares until
T0106/T0107/T0108 were filed) and vsync (still unowned, item 1 below). Both were
invisible because each sat *between* tickets that individually looked complete.
That seam is what this document hunts.

**Method.** Every claimed absence is backed by a grep over `backlog/` and
`documentation/` (case-insensitive, run 2026-08-03), with hits read rather than
counted -- "IME" matches "time" in 93 files, "presentation" matches
"representation" in 7, and none of the seven is about frame presentation. A gap
is only listed if the term genuinely does not appear, or appears only in an
unrelated sense. Where a claim depends on a ticket's full text, that ticket was
read in full; the confidence section at the end says which were read and which
only grepped.

**Caveat -- the tree was being edited under this survey.** The decision log
gained D16 (SDL3) and T0015 grew notes *between* two reads during the hunt.
Claims here are against the tree as of these greps; anything marked in-flight
may already have moved. The T0015-says-NativeApp-while-D16-says-SDL3 mismatch
is known and mid-edit -- it is not re-raised as a finding.

Two classifications, because they need different fixes:

- **[absent]** -- the concept appears nowhere. Fix: decide whether it is wanted,
  and either file a ticket or record the rejection so the absence is a decision.
- **[mentioned, unowned]** -- a sentence exists somewhere, but no ticket's
  Done-when owns it. Fix: assign it to an owner; the thinking half is done.

Entries are ordered by **how expensive the gap gets if found late** -- a gap
that changes an interface everything else is built on outranks a bigger feature
that can be added whenever.

## Summary

| # | Gap | Class | Fell between | Gets expensive |
|---|---|---|---|---|
| 1 | Vsync, present modes, frame pacing, frame-rate cap, focus-loss policy | absent | T0025 / T0014 / T0100 / T0078 | first playable build |
| 2 | Anti-aliasing decision; render scale / upscaling | absent (TAA hook only) | T0096 / T0046 / T0101 | when T0046 formats freeze |
| 3 | Text rendering, string identity, localisation | mentioned, unowned (two bullets in placeholder T0069) | Phases 3-7 data model vs Phase 12 UI | every string authored before the decision |
| 4 | Graphics quality settings schema in T0078 | mentioned, unowned (4 tickets cite it) | T0086/T0089/T0091/T0096 vs T0078 | mildly, as ad-hoc per-system settings |
| 5 | Device loss / GPU failure at runtime | absent | T0025 / T0099 | first driver reset on a player's machine |
| 6 | Interior ambient: probes, lightmaps, any local IBL control | absent | T0087 / T0079 / T0096 | after lights are tuned per scene |
| 7 | Camera shake, blends, cinematics | absent | T0081 / gameplay | if retrofit needs a camera modifier hook |
| 8 | VFX trails/ribbons; screen distortion pass | absent / mentioned once (T0107) | T0080 / T0106 / T0046 | when particle buffer layout freezes |
| 9 | Screenshot capture capability | mentioned, unowned (T0083 assumes it) | T0083 / T0094 | Phase 8, small |
| 10 | Steam/platform: achievements, cloud saves, overlay | absent | nothing -- no owner at all | shipping time; two cheap constraints now |
| 11 | Accessibility: subtitles, colourblind, UI scale | absent (rebinding *is* covered) | T0052 / T0069 | late, then socially expensive |
| 12 | Runtime developer console / cvars | absent (T0066 is a log viewer) | T0066 / T0078 | never structurally; QoL loss daily |
| 13 | Editor content ops: asset rename/delete/fixup, find-usages, hierarchy search, scene autosave | absent | T0036 / T0035 / T0023 | in proportion to content volume |
| 14 | Ragdoll; morph targets | absent | T0049 / T0051; T0038 / T0041 | Phase 7/9 if wanted at all |
| 15 | Aspect-ratio / ultrawide policy | absent | T0081 / T0069 | cheap any time before HUD art |
| 16 | Memory budgets (GPU / asset) | absent (frame-time budgets are owned) | T0031 / T0023 / T0058 | only if content outgrows RAM quietly |

---

## 1. Presentation: vsync, present modes, frame pacing -- [absent]

The known find that prompted this hunt, confirmed and scoped. The terms
`vsync`, `v-sync`, `swap interval`, `MAILBOX`, `FIFO`, `tearing`,
`frame pacing`, `frame-rate cap`, `framerate` return **zero hits** across all
110 ticket files and all documentation. The only occurrence of "present mode"
in the repository is a log line pasted as evidence into completed T0003:

```
Diligent Engine: Info: Using VK_PRESENT_MODE_IMMEDIATE_KHR swap chain present mode
```

That line is worth staring at: it means the engine's *current observed
behaviour* is uncapped rendering with tearing -- Diligent's default, chosen by
nobody. This is not a hypothetical absence; the wrong value is already running.

Where it fell: T0025 owns the swap chain (creation, resize, shutdown -- read in
full, no present policy). T0014's loop is "poll → update layers → render →
present" with present as an unexamined step. T0100 owns "the frame's anatomy...
the single ordered list of what runs when" and never mentions presentation or
pacing -- the natural owner, blind to it. T0078's game options are "resolution,
volume, keybinds" with no display section. T0015's display-modes review note
(fullscreen/borderless/resolution/DPI) claims window-side display state but not
presentation.

What an owner must decide: present mode selection per backend (FIFO/MAILBOX/
IMMEDIATE on Vulkan, swap interval on GL -- per-backend code, like T0096's sRGB
handling); a frame-rate cap independent of vsync (menus and background
windows should not render at 3000 fps); what the *editor* does (an editor
burning a laptop battery at uncapped fps is a daily-life bug); and the
focus-loss policy -- "focus" appears in five files, all about input focus or
focus-on-selection, none about what a backgrounded game does (cap frame rate?
pause? mute? -- interacts with T0052 and T0057). Frame pacing also feeds T0057's
fixed-step accumulator and T0031's frame budgets: a budget against an unpaced
frame is noise.

When it bites: the first time anyone runs a playable build. Cheap to own now,
because the settings plumbing (T0078), the swap-chain code (T0025) and the loop
(T0014) are all still unwritten.

## 2. Anti-aliasing and render scale -- [absent as a decision]

`anti-alias`, `antialias`, `MSAA`, `FXAA`, `SMAA` -- **zero hits**. `upscal`,
`dynamic resolution`, `render scale`, `DLSS` (as a feature), `FSR` -- zero.
TAA exists only as a deferred hook: T0096.7 "leave the hook where Bloom/TAA
slot in later", T0101 "the TAA/motion-vector hook T0096 leaves open". So the
*mechanism* for one specific AA technique is half-anticipated, but the
*decision* -- does this engine ship aliased, and if not, how -- appears nowhere.

Two facts sharpen this. First, DiligentFX ships TAA as a component
(T0096 verified the list) and the build already produces
`SuperResolution_64r.dll` on every platform (recorded in
`05-verification-status.md`) -- an upscaler compiled ~1100 times and referenced
by zero tickets. Second, the choice is structural, which is why it is ranked
second: MSAA changes T0046's render-target formats and every pass that reads
depth (T0106's soft particles read scene depth -- per-sample under MSAA);
TAA needs motion vectors (T0101), jitter, and history buffers in T0046; and
either one has an ordering relationship with tonemapping that T0096 would own.
Deciding "no MSAA ever, TAA-shaped hook, evaluate FSR" costs a paragraph now.
Retrofitting MSAA after T0046's "formats are declared in one place" freezes
touches every declaration and several passes.

Render scale is nearly free and worth deciding with it: the editor already
renders the scene into an offscreen target at panel size (T0033), so the
runtime rendering at a scale below window size and blitting up is the same
machinery -- but only if someone says so before the world layer assumes it
renders at swap-chain size.

## 3. Text, string identity and localisation -- [mentioned, unowned]

`glyph`, `text render`, `freetype`, `stb_truetype`, `msdf` -- **zero hits**.
`localis`/`localiz` appears exactly twice, both inside T0069 (the Game UI
placeholder epic): once in the option table ("text layout, input focus,
scaling, localisation are each substantial") and once as a scope bullet --
"Font handling, and a decision on localisation". `subtitle`, `i18n`,
`string table` -- zero. Debug text exists (T0061's `DebugDraw::Text`) but is
compiled out of shipping builds by its own Done-when.

So all player-facing text -- and with it fonts, shaping, and every language
question -- lives as two bullets inside a Phase 12 epic whose library is
undecided. Two consequences:

- **The ordering is quietly wrong.** Phase 8 "ships a game" (T0042 runtime,
  T0043 export, T0083 saves with slot metadata implying a load menu) four
  phases before the system that can draw a menu exists. That may be intended --
  phases are engine-building order, not release order -- but nothing says so.
- **The expensive half is not fonts, it is string identity, and it cannot wait
  for Phase 12.** Phases 3-7 author components, prefabs and scenes. Every
  user-facing string that lands in that data as a literal ("Rusty Key",
  "Door locked") is a migration when localisation arrives; a string-table key
  costs nothing extra *if the convention exists when authoring starts*. That is
  a one-paragraph rule for `06-engine-conventions.md` or T0020/T0022, not a UI
  problem. Input rebinding (T0068) showing key names in UI, and save-slot
  metadata (T0083), hit the same question earlier than Phase 12.

Fix: keep the UI epic as-is, but pull "strings are keys, not literals -- or we
explicitly ship English-only" out of it into a convention now, and note fonts
as the reason the UI library choice cannot drift forever.

## 4. Graphics quality settings have four consumers and no producer -- [mentioned, unowned]

T0086.9 "Quality settings wired to project/user config (T0078)"; T0091.7
"Quality tiers driven by settings (T0078)"; T0089 "the fallback on lower
quality settings"; T0096.7 "behind quality settings (T0078)". Four rendering
tickets wire themselves into a quality section of T0078 -- and T0078 (read in
full) has no such section: its game-options row is "resolution, volume,
keybinds" and no subtask mentions graphics tiers, presets, or a quality axis.

Nothing here is hard; the risk is drift -- each rendering ticket inventing its
own quality enum, then a settings screen trying to unify four shapes. One
edit to T0078 (a display + quality section: the item 1 display options, a
small set of named tiers, and the per-system overrides policy) closes it.
Gamma/brightness (`gamma`, `brightness`: zero hits) belongs in the same
section.

*Update 2026-08-06: T0148 (post-process stack) is now a fifth consumer --
its 148.5 wires per-effect quality to the same unbuilt T0078 section rather
than inventing a shape of its own.*

## 5. Device loss -- [absent]

`device lost`, `device removed` -- **zero hits**. T0025 owns device lifetime
and covers creation, backend selection, resize and clean shutdown; nothing
anywhere covers the device dying mid-run. On Vulkan, `VK_ERROR_DEVICE_LOST`
happens on real machines -- driver updates, GPU hangs, laptops switching GPUs
-- and this engine plans to run its entire particle system in compute (D15),
which is the classic way to write an accidental GPU hang during development.

The minimum viable answer is one sentence -- "device loss is fatal: route it
through T0099's crash handler with a distinguishable message" -- and it is a
far better sentence than an unhandled error code propagating as a mystery
crash. Full recreate-and-continue is a large feature nobody should build
speculatively. The gap is that neither has been chosen. Falls between T0025
(owns the device) and T0099 (owns dying well).

## 6. Interiors under a global IBL: no probes, no lightmaps, no local ambient -- [absent]

`lightmap`, `light probe`, `reflection probe`, `global illumination`,
`GI` -- **zero hits** in every sense. The lighting stack is: punctual lights
with per-object selection (T0079, read in full -- directional/point/spot
only), shadows (T0086), and **one environment map per scene** with a global
ambient intensity (T0087, read in full: "the environment is authorable per
scene", "ambient intensity is controllable, so it can be tuned per scene").

The gap: a single scene-global IBL illuminates interiors with sky light.
A basement is lit by the same environment as the street above it. Every
engine with authored indoor/outdoor spaces grows *some* answer -- probe
volumes, lightmaps, per-volume ambient scale, or an authored "indoor" factor
-- and this backlog has none, not even a rejection. T0093's visibility
decision raises the stakes: "a dark room inside the vision cone is *visible
and dark* -- lit by whatever lights exist", so interior light levels are
gameplay-legible, not just cosmetic.

When it bites: T0096 already names the cost precisely, for colour-space bugs
-- "fixing it later means re-tuning every light and every material in every
scene". Ambient leakage is the same class: lights get tuned to compensate,
then un-compensated when a real mechanism lands. A cheap first answer (an
ambient/IBL-intensity *volume* or per-room scalar, applied where T0087's
inputs feed `PBR_Renderer`) should at least be dispositioned before scenes
are lit. A single all-outdoor or all-interior game would need only half of
this; the engine does not get to assume that, because the next game is the one
that needs the other half. T0087 owns both, and the live question is which is
built first -- not what any one game turns out to be.

## 7. Camera motion: shake, blends, cinematics -- [absent]

`camera shake`, `cutscene`, `cinematic`, `sequencer`, `spline`, camera
blending/transition -- **zero hits**. T0081 (read in full) resolves an active
camera per layer by priority, and "switching cameras from gameplay is one
call" -- an instant cut. There is no concept of a blend between cameras, a
procedural offset (shake, recoil, sway), or a scripted camera move.

Two different sizes hide in this:

- **Shake/offset needs an interface hook and nothing else.** If T0081's view
  matrix comes strictly from the entity transform, gameplay shake must
  physically move the camera entity -- workable but ugly (it pollutes the
  transform other systems read, e.g. the audio listener). A "post-resolve
  view offset" seam in 81.3 costs a line now and makes shake, recoil and
  blending gameplay problems, where D14/T0094 philosophy says they belong.
- **A sequencer/timeline is a subsystem** and should not be built
  speculatively. Whether the engine ships one is an engine capability decision
  needing its own ticket, raised when someone is ready to argue the cost -- not
  a property of any particular game. T0081 records the seam (81.9) that would
  receive it.

## 8. VFX shapes beyond quads, and the distortion pass -- [absent / mentioned once]

Today's T0106/T0107/T0108 closed sprites, flipbooks, soft particles, blend
modes, composed effects and decals. Two things are still outside:

- **Trails, ribbons, beams**: zero hits (`ribbon`, `beam`; every `trail` hit
  is "trailing underscore" or similar). T0080's rendering is exclusively
  "camera-facing quads" (80.4). For a shooter-shaped game (T0093's vision
  cones, T0098's agents) tracers, projectile trails and beam weapons are
  near-certain requests. A ribbon is not an emitter-parameter tweak -- it is a
  different topology (a strip through a particle's history) and touches D15's
  fixed GPU buffer layout. It does not need building now; it needs a line in
  T0080 saying the particle buffer and dispatch structure should not preclude
  strip-topology emitters, before 80.2 freezes that layout.
- **Screen distortion** appears exactly once, as a line item in T0107's
  anatomy of an explosion ("optionally screen distortion for the shockwave")
  -- and no ticket owns a distortion pass. It has the same shape as T0106's
  soft-particle finding: it needs *scene colour* readable during the
  transparent pass, exactly as soft particles need *depth* readable, and
  T0106 explicitly flagged that constraint into T0046 while distortion was
  not. One sentence in T0046 keeps the door open; discovering it after the
  frame layout solidifies is a refactor.

  *Update 2026-08-06: the scene-colour-during-transparents read now has an
  owner -- T0147 (engine intermediates), whose 147.2 places the snapshot in
  the frame; a distortion* pass *composes from T0147's read plus T0148's
  chain. The half of this item that remains open is ribbons/trails (T0080's
  buffer-layout constraint), unchanged.*

  *Update 2026-08-07: **the read landed** (T0147, D37). Scene colour and scene
  depth are copied between the opaque and blend passes at 10.9b and sampled by
  any material whose alpha mode is `Blend`, so screen distortion is a
  **material** today and needs no pass of its own -- a shockwave is a blended
  quad whose `baseColor` samples `HpSceneColour(In.ScreenUV + offset)`. What a
  distortion* pass *would still buy is applying it to the whole frame rather
  than to a surface, which is T0148's chain and is a different thing wanting
  the same word. The ribbons/trails half of this item is still open.*

## 9. Screenshot capture -- [mentioned, unowned]

T0083.3 requires save-slot metadata including a "screenshot". No ticket
provides screenshot capture: `screenshot` hits are T0083's metadata line, an
editor-evidence line in T0032, and verification prose. Adjacent machinery
exists -- T0094.6 "async readback to CPU" -- but capture-the-presented-frame,
encode-to-PNG and where-it-goes have no home. Also wanted, eventually, by
T0036 (thumbnails, explicitly deferred), bug reports and marketing. Small;
assign it (T0094 is the natural owner for the readback, T0083 for the use)
so it stops being assumed.

## 10. Platform and store integration -- [absent]

`achievement`, `cloud save`, `rich presence`, `overlay`, `Steamworks` --
**zero hits**. Steam appears twice: "Steam Audio" as an audio-library option
(T0052) and D14 rejecting "Steam-Workshop-style" UGC. Whether any game built on
this engine ships on Steam is not this repository's question to answer, and
deferring it is reasonable -- except that two constraints are cheap now and
annoying later:

- **Cloud saves constrain the write directory.** T0103.4 already creates a
  per-platform write directory for saves; cloud sync wants that directory
  stable, small and free of non-save junk (crash dumps land "beside save
  data" per T0099.3 -- fine, but the layout should be chosen knowing a sync
  root may wrap it). One note on T0103.4.
- **Achievements want a gameplay event source.** The message bus (T0075) is
  exactly that; a line saying "platform integration would subscribe here,
  keep gameplay ignorant of it" preserves the option for free.

Everything else (overlay quirks, store builds, DRM-free packaging) is
genuinely fine to leave until a platform decision exists. Which platforms the
studio targets *is* a real owner decision -- unlike "what the game is" -- but
it gates none of the above, because T0075's rule (platform integration
subscribes to gameplay events; gameplay stays ignorant of the platform) keeps
the option open for free.

## 11. Accessibility -- [absent, with one covered exception]

`accessib` (in the accessibility sense), `colorblind`/`colourblind`,
`subtitle`, UI scale as a user option -- **zero hits**. The one genuine
covered piece deserves saying: **input rebinding is fully owned** (T0068:
"bindings are serialized and user-rebindable", dead zones, and rumble now in
scope via D16), which is the single highest-value accessibility feature and
it is not missing.

The rest needs owners eventually, and two of them are cheap to note now
because their host systems are still unshaped: subtitles/captions belong in
T0052's scope bullets (the audio engine's event-driven playback is exactly
where captions hook -- retrofitting caption timing into an audio API that
never considered it is the expensive version), and UI scale (distinct from
DPI correctness, which T0069 has) plus colourblind-safe palette policy belong
in T0069's decision list. None of this blocks anything today; all of it is
cheaper as a bullet in an unbuilt system than as a patch to a built one.

## 12. Runtime developer console and cvars -- [absent]

T0066 (read in full) is a log *viewer* -- filter, search, counts. `console
command`, `cvar`, `cheat` -- zero hits. Nothing anywhere provides "type a
command, tweak a value, spawn a thing" at runtime -- in the editor or in a
dev build of the game. T0078.8 (command-line overrides) is the same idea at
process start only. Not structural -- it can be added any time -- but it is
the tool that makes every *other* system debuggable, and engines that lack
one grow five ad-hoc debug keybinds per system instead. Worth a Low ticket so
it is a decision rather than an accident.

## 13. Editor content operations at scale -- [absent]

Individually small, collectively the "editor feels hostile after 500 assets"
cluster; none appears anywhere:

- **Asset rename/move/delete with reference fixup.** T0036 imports and
  assigns; T0023 flags *externally* missing/moved sources. No ticket lets you
  rename or delete an asset *from the editor* and fix or report references.
- **Find usages / dependency view.** `find usages`, `asset dependen` -- zero.
  T0043 walks dependencies at export; the editor never surfaces them. "What
  uses this texture?" is unanswerable by design so far.
- **Hierarchy search/filter.** T0035 lists entities live; no search. Fine at
  50 entities, not at 2000. (Multi-select *is* owned -- T0064 demands a
  decision; duplicate is owned -- T0035.3b.)
- **Editor scene autosave/backup.** T0099's note names losing an unsaved
  scene as "the editor equivalent of losing a save" -- and no ticket owns
  preventing it.

None of these changes an interface; all get linearly more painful with
content volume. A single "editor content operations" ticket in Phase 6 would
hold them.

## 14. Ragdoll and morph targets -- [absent]

The animation stack is otherwise the best-covered area in the backlog (see
covered list), which makes the two silences notable:

- **Ragdoll**: zero hits. It lives on exactly the animation/physics seam the
  backlog treats most carefully -- root motion vs simulation is called out in
  T0049, T0051 *and* the README's Very Complex list -- yet death physics,
  the other big seam feature, is never named. T0051 is a placeholder epic and
  may absorb it; a line there ("ragdoll: yes/no, and powered or pure") would
  make the absence a decision.
- **Morph targets / blend shapes**: zero hits (`morph target`, `blend
  shape`, `blendshape`, `facial`). T0038's FBX→glTF subtasks convert meshes,
  skinning and animations -- morphs are not listed, ozz does not do them, and
  the skinned vertex layout being assigned in T0041's review note does not
  mention them. Morph targets touch importer, vertex format and animation
  runtime at once -- a three-place retrofit if added late. T0038 and T0049 own
  the decision and own it on that cost: the engine either offers shape keys or
  it does not, and no game can add them from gameplay code if the vertex
  format has no room.

(Blend-space *authoring* is deliberately excluded -- T0049: "deliberately a
C++ helper, not an authored graph" -- so it is not listed as a gap.)

## 15. Aspect ratio policy -- [absent]

`aspect ratio`, `letterbox`, `ultrawide`, `safe area` -- zero hits in any
runtime context. T0033 has three fit modes *for the editor viewport*; the
game window has no stated policy (free aspect? letterbox to a design ratio?
what does 21:9 see -- more world, which in a vision-cone stealth game is a
gameplay advantage?). The last point makes this a design question, not a
polish question, for this particular game. One line each in T0081 (projection
from viewport) and T0069 (HUD anchoring) once decided.

## 16. Memory budgets -- [absent, deliberately narrow]

Frame-*time* budgets are owned (T0031, read in full). Memory budgets are not:
`memory budget`, `texture streaming`, `residency` -- zero hits. What exists is
per-system: D15's fixed particle buffer, T0107.5's effect cap, T0046's
"report allocated target memory". There is no total, no per-category split,
and no eviction story beyond T0058.2's release policy. For a confined-scene
game on desktop this may never matter, which is why it is last. What makes it
worth writing down anyway is that streaming is a Phase-4-shaped retrofit if it
is ever wanted, and nothing today would tell you it had become necessary.
T0031's budget is the cheap instrument that turns "we outgrew RAM" into a
measured event; whether the asset system ever streams is then an engineering
call for T0023/T0058, made against a number instead of a guess.

---

## Checked and adequately covered -- do not re-raise

Each of these was hunted the same way and found owned. Listed so the next
sweep does not repeat this one.

- **Sprites/flipbooks/soft particles/blend modes; decals; composed effects**
  -- T0106/T0107/T0108, filed today; T0106 also caught and fixed the T0097
  atlas contradiction. The two prompting finds are closed except item 1.
- **Display-mode switching, resolution changes, DPI** -- claimed by T0015's
  second review note (scoped floor: borderless-fullscreen toggle + runtime
  resolution + basic DPI). Presentation (item 1) remains distinct and
  unowned.
- **Gamepad, rumble, relative mouse capture, clipboard** -- D16 (SDL3) plus
  T0068's amendments; rumble explicitly pulled into scope.
- **Frame anatomy and update order** -- T0100 owns exactly the "falls between
  tickets" class for the CPU frame (drain points, safe points, late update).
- **Pause, time scale, fixed timestep, delta clamp** -- T0057.
- **Shader compile caching and hitching** -- T0060 via Diligent's
  `RenderStateCache`/`BytecodeCache`.
- **GPU skinning draw path** -- assigned by T0041's review note (joint
  palette, skinned vertex layout, PSO variant), including the T0060 variant
  axis. **Skinned culling bounds** -- T0045's note.
- **Occlusion culling** -- deliberately out of scope in T0045 with an
  insertable interface, and T0093.7's per-object visibility provides this
  game's dominant cull anyway. A recorded deferral, not a gap.
- **Texture compression, mips, sRGB tagging, premultiplied alpha** -- T0097
  (+ T0106 amendment).
- **Sub-asset GUIDs, reserved builtin GUIDs, VFS-only reads** -- T0023's
  review notes; **packs/patching/DLC/write dir** -- D13/T0103/T0043.
- **Crash handling, symbolication, flush-on-fatal, upload privacy** -- T0099;
  99.5 explicitly decides crash uploads default to no. Broader
  telemetry/analytics is absent but reads as the same privacy stance; if that
  is the intent, one line in T0099 or the decision log makes it a decision.
- **Modding/scripting/UGC** -- decided out in D14; assets-only DLC in via
  D13. **Networking and determinism constraints** -- T0070 records the
  constraints honestly as a placeholder; D15/T0075 record what already
  forecloses replay-grade determinism.
- **Terrain, water, vegetation** -- absent, and now **unowned**: T0044 named
  them and was dropped, so no ticket carries them. Each is a large subsystem
  and wants its own ticket before anything is allowed to depend on it.
- **Character controller, root motion vs physics, Jolt/enkiTS job sharing**
  -- T0051 epic. **Steering/crowds** -- T0098 (DetourCrowd); perception
  deliberately gameplay-side. **Behaviour trees** -- deliberately unowned:
  gameplay-side logic per D14/T0073's state-machine stance.
- **Audio engine scope** -- T0052 epic covers buses, music streaming, 3D,
  editor mute, and the SDL3 backend note; the library choice is the recorded
  open question.
- **Loading-screen hooks** -- T0077 (drawing one is Phase 12's problem).
- **Endianness** -- decided at T0020.4. **Undo/redo** -- T0065.
  **Multi-select** -- T0064 forces a decision. **Entity duplicate** --
  T0035.3b. **Save atomicity/versioning/identity** -- T0083 with its
  second-pass notes, T0082.

## Confidence

Read in full: `README.md` (board), all six documentation files, and tickets
T0014, T0015, T0023, T0025, T0027, T0031, T0033 (partial), T0035 (partial),
T0036, T0042, T0043, T0044, T0045, T0046, T0051, T0052, T0057, T0061, T0066
(partial), T0068, T0069, T0070, T0078, T0079 (partial), T0080, T0081, T0083,
T0087, T0093, T0094 (partial), T0096, T0097, T0099, T0100, T0103, T0106,
T0107. The remaining ~55 tickets were covered by grep only; a claim above
about any of them rests on the grep, not a reading. Two specific hedges:

- T0049, T0059, T0075, T0085, T0108 were grepped heavily but not read
  end-to-end; items 7, 8 and 14 touch them and could be refined by a full
  read.
- The tree was being edited concurrently (D16 appeared mid-survey; T0015
  changed between reads). Items 1-16 were re-grepped after D16 landed and
  stand against it -- D16 brings display capability (fullscreen, DPI,
  monitors) but says nothing about present modes, vsync or pacing -- but any
  edit after these greps may have closed something listed here. Re-run the
  greps in this file before filing tickets from it.
