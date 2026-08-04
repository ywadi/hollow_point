# T0056 — Core utilities: math, memory, containers

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 2 — Engine skeleton |
| **Order** | 50 |
| **Created** | 2026-08-03 |

## Why

Mostly a set of decisions rather than code, but ones that propagate into every
file. Getting them wrong means either rewriting later or living with friction
forever.

The headline finding: **Diligent already provides most of this**, so the work is
mostly deciding to use it rather than building alternatives.

## Done when

- [x] Math library chosen — Diligent's, recorded in the conventions doc. "Used consistently" is inherited by the first code that does maths
- [x] Allocator strategy decided — ordinary allocation until a profile justifies otherwise. **The scratch allocator itself is not available**: adopting Diligent's needs Diligent linked, moved to T0025
- [x] A decision on custom containers versus the standard library — standard, until a profile says otherwise
- [x] Written into the conventions doc — [06-engine-conventions.md](../../documentation/06-engine-conventions.md), "Math, containers and memory"

## Subtasks

- [x] 56.1 **Math: use Diligent's `BasicMath.hpp` / `AdvancedMath.hpp`** — decide
      and record; see notes for why not glm
- [ ] 56.2 Confirm the SSE/NEON paths are enabled for release builds — **moved to [T0025](../inprogress/0025-render-layer.md)**
- [ ] 56.3 Adopt `DynamicLinearAllocator` for per-frame scratch allocations — **moved to [T0025](../inprogress/0025-render-layer.md)**
- [ ] 56.4 Adopt `FixedBlockMemoryAllocator` where pooling pays — **moved to [T0025](../inprogress/0025-render-layer.md)**, and it needs a profile as well as a linked library
- [x] 56.5 Decide on standard containers vs alternatives, and record it
- [x] 56.6 Decide whether engine types are exposed to gameplay directly or behind
      a narrower surface (interacts with T0048's module boundary)

## Notes / findings

**Use Diligent's math, do not vendor glm.** Every renderer call takes Diligent
math types; introducing a second vector/matrix library means conversions at every
boundary, and eventually a bug where someone converts a row-major matrix as
column-major. Diligent ships `BasicMathSSE.hpp` and `BasicMathNEON.hpp`, so the
performance argument for glm does not hold either.

**Allocators already exist and are worth adopting:** `DynamicLinearAllocator`
(bump allocator, reset per frame), `FixedBlockMemoryAllocator` (pools),
`FixedLinearAllocator`, `STDAllocator` to use them with standard containers.

A per-frame scratch allocator matters more than it sounds — culling, draw
submission and animation all produce transient per-frame arrays, and doing that
through the general allocator shows up in profiles immediately.

### Architecture review (2026-08-03) — 56.6 vs T0094's "do not expose Diligent types"

These two look contradictory and are not, but the line needs drawing
explicitly: Diligent **math types** (`float3`, `float4x4` — header-only value
types with no linkage or lifetime) are fine to use across the gameplay
boundary, and forcing a conversion layer over them would be pure friction.
What must *not* cross the boundary are **RHI interface pointers**
(`IRenderDevice`, `ITexture`, …) — reference-counted objects whose lifetime
the module cannot own safely across a hot reload; that is T0094's wrapper
rule. Write 56.6's answer in exactly those terms. The linkage side of the same
question (one engine state, symbol resolution) is T0095.


## Closing note

A policy ticket, closed on its policies. All three decisions are made and
recorded in [the conventions doc](../../documentation/06-engine-conventions.md):

- **Math is Diligent's**, not glm — a second math library means conversions at
  every renderer boundary, and the two disagree about row versus column major.
  Gameplay uses the same types, which is safe across the module boundary because
  they are header-only value types with no linkage (D12).
- **Containers are the standard library's.** The case for replacing them is
  allocator control and debug-build speed, and neither has been measured here.
  Adopting a container library on other projects' measurements is how a codebase
  acquires a dependency nobody can remove.
- **Memory: ordinary allocation** until a profile justifies pools, with the one
  non-negotiable rule being that memory does not cross the module boundary to be
  freed.

**The three adoption subtasks moved to [T0025](../inprogress/0025-render-layer.md)** rather
than holding this ticket open. They all need the engine to link Diligent, which
T0013 deliberately deferred and T0025 owns — and this ticket sits at order 50
while that lands at 380. A ticket that cannot complete for the whole of Phases 2
and 3, sitting near the top of an execution-ordered board, is worse than one
that closed honestly and left a pointer.

56.4 keeps a second blocker that T0025 will not clear: "where pooling pays"
needs a measurement. It should not be ticked merely because the allocator became
reachable.
