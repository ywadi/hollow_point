# T0017 — LayerStack (system layers)

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Phase** | 2 — Engine skeleton |
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

- [ ] `ILayer` with `OnAttach`, `OnDetach`, `OnUpdate`
- [ ] `LayerStack` push/pop with correct ordering and lifecycle calls
- [ ] `Application::Run` updates the stack each frame
- [ ] Layers are destroyed in the correct order relative to the render device
- [ ] Tests cover attach/detach ordering

## Subtasks

- [ ] 17.1 `ILayer` interface
- [ ] 17.2 `LayerStack` — push calls `OnAttach`; removal calls `OnDetach` first
- [ ] 17.3 Removal always starts from the top, so dependants detach before
      their dependencies
- [ ] 17.4 Wire into `Application`'s loop and shutdown
- [ ] 17.5 Tests for ordering and lifecycle

## Notes / findings

Decide update direction deliberately and write it down: Laura updates
top-to-bottom. Events (T0018) propagate top-down so the topmost layer gets first
refusal — which is what makes the editor able to swallow input before the game
sees it. Update order and event order need not match, but the choice should be
explicit.

Overlays (layers that must stay on top, like a debug UI) are usually handled by a
separate insert point in the stack. Worth deciding now whether we need that
distinction or a single ordered list suffices.
