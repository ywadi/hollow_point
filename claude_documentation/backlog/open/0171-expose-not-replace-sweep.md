# T0171 — Expose, do not replace: sweep the backlog against what DiligentEngine already ships

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 458 |
| **Created** | 2026-08-08 |
| **Blocked by** | [0170-diligent-owns-the-render-loop.md](../inprogress/0170-diligent-owns-the-render-loop.md) — **hard, and not a preference.** Every verdict below changes once the engine is a `GLTF_PBR_Renderer` subclass: today "adopt their shadows" is a port, afterwards it is a settings field. Sweeping first would produce a table that is wrong by the time it is read |
| **Refs** | [../../documentation/12-vendored-capabilities.md](../../documentation/12-vendored-capabilities.md) — **the document this makes binding**; [../completed/0168-asset-import-coverage.md](../completed/0168-asset-import-coverage.md) — the worked precedent: the same sweep for the import path ended that class of surprise in one afternoon; [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md), [../completed/0142-slang-shader-language.md](../completed/0142-slang-shader-language.md), [../completed/0145-lighting-stage-own-the-light-loop.md](../completed/0145-lighting-stage-own-the-light-loop.md) — **the attachment pattern this generalises**; [0045-culling-and-render-queues.md](0045-culling-and-render-queues.md), [0086-shadows.md](0086-shadows.md), [0088-sky-atmosphere-time-of-day.md](0088-sky-atmosphere-time-of-day.md), [0091-volumetric-fog.md](0091-volumetric-fog.md), [0096-hdr-pipeline-and-tonemapping.md](0096-hdr-pipeline-and-tonemapping.md), [0148-post-process-stack.md](0148-post-process-stack.md), [0155-terrain-rendering.md](0155-terrain-rendering.md), [0047-evaluate-render-graph.md](0047-evaluate-render-graph.md) — the tickets this judges; **D26** (as amended), **D30**, **D35** |

## Why

**The owner's instruction, and it is the whole ticket:**

> *"I don't want us doing all those crappy tasks in our backlog that are covered by Diligent. We only need the shaders to be exposed to the game devs and abilities we clearly stated to game devs."*
>
> *"**EXPOSE** capabilities to game devs, not **replace**… we already did that with Slang, and we can do that for others in the same method of attachment and not rewriting."*

**`CLAUDE.md` already names most of what is vendored** — *"Tone mapping, bloom, DoF, SSAO, SSR, TAA, atmospheric scattering, four shadow-filter modes and a whole terrain implementation are already vendored. The check has failed twice here."* It has now failed **six** times, and the backlog still contains tickets to build several of those from scratch.

## The principle, stated once so every verdict can be checked against it

**Attachment, not rewriting.** The pattern already exists and it is `IHpMaterial`: the engine declares an interface whose **default implementation *is* the standard path**, and a game overrides only what it wants. Nothing is reimplemented; a seam is opened. T0142 did it for Slang, T0145 did it for the light loop, T0159/T0160/T0161 did it for the material contract.

**Every capability below gets the same treatment.** Diligent's shadows, IBL, tone mapping, bloom, SSAO, atmosphere and terrain are the implementation; our job is the **seam** a game reaches them through, and nothing else. A ticket that proposes to *build* one of these is wrong by construction after this sweep.

**And the keep-list is short by design:** what a game developer needs to reach the shader, plus the abilities explicitly promised them. Everything else Diligent does and we expose.

## Done when

- [ ] **Every open render-layer ticket has exactly one verdict**: ❌ **closed as vendored** / 🔌 **rescoped to expose** / ✅ **genuinely ours**, with the upstream symbol or file named where it is one of the first two
- [ ] A **table** exists that a person can read to answer *"is this already in Diligent?"* — the shape `14-asset-import-matrix.md` proved
- [ ] Every ticket that is rescoped says **what its seam is**, in `IHpMaterial`'s terms — what a game overrides, and what the default is
- [ ] **`12-vendored-capabilities.md` becomes binding** rather than advisory, and `CLAUDE.md` points at the table
- [ ] The **Current ticket sequence contains nothing Diligent already does**

## Subtasks

- [ ] 171.1 **Sweep by reading, not by building.** Walk `12-vendored-capabilities.md` and every open ticket in phase 4 against the actual vendored source. **This is an afternoon** — T0168 did the same for import and closed four gaps in one pass
- [ ] 171.2 **Verdict the known candidates**, each with the upstream symbol: **T0086** shadows (`ShadowMapManager`, four filter modes), **T0087** IBL — *already folded into T0170*, **T0096** tone mapping, **T0148** bloom/DoF/SSAO/SSR/TAA, **T0088** atmospheric scattering, **T0091** volumetric fog, **T0155** terrain, **T0045** culling and OIT, **T0047** render graph
- [ ] 171.3 **Design the seam once, not nine times.** These are all post-`Render()` or pass-level features; if each invents its own hook the result is nine mechanisms. **One attachment pattern, `IHpMaterial`-shaped**, and every rescoped ticket uses it
- [ ] 171.4 **Close what should be closed**, moving anything genuinely ours out of the closed ticket and onto the one that keeps it. `CLAUDE.md`'s rule applies: close on what was achieved, move the remainder, and make the references point both ways
- [ ] 171.5 **Make the check unavoidable.** A row in `CLAUDE.md`'s table, and **D35 extended a third time** — a *render feature* gets a matrix row before it is built, exactly as a shader technique and a format feature already must
- [ ] 171.6 **Record what Diligent does *not* have**, with the same care. `TODO: depth sorting` and the missing `EXT_meshopt_compression` are as valuable as the positives — they are what stops the next person re-running the search

## Not in scope

- **Doing the rescoped work.** This ticket produces verdicts and seams; the tickets it rescopes do the work.
- **The non-render backlog.** Scene, ECS, VFS, modules, editor, import, gameplay — Diligent has no opinion on any of it.
- **Reopening T0170's decision.** This sweep assumes the engine is a subclass; that is why it is blocked on it.

## Notes / findings

### Why this is blocked on T0170 rather than running now

Every verdict depends on it. As a reimplementation of Diligent's loop, adopting their shadows means porting a subsystem into a foreign walk. As a subclass, it is a settings field and a seam. **The same ticket gets a different verdict either side of T0170**, so sweeping first produces a table that is wrong before anyone reads it.

### The counter-risk, named so the sweep does not overcorrect

**"Diligent has it" is not automatically "we do not need the ticket."** Their implementation may be wrong for us — the wrong quality bar, the wrong extension shape, or fighting a recorded decision. T0155's own title already says it: *"their reference implementation is the floor, not the ceiling."* So the verdict ✅ **genuinely ours** must stay reachable, and where it is chosen the reason gets written down. **The failure this ticket corrects is reimplementing by default; replacing it with adopting by default would be the same mistake facing the other way.**
