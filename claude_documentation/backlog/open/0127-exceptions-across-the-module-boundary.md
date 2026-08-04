# T0127 — A typed exception cannot cross the module boundary on Linux

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Order** | 6 |
| **Created** | 2026-08-04 |
| **Found by** | T0053 research (Cling evaluation) |
| **Refs** | T0095, T0105, T0055, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D12 |

## Why

An exception thrown inside a gameplay module **cannot be caught by type** in the
engine or host on the Linux target. It is caught only by `catch (...)`. On the
Windows target the identical source works correctly.

Measured twice — once by the research that found it, once independently:

```
zig c++, Linux, throw std::runtime_error in a dlopen'd .so, catch in the host:
    catch (const std::runtime_error&)  -> not taken
    catch (const std::exception&)      -> not taken
    catch (...)                        -> TAKEN

control, throw and catch within one artifact (Linux):
    catch (const std::runtime_error&)  -> TAKEN

Windows target, same source via LoadLibrary:
    catch (const std::runtime_error&)  -> TAKEN
```

**The asymmetry is what makes this dangerous.** CI runs both targets, so a design
that assumes typed catch across the boundary passes on Windows and fails on
Linux — and the Linux failure is not a crash or a link error, it is an exception
silently arriving in the wrong handler.

### Likely mechanism, not yet proven

Zig links libc++/libc++abi/libunwind **statically into every artifact with hidden
visibility**. Verified: `libhp_engine.so` exports zero `std::`, zero `__cxa_*`
and zero `_Unwind_*` symbols, while carrying `__cxa_throw` as a *local* symbol.
So each artifact has its own C++ runtime and its own typeinfo objects.

libc++ selects its typeinfo comparison at build time
(`lib/libcxx/include/typeinfo` in the pinned toolchain): COFF gets deep string
comparison, ELF gets pointer comparison on the assumption that the linker merged
RTTI into one definition. With a private hidden libc++abi per artifact, nothing
merges — so on ELF the pointers differ and the type match fails, while COFF's
string comparison still succeeds. That explains the platform split exactly.

**Not proven, and the attempted fix did not work:** setting
`-D_LIBCPP_TYPEINFO_COMPARISON_IMPLEMENTATION=2` on both sides did **not** repair
it, because the comparison happens inside the prebuilt `libc++abi.a`. Treat the
mechanism as strongly consistent with the evidence and the remedy as unknown.

## Why it matters now

`engine/`, `apps/`, `samples/` and `tests/` are all built **with** exceptions
enabled — `-fno-exceptions` appears only on some `third_party` targets. So
nothing currently stops someone designing an error path around throwing across
the boundary, and T0055's error-handling policy does not address the case
because it was not known.

## Done when

- [ ] The engine conventions state plainly whether an exception may cross the
      module boundary, and the answer is enforced rather than advisory
- [ ] A boundary test asserts the chosen behaviour on **both** targets, so the
      platform asymmetry cannot regress unnoticed
- [ ] If typed catch is wanted, a working mechanism is found and proven; if it
      is not wanted, the alternative for module-to-engine error reporting is
      specified (error codes, a result type, or a logged-and-swallowed policy)
- [ ] T0055 is amended, since it is the ticket that owns error handling

## Subtasks

- [ ] 127.1 Decide the policy: exceptions do not cross, or they must
- [ ] 127.2 If they must — find a mechanism. Candidates, none evaluated:
      exporting RTTI explicitly with default visibility on boundary-crossing
      exception types; a shared libc++ if one can be produced; or catching and
      rethrowing at the boundary
- [ ] 127.3 Boundary test on both targets
- [ ] 127.4 Amend T0055 and `06-engine-conventions.md`
- [ ] 127.5 Check whether `std::error_code`/`expected`-shaped returns are already
      the house style, in which case 127.1 is close to already decided

## Notes / findings

**Found while evaluating something else entirely.** The Cling research prototyped
JIT'd code calling into the real `libhp_engine.so`, and the "no C++ runtime is
exported" observation that killed the JIT idea is the same fact that causes this.
One measurement, two conclusions.

**This is a D12 gap, not a D12 refutation.** Rich C++ still crosses the boundary
— T0095 measured RTTI, `dynamic_cast` and entt components working across it, and
those still work. What does not survive is *typed exception matching*, which
nobody tested because nobody thought to.

**Related but distinct from T0105.1.** That was static destructors dangling after
unload; this is typeinfo identity across artifacts. Same root cause — a private,
statically linked, hidden C++ runtime per artifact — surfacing differently.
