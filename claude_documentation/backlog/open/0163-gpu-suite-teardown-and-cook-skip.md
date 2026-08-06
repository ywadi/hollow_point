# T0163 — A fatal assertion in a gpu case takes the next case with it

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 4 — Render layer |
| **Order** | 468 |
| **Created** | 2026-08-07 |
| **Blocked by** | nothing |
| **Refs** | [../completed/0146-vertex-stage-hook.md](../completed/0146-vertex-stage-hook.md) — **found while measuring, pre-existing, and explicitly not that ticket's**; [../completed/0142-slang-shader-language.md](../completed/0142-slang-shader-language.md) 142.7 / **D34** — the cook this trips over; [../completed/0012-test-harness.md](../completed/0012-test-harness.md) — the bucket layout this changes nothing about |

## Why

**A `REQUIRE` that fires inside a gpu test case does not just fail that case — it
kills the next one.** Every gpu case brings up a `Window` and a `RenderLayer` and
tears them down *in the test body*, after the assertions. doctest's `REQUIRE`
aborts the case immediately, so the teardown never runs, the device and the VFS
stay mounted, and the following case dies with SIGSEGV.

That makes every `REQUIRE` in the gpu suite a landmine: **the first real failure
in a run reports as two failures, one of which is a crash in unrelated code.**
Anyone debugging it starts in the wrong file. This has already cost time once —
a T0157 session spent a while on a `~basic_registry` SIGSEGV that turned out to
be a lifetime ordering problem in the test, not the engine.

**Discovered 2026-08-07 while measuring T0146**, and verified against the
pre-T0146 build, so it is not a regression — it is a hazard that has been there
since the gpu bucket got its second case.

## The reproduction, measured

```
HP_SPIRV_CACHE=0 zig build test -Dtest=gpu
```

- `cooked_shaders_test.cpp:353` fails: `REQUIRE(hp::cookShaders(...))`, because
  the cook refuses with *"HP_SPIRV_CACHE=0 keeps no compiled bytecode, so there
  is nothing to seal"* — which is the environment correctly saying this run
  cannot cook;
- and then the **next** case segfaults, for the reason above.

## Done when

- [ ] A failing `REQUIRE` in a gpu case leaves the device and the VFS in a state
      the next case can use — **proven by a deliberately failing case**, not by
      reasoning about scope
- [ ] The cook cases **skip** rather than fail when the developer cache is off,
      the way every other gpu case already skips with no device
- [ ] `HP_SPIRV_CACHE=0 zig build test -Dtest=gpu` is green, and that command is
      written down somewhere a person will find it

## Subtasks

- [ ] 163.1 **Teardown by destructor, not by statement.** The `Device` struct
      each gpu file defines already owns the window and the render layer; give
      it a destructor (or a scope guard) so an aborted case still releases them.
      Roughly a dozen files share the same shape — the fix is one pattern
      applied, not one clever thing
- [ ] 163.2 **The cook cases skip when `HP_SPIRV_CACHE=0`**, with the same
      `MESSAGE(...); return;` shape the no-device skip uses. The cook's refusal
      message is already correct and should stay
- [ ] 163.3 **A test that proves it** — a case that deliberately fails a
      `REQUIRE` and a following case that must still run. If doctest cannot
      express that in one binary, a subprocess check in the integration bucket
      can, and that is where the harness tests already live
- [ ] 163.4 Note the cache-off invocation in `03-build-harness.md`, since it is
      the only way to time a cold compile and T0146 needed it

## Not in scope

- **Reworking the gpu bucket's fixture.** Each file constructing its own device
  is deliberate — the suite is meant to survive a case that wedges one. This
  ticket makes that true, it does not centralise it.
- **The cook's refusal itself**, which is correct behaviour and well worded.

## Notes / findings

### Why Simple rather than Medium

The diagnosis is done and pasted above, the fix is a destructor, and the
verification is a command that either exits zero or does not. What earns it a
ticket rather than a drive-by is 163.3: proving that an aborted case no longer
poisons its successor needs a test that *deliberately* fails, and that is a
slightly unusual thing to write.
