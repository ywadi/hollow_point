# T0056 — Core utilities: math, memory, containers

| | |
|---|---|
| **Status** | 🔜 TODO |
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

- [ ] Math library chosen and used consistently across engine and gameplay
- [ ] Allocator strategy decided, with a per-frame scratch allocator available
- [ ] A decision on custom containers versus the standard library
- [ ] Written into the conventions doc (T0055)

## Subtasks

- [ ] 56.1 **Math: use Diligent's `BasicMath.hpp` / `AdvancedMath.hpp`** — decide
      and record; see notes for why not glm
- [ ] 56.2 Confirm the SSE/NEON paths are enabled for release builds
- [ ] 56.3 Adopt `DynamicLinearAllocator` for per-frame scratch allocations
- [ ] 56.4 Adopt `FixedBlockMemoryAllocator` where pooling pays
- [ ] 56.5 Decide on standard containers vs alternatives, and record it
- [ ] 56.6 Decide whether engine types are exposed to gameplay directly or behind
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
