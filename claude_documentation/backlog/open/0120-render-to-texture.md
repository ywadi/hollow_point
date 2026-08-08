# T0120 — Camera render-to-texture

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 515 |
| **Created** | 2026-08-03 |
| **Refs** | T0028, T0033, T0036, T0045, T0046, T0060, T0063, T0081, T0093, T0094, T0100 |

## Why

There are two different things both called "render to a texture" in this
backlog, and only one has an owner.

**Owned already:** the *single* main scene camera rendering *once* into an
offscreen target sized to whatever displays it. T0028.4 renders to "an
offscreen target sized to the viewport", T0046 owns that target's lifetime
(it names it directly: "the offscreen colour target the editor viewport
displays"), and T0033 is the consumer. This path is real and sufficient for
the editor viewport — T0033 is not blocked on this ticket.

**Not owned anywhere:** an *arbitrary* camera, positioned wherever gameplay or
tooling wants it, rendering the scene into a texture that something *else*
then samples — a second, independent view, not the one shown this frame.
T0094's own "Why" section names the consumers by name — "minimaps, portal and
mirror views, security-camera monitors, custom post effects" — but its
Done-when and subtasks (94.1-94.10) only give gameplay a persistent texture
*resource*, a custom `IRenderLayer` insertion point, and read access to the
resources the *existing* frame already computed. Nothing re-invokes scene draw
submission (T0028) against a second camera and a second target. A custom
layer can read the main frame's depth or GBuffer; it cannot conjure a second
render of the world from a different position. Fog-of-war accumulation (T0094's
own worked example, 94.9) does not need this — it accumulates a 2D visibility
result into a texture, it does not re-render 3D geometry from a second
viewpoint. A security-camera monitor does.

Two places in the backlog already assumed this capability exists and found it
did not:

- T0036 (assets panel) defers mesh/texture thumbnails with: "a genuine time
  sink (mesh thumbnails need offscreen renders)... revisit once the render
  stack can render to arbitrary targets." No ticket was that revisit point.
- T0063 (editor picking) builds its own one-off offscreen render — a GPU
  entity-ID buffer — independently of T0028's viewport target, because there
  was no shared "render this camera to this texture" primitive to reuse.

Two bespoke one-off renders already exist (T0028's viewport path, T0063's ID
buffer) and a third and fourth are named as wanted (portals, mirrors, security
monitors, a live character portrait) without a shared mechanism. That is
exactly the "individually looks complete, gap between them" failure mode
`documentation/07-design-gaps.md` was written to catch, for the same reason
particle texturing and vsync were invisible until named.

**What gets expensive if this stays unowned:** T0094 ships as specced — the
persistent-texture and custom-layer plumbing works, its own worked example
(accumulating visibility) passes — while the very use cases its "Why" section
uses to motivate that work quietly remain undeliverable, discovered only when
someone tries to build a security-camera monitor and finds nothing renders
into it. T0036 and T0063 also stay as two divergent bespoke offscreen-render
implementations with no shared base, so a third consumer either forks a third
implementation or someone untangles three by then instead of building one now.

## Done when

- [ ] A camera (T0081) can be given a texture target — reusing T0094's
      `RenderTexture` resource — instead of, or in addition to, contributing
      to the composited swap-chain frame
- [ ] Rendering such a camera reuses the real pipeline — culling (T0045),
      scene draw submission (T0028), materials (T0060) — not a second,
      parallel rendering path
- [ ] An explicit per-camera update policy exists: every frame, on demand, or
      every N frames. The default is not "every render-texture camera, every
      frame, at full resolution" — see notes on cost
- [ ] Resolution and format are chosen per render-texture camera, independent
      of the swap chain and of T0046's frame-target declarations
- [ ] Recursion is bounded — a render-texture camera whose view contains a
      surface sampling another render-texture cannot recurse unboundedly; the
      limit is explicit and documented, not a stack overflow waiting to happen
- [ ] Whether a render-texture camera's render participates in culling and
      vision (T0045, T0093) is a **stated decision**, not an accident of
      implementation order (see notes — this is partly a gameplay question)
- [ ] Misuse fails loudly in debug: a target bound as both a camera's output
      and an input the same camera's view would sample (the naive
      infinite-mirror case) is caught, not silently corrupted
- [ ] GPU memory used by render-texture camera targets is reportable,
      alongside T0046's existing frame-target reporting (46.6)
- [ ] T0036's mesh/texture thumbnails and T0063's ID-buffer picking are
      identified in this ticket's notes as candidates to move onto this
      mechanism — migrating them is explicitly **not** required by this
      ticket's Done-when, only flagged

## Subtasks

- [ ] 120.1 Give T0081's camera an optional texture target, reusing T0094.1's
      `RenderTexture` type rather than a second texture-lifetime abstraction
- [ ] 120.2 Make scene draw submission (T0028) callable per-camera against an
      arbitrary target, instead of hardwired to one implicit camera and one
      viewport-sized target
- [ ] 120.3 Per-render-texture-camera culling (T0045) using *that* camera's
      frustum, not the primary viewer's
- [ ] 120.4 Update-policy field and scheduling: every frame / on-demand /
      every N frames, with a sensible per-camera default
- [ ] 120.5 Recursion guard, decided and recorded — e.g. render-texture
      surfaces do not themselves sample another render-texture, enforced by a
      depth counter in debug
- [ ] 120.6 Visibility/culling-participation decision: does a render-texture
      camera compute its own T0093 vision, inherit the primary viewer's, or
      ignore vision entirely. Record the decision; do not default silently
- [ ] 120.7 Ordering within the frame: render-texture camera renders happen
      before whatever samples them, placed as a named phase in T0100's frame
      anatomy rather than picked ad hoc
- [ ] 120.8 GPU memory/cost reporting for render-texture targets, alongside
      T0046.6
- [ ] 120.9 Worked example: a security-camera monitor mesh sampling a live
      render-texture camera as a material parameter (T0060, T0094.5)
- [ ] 120.10 Note, but do not perform, the T0036 and T0063 migrations

## Notes / findings

**Distinct from T0028/T0033/T0046's existing path.** That path is the single
main camera rendering once, into one target sized to whatever displays it —
already correct and not reopened here. This ticket is about *additional*,
independently-positioned cameras rendering into *their own* targets within the
same frame.

**Distinct from T0046's mid-frame readback amendments.** T0046's 2026-08-03
amendment (depth readable for soft particles T0106, scene colour readable for
a distortion pass T0107) is about reading the *current* frame's *own* targets
mid-pass — post-processing over one camera's output. This ticket is about a
*second, independent* render from a *different* camera. Different concern,
same document's neighbourhood; do not merge them.

**T0094 is a prerequisite, not a duplicate.** This ticket's target texture
should *be* a T0094.1 `RenderTexture`, and the worked example (120.9) binds it
as a material parameter via T0094.5/T0060. What T0094 does not provide is the
thing that fills the texture with an actual rendered view — that is this
ticket's entire content. Ordered after T0094 (510) for exactly that reason:
this reuses T0094's resource type rather than inventing a second one.

**Cost is the real danger of shipping this naively.** A render-texture camera
is a full extra scene submission — N such cameras is N× the per-frame render
cost unless something bounds it. 120.4's update policy exists because of this,
not as a nice-to-have. Once real numbers exist this belongs in T0031's budgets
(cross-reference, do not duplicate the profiling-workflow ticket here).

**Recursion is a classic RTT trap** — a mirror reflecting a mirror, or a
security monitor visible within its own camera's view, is a feedback loop, not
merely a performance concern. A hard depth limit (render-texture surfaces
cannot themselves sample another render-texture) is the cheap, defensible
default; recording it here means every consumer does not improvise its own.

**Visibility participation is deliberately left an open decision, not
resolved here.** T0093's own stance is that fog-of-war *policy* is not an
engine feature — the engine exposes the primitive, gameplay decides ("the
game builds the policy in an autoload", T0093's Decisions section). Whether a
security camera's view should be limited by *its own* vision cone, by the
player's fog-of-war state, or by neither, is exactly that kind of policy
question — genuinely gameplay, not rendering, even though the *machinery*
(a camera has a frustum; T0093's occlusion machinery can be pointed at any
camera) belongs here. 120.6 exists so this does not get decided by accident
the first time someone builds a security-camera puzzle and the camera turns
out to see through fog it should not, or vice versa.

**T0036 and T0063 are flagged, not migrated.** Both already built narrower,
independent versions of "render something offscreen" before this ticket
existed. Forcing their migration into this ticket's scope would couple an
editor-tooling change to a Phase-4 rendering ticket for no immediate benefit;
120.10 exists so the relationship is on record for whoever picks either of
those tickets up next, without expanding this ticket's Done-when to cover
editor code it does not need to touch to be complete.

### 2026-08-08 — considered for merge into T0094, and **declined** on this ticket's own evidence

The proposal was to fold this into [T0094](0094-gameplay-extensible-rendering.md)
on the grounds that T0094's `## Why` names *"portal and mirror views,
security-camera monitors"* — which is this ticket's content. The condition set
for the merge was: *"if camera render-to-texture has engine-side API beyond the
seam, it survives separately."*

**It does, and this ticket already argued it:**

> **T0094 is a prerequisite, not a duplicate.** … What T0094 does not provide is
> the thing that fills the texture with an actual rendered view — that is this
> ticket's entire content.

Concretely, the engine-side API beyond T0094's seam is **120.2** (make scene draw
submission callable per-camera against an arbitrary target, instead of hardwired
to one implicit camera and one viewport-sized target), plus per-camera culling
(120.3), the update policy (120.4), the recursion guard (120.5), the frame-anatomy
phase (120.7) and memory reporting (120.8). None of that is expressible as an
`IRenderLayer`.

**And it has two consumers outside gameplay extensibility entirely** — T0036's
mesh and texture thumbnails, and T0063's editor-picking ID buffer, both of which
already built narrower one-off offscreen renders because no shared primitive
existed. Merging this into a gameplay-seam ticket would leave those two with no
owner again.

**What the merge review did change**: the relationship is now stated on both
tickets rather than only on this one. T0094 owns the seam and the `RenderTexture`
resource; this ticket owns re-invoking submission against a second camera, and
reuses that resource type rather than inventing a second.
