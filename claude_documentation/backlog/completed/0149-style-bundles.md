# T0149 — Style bundles: the one-click looks

| | |
|---|---|
| **Status** | ❌ SUPERSEDED |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 542 |
| **Created** | 2026-08-06 |
| **Superseded** | 2026-08-08 by **[T0148](../open/0148-post-process-stack.md)** — *"The frame after the world"*. **Nothing was dropped**: the mapping is below, and this ticket's owner decisions, its Godot/Unreal findings and its rung-0 reasoning are preserved **verbatim** in T0148 under *Absorbed from T0149* |

## Why this was absorbed rather than left open

**This ticket predicted its own absorption, in its own subtask list**, and the
condition it named has been met:

> 149.1 … **Hold this until T0145 and T0148 are shaped — they may absorb it
> entirely**, and a bundle format written against primitives that do not exist
> yet is work done twice.

T0145 landed 2026-08-06. T0148 is now shaped, and it is the ticket that owns
almost every layer a style *names*: the post preset, the tonemap curve, the
exposure defaults. A style has **no machinery of its own** — this ticket's own
text says so, and calls that what makes it honest — so it is a preset list over
T0148's chain rather than a system beside it.

**The second reason is the pattern the T0171 sweep found.** This ticket was
split off by *implementation stage*: build the primitives, then compose them.
That is a sound split while the primitives are ours to build. Under **D40** they
are Diligent's, so "wire it" and "compose it" are one job, and separating them
produces two partial designs of the same preset format.

## Where everything went

| From here | Now |
|---|---|
| The bundle asset — fields, serialisation, reflection, **clone-on-use** (149.1) | **T0148.12**, with the owner's clone-on-use decision and its named cost carried |
| The toon style (149.2) | **T0148.14**, including the instruction to **re-author** rather than promote `lighting_stage_test.cpp`'s probe module |
| The realistic baseline / identity bundle (149.3) | **T0148.13** |
| Ultra-realistic (149.4) and **dark noir as a derived style** (149.5) | **T0148.15** |
| The inspector picker (149.6) | **T0148.16** |
| Measure the switch (149.7) | **T0148.17** |
| *"Switching cost is measured and bounded"* — the hard runtime constraint | **T0148**'s Done-when, and its cost still lands on **T0151** + T0141.3 |
| *"What a style may not change is written down"* | **T0148**'s Done-when, and its seam section |
| **A project- or scene-wide material override** — *"the cheapest large win here"* | **still unticketed**, and now recorded as such in a dedicated section of T0148 rather than buried in a table here. It is **not** in T0148's Done-when |
| The Godot/Unreal comparison, and *"neither ships a style system"* | preserved verbatim in T0148 |
| *"Why this is not a second renderer per style"*, and D24's revisit clause | preserved verbatim in T0148, and restated in its seam section |

## The finding worth reading before touching T0148.14

**Material switching alone is not sufficient.** Swapping a material changes the
*surface* — albedo, roughness, normals. It cannot change *how light is applied*,
and toon shading is fundamentally quantised `N·L`, which lives in the light
loop. That is exactly why toon in Unreal is painful (fixed shading models, so
people reach for post-process materials or engine-source edits) and why Godot
can do it (it has `light()`).

**T0145 is what makes a toon style possible at all** — rung 3 of D30's ladder,
`IHpMaterial.light()`. Cel is `R.NdotL = ceil(R.NdotL * bands) / bands`; ramp is
`R.Diffuse` from a declared texture with `R.NdotL = 1`. It landed, which is why
this ticket stopped being blocked and started being a subtask.
