# T0058 — Asset lifetime, reference counting and hot reload

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 3 — Data model |
| **Order** | 240 |
| **Created** | 2026-08-03 |

## Why

T0023 loads assets and explicitly defers everything about their lifetime. That is
fine until a project outgrows memory, or until someone edits a texture and wants
to see it without restarting — at which point retrofitting reference counting
through a system that assumed assets live forever is a rewrite.

Hot reload is also what makes the editor pleasant, and it pairs with the
hot-reloadable gameplay module (T0048) to make iteration genuinely fast.

## Done when

- [ ] Assets are reference counted; unused ones can be released
- [ ] Handles survive an asset being reloaded — GUID indirection, never raw pointers
- [ ] Editing a source asset on disk reloads it live in the editor
- [ ] Reload swaps contents without invalidating references held by components
- [ ] A failed reload keeps the previous version and reports the error
- [ ] Load is asynchronous, off the main thread, without blocking the frame

## Subtasks

- [ ] 58.1 Reference-counted asset handles resolving through the GUID map
- [ ] 58.2 Release policy: immediate, deferred, or explicit unload
- [ ] 58.3 File watching on source assets, debounced
- [ ] 58.4 Reload in place — swap contents behind the handle
- [ ] 58.5 Async load on the job system (T0026), with GPU upload marshalled
      back to the main thread (T0050)
- [ ] 58.6 Failure handling that preserves the old asset
- [ ] 58.7 Tests: reload while referenced, release while referenced, failed reload

## Notes / findings

**The GUID indirection T0023 already establishes is what makes this possible.**
Components reference assets by GUID and resolve through the pool, so reloading
means replacing the pool's entry — no component knows or cares. Had components
held pointers, this ticket would be impossible without touching all of them. Worth
protecting that invariant.

**GPU uploads cannot happen on a worker thread** — resource state transitions are
not thread-safe (T0050). So async load means: decode and parse on a worker, hand
the CPU-side data back, upload on the main thread. Design the pipeline in those
two stages from the start.

Debounce the file watcher: DCC tools and build steps write files in stages, and a
naive watcher fires on a half-written file and reloads garbage.

### Architecture review (2026-08-03)

Diligent already ships a helper for the upload half of 58.5:
`GPUUploadManager.h` in `DiligentCore/Graphics/GraphicsTools/interface/`.
Check what it provides before hand-rolling the worker-decode → main-thread
upload marshalling. (Note also T0026 has been moved to Phase 3 — this ticket's
job-system dependency was previously pointing one phase forward.)
