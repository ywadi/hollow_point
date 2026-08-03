# T0017 — LayerStack (system layers)

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 2 — Engine skeleton |
| **Order** | 110 |
| **Created** | 2026-08-02 |

## Why

Without this, `Application` becomes a dumping ground for every subsystem and has
to be untangled later. More importantly, the LayerStack is the mechanism that
lets the **editor be just another layer** pushed onto the stack — so the engine
library never has to know an editor exists. That is what makes T0013's separation
hold in practice rather than only on paper.

**Naming:** this stacks *systems* (render, editor). The separate stack that
composites *visual output* (world → HUD → UI) is `RenderStack` in T0027. Two
different things; keep the names apart.

## Done when

- [x] `ILayer` with `OnAttach`, `OnDetach`, `OnUpdate`
- [x] `LayerStack` push/pop with correct ordering and lifecycle calls
- [x] `Application::Run` updates the stack each frame
- [x] Layers are destroyed in the correct order relative to the render device
- [x] Tests cover attach/detach ordering

## Subtasks

- [x] 17.1 `ILayer` interface
- [x] 17.2 `LayerStack` — push calls `OnAttach`; removal calls `OnDetach` first
- [x] 17.3 Removal always starts from the top, so dependants detach before
      their dependencies
- [x] 17.4 Wire into `Application`'s loop and shutdown
- [x] 17.5 Tests for ordering and lifecycle

## Notes / findings

Decide update direction deliberately and write it down: Laura updates
top-to-bottom. Events (T0018) propagate top-down so the topmost layer gets first
refusal — which is what makes the editor able to swallow input before the game
sees it. Update order and event order need not match, but the choice should be
explicit.

Overlays (layers that must stay on top, like a debug UI) are usually handled by a
separate insert point in the stack. Worth deciding now whether we need that
distinction or a single ordered list suffices.


### Amendment (2026-08-03) — instrument `OnUpdate` when you write it (from T0019)

T0019 built the profiling macro surface and instrumented T0014's frame loop, but
`LayerStack::OnUpdate` did not exist to instrument. That one item moved here
rather than holding T0019 open to add a single line to code that had not been
written.

When this class lands, put `HP_PROFILE_ZONE()` in `OnUpdate` and a named zone
per layer -- `HP_PROFILE_ZONE_NAMED(layer->name())` or equivalent. Per-layer
zones are the point: a frame that shows one `update` block tells you the update
was slow, and one that shows which layer was slow tells you what to do about it.

The macros are already available (`<hp/Profiling.hpp>`, no engine dependencies)
and compile to nothing by default, so this costs nothing until profiling is on.


## Findings

Built alongside T0018, because half of that ticket's subtasks needed a stack to
dispatch through and the two are one design in practice.

**Overlays are pinned above, and stay there when a normal layer is pushed
later.** Without it, pushing a gameplay layer after the editor UI silently puts
the world on top of the interface — a bug that is invisible until someone
clicks. One vector with an insert point rather than two vectors, so "dispatch
top-down across both" is not a special case at every call site.

**Layers are owned (`unique_ptr`), not borrowed.** The alternative makes
lifetime a question every caller has to answer correctly, and getting it wrong
means the stack walking into freed memory during shutdown.

**Teardown is top-down, mirroring dispatch.** A layer above may depend on one
below still existing while it tears down; never the reverse. `Application`
clears the stack *after* `onShutdown` and *before* the window goes, which is the
ordering T0025 will depend on — a layer holding a GPU resource must release it
while the device still exists.

**T0019's leftover subtask is done here**: `LayerStack::update` and `render`
carry `HP_PROFILE_ZONE`, and each layer gets its own named zone, so a capture
shows *which* layer was slow rather than that the update was.

## Evidence

Covered by `tests/integration/layer_event_test.cpp` — see T0018 for the run.
Ordering, overlays, attach/detach and pop are each asserted independently.
