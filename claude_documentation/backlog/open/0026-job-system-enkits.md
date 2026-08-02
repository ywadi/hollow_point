# T0026 — Job system on enkiTS

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 4 — Render layer |
| **Created** | 2026-08-02 |

## Why

enkiTS is vendored and builds for both targets but nothing calls it. It becomes
the engine's task scheduler — the thing that makes asset import, LOD generation
(T0039), scene traversal and cooking parallel instead of serial.

Introduced in Phase 4 rather than Phase 2 because a scheduler with no work to
schedule is speculative. By here there is real work.

## Done when

- [ ] A `JobSystem` wrapping `enki::TaskScheduler`, owned by `Application`
- [ ] Parallel-for over a range, and standalone tasks with completion waits
- [ ] Worker threads named, so they are identifiable in Tracy (T0029)
- [ ] Thread count configurable, defaulting to hardware concurrency minus one
- [ ] Tests covering completion, nesting and the empty-range edge case

## Subtasks

- [ ] 26.1 Wrapper API — do not leak `enki::` types into engine headers
- [ ] 26.2 Parallel-for and task-with-dependency primitives
- [ ] 26.3 Lifetime: start after logging, shut down before the render device
- [ ] 26.4 Name worker threads for the profiler
- [ ] 26.5 Tests, including that nesting a job inside a job does not deadlock

## Notes / findings

**GUID generation must already be thread-safe** (T0016) before anything creates
objects from workers — that dependency is real and easy to miss.

Decide early whether the render layer submits from worker threads or only from
the main thread. Diligent supports deferred contexts for multithreaded recording,
but it is a significant complexity step; single-threaded submission with
parallel *preparation* is the sane starting point.

Nested tasks deadlock trivially in naive schedulers. enkiTS handles this
properly, but the wrapper must not add a blocking wait that reintroduces it.
