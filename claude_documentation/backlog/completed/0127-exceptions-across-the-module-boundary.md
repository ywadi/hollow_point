# T0127 — A typed exception cannot cross the module boundary on Linux

| | |
|---|---|
| **Status** | ✅ DONE |
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

- [x] The engine conventions state plainly whether an exception may cross the
      module boundary — **stated and tested; not fully enforced.** The behaviour
      is pinned by the suite on both targets. The `catch (...)` at module entry
      points remains a convention, because nothing today declares what a module
      entry point is; that is handed to T0048, which does. Ticked on "stated
      plainly", not on "enforced" — see "What is not done" 
- [x] A boundary test asserts the chosen behaviour on **both** targets, so the
      platform asymmetry cannot regress unnoticed — and in *either* direction
- [x] If typed catch is wanted, a working mechanism is found and proven; if it
      is not wanted, the alternative is specified — **both**: a mechanism was
      found and proven, and then deliberately not adopted, for a reason the
      ticket could not have predicted
- [x] T0055 is amended, since it is the ticket that owns error handling

## Subtasks

- [x] 127.1 Decide the policy — **exceptions do not cross**
- [x] 127.2 Mechanism — the first candidate is the one that works: an
      engine-owned type with **default visibility and an out-of-line key
      function**. Proven, and then rejected as the interface. A shared libc++
      does not exist (T0105.1 established zig ships none); rethrowing at the
      boundary cannot help, since the rethrown object carries the same typeinfo
- [x] 127.3 Boundary test on both targets
- [x] 127.4 Amend T0055 and `06-engine-conventions.md`
- [x] 127.5 Check whether `std::error_code`/`expected`-shaped returns are already
      the house style — they are, and `hp::Expected` does not exist; see the
      Correction section

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

## Correction (2026-08-04, found while backfilling references)

**"T0055's error-handling policy does not address the case because it was not
known" is wrong**, and it changes what this ticket has to do.

`06-engine-conventions.md` already carries the rule, in the Exceptions section:
engine code does not throw; "**throwing across the gameplay module boundary is
not safe to rely on**"; and rule 3, `catch (...)` at every module entry point.
T0095.5 folded exactly this into T0055 at the time. So 127.1 is largely
**pre-decided** — the policy exists and is the right one.

Two things are genuinely missing, and they are what this ticket is actually for:

- **The rule is advisory prose.** Nothing enforces the `catch (...)` at module
  entry points and nothing tests it. T0105's `hp_add_gameplay_module()` is the
  natural enforcement seam, since it already guarantees a module cannot be built
  without its finalizer; 127.3's test belongs in `module_boundary_test.cpp`,
  which T0105.5 owns.
- **The doc states a hedge where there is now a measurement.** "Not safe to rely
  on" reads as caution; the truth is that it is broken on Linux, works on
  Windows, and fails by silently taking the wrong handler. 127.4 should replace
  the hedge with the fact, because a hedge invites someone to test it and
  conclude it works — on Windows, it does.

**127.5 is answered, and the answer has a hole in it.** The house style *is*
result-shaped: `std::optional` for normal absence, `hp::Expected<T, Error>` for
recoverable failure, per the conventions doc's table. But `hp::Expected` **does
not exist** — no such type in `engine/`, and the doc routes it to T0056, which
is **closed** and never mentions it. So the shape is agreed, the type was never
built, and the ticket that owned it is finished. Whoever takes 127.1 should
expect to raise that as its own ticket rather than discover it late.

## Done (2026-08-04) — and the ticket's own premise was too pessimistic

### The finding is real, and it is narrower than "typed catch does not work"

Reproduced independently here, with the pinned toolchain and the engine's own
flags (`-fvisibility=hidden`, C++20), then confirmed inside the boundary suite:

| Thrown in a module, caught in the host | linux/ELF | windows/COFF |
|---|---|---|
| `std::runtime_error`, caught as `std::runtime_error` | **`catch (...)` only** | exact |
| engine-owned type, caught as that exact type | **exact** | exact |
| engine-owned type, caught via a `std::` base | **`catch (...)` only** | base matches |
| control — thrown and caught inside one artifact | exact | exact |

**Row 2 is new, and it is the answer to 127.2.** A typed catch *does* survive
the boundary on ELF, for a type with default visibility whose **key function is
defined out of line in the engine library**. That pins vtable and typeinfo to
one artifact; everyone else carries an undefined reference to them, so ELF's
pointer comparison compares one object with itself.

Measured at symbol level rather than argued:

```
_ZTI15HpAbiEngineError     DEFINED   in the engine fixture
                           UNDEFINED in the module fixture      -> one object

_ZTISt13runtime_error      DEFINED locally in the module
                           DEFINED locally in the test executable
                           absent from the engine fixture       -> two objects
```

That is the whole mechanism. It is not about unwinding, and not about the
boundary as such — it is about how many definitions of a typeinfo object exist.

### Why the working mechanism is not the recommendation

**Row 3.** The first thing anyone does with an exception type is derive it from
`std::exception`, and `catch (const std::exception&)` — what people actually
write — cannot see an engine-owned exception on ELF. The derived-to-base walk
compares the *base* typeinfo, and `std::exception`'s typeinfo is private per
artifact again.

So the shape that works is the shape nobody reaches for, and the shape everyone
reaches for fails on exactly one target. In the scratch reproduction the
compiler warned that the exact-type handler was **unreachable** ("will be caught
by earlier handler") — and on Linux the unreachable handler is the one that
fired. The diagnostic was right about the language and wrong about the platform.

Building an error interface on a mechanism with that failure mode buys a little
convenience for a class of bug that is invisible on one target and silent on the
other. **Policy: an exception must not cross the module boundary.** The
mechanism is documented so nobody re-derives it, and marked as not to be used.

### What landed

- `06-engine-conventions.md` — the "not safe to rely on" hedge is now a stated
  fact, plus a new *Exceptions and the module boundary* section carrying the
  table, the symbol-level cause, and the enforced/not-enforced split. A hedge
  invites someone to test it and conclude it works; on Windows, it does.
- `tests/fixtures/abi_boundary.h` / `abi_engine.cpp` / `abi_module.cpp` — an
  engine-owned exception type with an out-of-line key function, and three throws
  through the module. Deliberately **not** derived from `std::exception`, so the
  fixture cannot demonstrate the trap by accident.
- `tests/integration/module_boundary_test.cpp` — three cases, both targets.
- T0055 amended; T0048 given the enforcement obligation, since it is the ticket
  that defines what a module entry point is.

### The test asserts in both directions, deliberately

The ELF case asserts the failure *keeps happening*. If it ever starts matching
by type — a toolchain bump, a shared libc++ — the suite fails and says so:

```
[linux/ELF]     std::runtime_error thrown in the module was caught by:
                catch (...) only -- typed match failed
[windows/COFF]  std::runtime_error thrown in the module was caught by:
                the exact type
```

Proven to be a real check by inverting the Linux expectation: the Linux binary
failed alone (`44 passed | 1 failed`) while the Windows binary stayed green at
45 — which also demonstrates the two targets are asserted independently rather
than one masking the other.

Both suites carry the target name in their diagnostics. Without it the two lines
above are genuinely ambiguous in a shared build log, and reading them the wrong
way round inverts the conclusion — which happened once while writing this.

### What is not done

- **The `catch (...)` at module entry points is not enforced**, only written
  down and reasoned about. Nothing today declares what a module entry point is —
  they are hand-written `extern "C"` functions — so there is no seam to put a
  guard in. Handed to **T0048** with a cross-reference on that ticket, because
  retrofitting a guard after entry points exist means touching every module ever
  written. Done-when 1 is ticked on "states plainly", not on "enforced".
- **The mechanism was not tested against a module carrying engine statics**, the
  same limitation T0105.1 records. The fixture is minimal by design.
- **`hp::Expected` still does not exist** (see the Correction above). The house
  style for module-to-engine error reporting is agreed in shape and has no type
  behind it, and T0056, which the conventions doc points at, is closed. That is
  a real gap this ticket surfaced and did not close; it needs its own ticket
  before anything designs against `hp::Expected`.
