# T0118 — Generated API reference for coding agents

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Order** | 45 |
| **Created** | 2026-08-03 |
| **Refs** | T0013, T0055, T0104, T0109, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D5 D12, [../../documentation/06-engine-conventions.md](../../documentation/06-engine-conventions.md) |

## Why

**The audience for this ticket is not a human, and the deliverable is not a documentation website.** The requirement, from the project owner directly: generate the API reference **in markdown** so that a Claude Code agent "can use it to generate game code properly... so Claude Code has access to APIs easily." HollowPoint is an engine for several games (D12), and gameplay is C++ compiled against the engine's real headers in lockstep — not a script, not a stable C ABI. That makes `engine/include/hp/` the API surface a coding agent writes gameplay against, in exactly the same sense it is the API surface a human developer would.

**Verified absent.** `doxygen`, `doxyfile`, `api doc`, `api reference`, `sphinx`, `clang-doc`, `javadoc` return **zero hits** across every ticket and every documentation file.

Measured, not assumed:

```
$ for f in engine/include/hp/*.hpp; do echo "$f: $(grep -c '^\s*///' "$f")"; done
engine/include/hp/Api.hpp: 0
engine/include/hp/Application.hpp: 25
engine/include/hp/Engine.hpp: 13
engine/include/hp/EntryPoint.hpp: 0
engine/include/hp/Guid.hpp: 17
engine/include/hp/Log.hpp: 42
engine/include/hp/Profiling.hpp: 9
engine/include/hp/Time.hpp: 36

$ grep -rn -E "@param|@returns|@throws" engine/include/hp/*.hpp
(no output)
```

**142** `///` doc-comment lines across the **8** public headers in `engine/include/hp/`, and **0** `@param`, `@returns` or `@throws` tags anywhere in them. `///` is Doxygen-compatible and the comments sit on the right declarations, so a generator would find real prose — but generated output today would be descriptions with no parameter or return documentation, which for an agent generating a call site is close to useless: it says what a function is *for*, not what to pass it or what it hands back.

**Staleness here is a correctness bug, not a documentation bug.** For a human reader, a stale doc comment is annoying — they read the header anyway when in doubt. An agent generating code from a reference has no equivalent instinct: if the markdown says a function takes `(int count)` and the header has since changed to `(int count, LogLevel level)`, the agent confidently emits a call that does not compile, or one that compiles against the wrong overload and means something else. **The reference must therefore be *generated*, never hand-maintained**, and kept in lockstep with the headers the same way T0104 keeps a gameplay module in lockstep with the engine — the same class of problem, a derived artefact silently drifting from its source, with a much cheaper fix available up front than after the fact.

**The reference should be produced by the build, not merely checked by CI — the owner's own framing: "maybe needs to be generated with the build?"** A CI check that parses headers and compares against committed markdown catches staleness *after* someone pushes it. A build step means the reference **cannot** go stale, because producing it is part of producing the engine. `build.zig` already orchestrates `configure`, `dist`, `test`, and per-target steps (`linux`, `windows`) — see `documentation/03-build-harness.md` — so a `zig build docs` step is the obvious continuation of an established pattern, not a new kind of thing this project does.

## What "generated markdown, for an agent" changes versus a documentation site

- **Completeness beats polish.** A missing symbol is worse than a tersely documented one — an agent finding no entry for a function does not conclude "undocumented, proceed carefully"; it infers the function does not exist and either invents a substitute or gives up on a path that was actually available. Coverage of the public surface, not prose quality, is the acceptance bar.
- **Structure for a context window, not for hyperlink browsing.** A human browses a generated site by following links; an agent reads some slice of it into a limited context window. Whether the output is one file per header/subsystem or a single reference file, and how much prose accompanies each symbol (full doc-comment prose, or signature plus a one-line description), changes how usable it is under that constraint. This is an open decision, not something to default on.
- **Convention rules belong inline, next to the API they constrain, not in a separate document an agent may not read.** `06-engine-conventions.md` states real constraints a caller must not violate: engine code does not throw and exceptions never cross a module boundary; memory is never freed on the side of the module boundary that did not allocate it; `entt::type_index` must never be persisted or compared across the boundary (use `entt::type_hash` or a stable name); `HP_ASSERT` compiles out entirely in release and must not gate anything with side effects. A human learns these once from the conventions doc and carries them around; an agent generating one call site at a time benefits far more from the constraint sitting next to the function it applies to. How that gets into the generated output — a recognised comment tag the generator surfaces specially, a structured section per header, or something else — is a decision to make, not assumed away.

## Tooling options — not picked here

| Option | Notes |
|---|---|
| **`clang-doc`** | Part of LLVM's `clang-tools-extra`, with a Markdown backend built for exactly this. **Unverified**: whether it ships as an invocable binary from this project's pinned toolchain is not confirmed. Zig 0.16.0 (D5) bundles `clang` internals for `zig cc`, but that is not the same thing as the separate `clang-tools-extra` binaries; check before relying on it |
| **Doxygen → XML → a small script** | Doxygen's own HTML/LaTeX output is not the target, but its XML output is a stable, documented intermediate that a short script can turn into whatever markdown shape 118.2 decides on. Adds Doxygen as a host-only dependency |
| **A purpose-built `libclang` tool** | Most control over exactly what gets extracted and how tags are surfaced. Not a new idea in this codebase: `third_party/DiligentEngine/DiligentTools/RenderStateNotation/CMakeLists.txt` already `pip install`s `libclang==16.0.6` as part of its own CMake configure step (verified) — proof a libclang-based host tool already works in this build, and a concrete existing pattern (pip-install-at-configure-time) to either reuse or deliberately avoid |

Whichever is chosen **runs as a host tool only** — it never cross-compiles, mirroring T0038's FBX converter. The reference is target-independent (the same `engine/include/hp/` headers regardless of Windows or Linux target), so it must be generated exactly once per build, not once per target.

## Done when

- [ ] Every declaration in `engine/include/hp/` — and only that directory; `engine/src/` is not public surface — appears in the generated reference, with signature and doc-comment prose, checkable as a coverage count against the header count
- [ ] `///` comments on public API gain `@param`/`@returns`/`@throws`-shaped tags where a function takes parameters or returns something non-obvious, so the generator has structured data to extract — added as tag discipline on top of the existing prose, not a replacement for it (see the tension below)
- [ ] The context-window structure decision is made and recorded: one file per header/subsystem versus a single reference file, and the prose-density question (full doc comment versus signature-plus-one-line)
- [ ] The convention-inlining decision is made and recorded: how constraints from `06-engine-conventions.md` (module-boundary rules, exception policy, `HP_ASSERT` behaviour) reach the generated output next to the symbols they constrain
- [ ] `zig build docs` (or the chosen step name) produces the reference as part of the build, per the build-step-over-CI-check reasoning above; whether it runs on every ordinary build or on demand is decided and recorded, weighing generation cost on every build against the risk of it not being run
- [ ] The commit-vs-gitignore decision is made and recorded (see below), not defaulted by whichever happens to be convenient at implementation time
- [ ] If committed: CI fails on a dirty working tree after running the docs step — the simple gate the build-step design enables, instead of a second header-parsing pass to detect drift
- [ ] Verified against a real change: adding a parameter to a public function and running the build step changes the generated output; forgetting to regenerate (if committed) fails CI

## Subtasks

- [ ] 118.1 Decide the tooling (see the options table) and record the rejected alternatives' costs, matching the decision-log's own style
- [ ] 118.2 Decide the context-window structure: file granularity and prose density
- [ ] 118.3 Add `@param`/`@returns`/`@throws` tag discipline to the 8 existing public headers as a worked example, and write the expectation into `06-engine-conventions.md` (or this ticket's own note, if conventions is judged the wrong home) so new public API is tagged as it is written — the same "land the rule before the surface grows" reasoning as T0019's profiling macros
- [ ] 118.4 Decide how module-boundary and other cross-cutting conventions surface inline in the generated output
- [ ] 118.5 Wire the chosen tool into `build.zig` as a host-only step (`zig build docs`), following the existing `configure`/`dist`/`test` pattern; decide on-demand versus part of the default build
- [ ] 118.6 Decide committed-to-the-repo versus generated-and-gitignored (see the trade-off below) and implement the corresponding CI gate
- [ ] 118.7 Confirm the tool runs once, host-only, independent of target — no accidental per-target regeneration

## Notes / findings

**The tension with `06-engine-conventions.md` is real and the fix is additive, not a rewrite.** That document pushes comments toward explaining *why*, not restating the signature — good guidance for a human reading the header in place. The existing 142 `///` lines follow it and read well for that audience. What a generated reference additionally wants is structured `@param`/`@returns` data, a different axis entirely (machine-extractable facts about a call site) from prose explaining rationale. The fix is adding tag discipline for public API on top of the existing prose, not replacing it — an agent benefits from both the "why" and the structured signature facts, for different reasons.

**Committed versus gitignored is left as an open question, not decided here**, because both are genuinely defensible and the trade-off is a project-management call as much as a technical one:

- **Committed**: an agent (or a human on GitHub) sees the reference without building anything first, and it is what makes the "build, then fail if the tree is dirty" CI gate possible at all. Costs review noise on every PR that touches a public header (the generated diff rides along with the real change) and a growing generated artefact in history.
- **Gitignored, generated fresh**: no generated-file noise in history or reviews. Costs that nobody sees the reference without running the build step first — which matters specifically for the stated use case, since an agent working against a fresh checkout that has not been built yet would find no reference until it builds one.

**Ties to T0109.** A game project outside this repository builds its gameplay module against a *released* engine (T0109's "installed engine" question — headers, import libs, shared libraries, a version stamp). Whichever form this reference takes, it is part of what an installed engine ships, not only an artefact of this repository — 109.1's SDK-layout decision should account for it once both tickets are further along. Not recorded as a `Blocks` relationship: T0109 is Phase 8 and does not structurally require this ticket to land first, but it does need to know this exists.

**Public surface is exactly `engine/include/hp/`.** `engine/src/` is implementation and out of scope for this reference regardless of what comment density it carries — matching the layout `06-engine-conventions.md` already states (`include/hp/…` public, `src/…` implementation).

**Placed early (Phase 2, Order 45) rather than later.** Gameplay-facing headers accumulate from Phase 2 onward, and a coding agent writing against them needs the reference from the start, not once Phase 12's UI epic is reached. The same "land the rule before the surface grows" reasoning that put T0019's profiling macros ahead of the systems they instrument applies here: retrofitting tag discipline and a generation step across a large, already-grown header surface is a sweep nobody schedules; doing it now, against 8 headers, is cheap.


## Decisions taken (2026-08-03)

**118.1 Tooling: python + libclang.** `clang-doc` is **not** in the pinned
toolchain — checked, `find .harness/zig -name 'clang-doc*'` returns nothing, so
the ticket's flagged unknown is settled negatively. Doxygen *is* on this host
but was rejected twice over: it would be an unpinned host dependency, which is
what D5 exists to prevent, and its markdown is shaped for human browsing rather
than for an agent. libclang gives exact signatures and Python is already a build
requirement (DiligentCore pip-installs jinja2 during configure), so this adds a
dependency of a kind the project already carries.

**The diagnostics rule is what makes it trustworthy.** The host's libclang is a
different LLVM version from the one zig bundles, so parsing zig's libc++ headers
produces version-mismatch noise — 13 errors on `Log.hpp` alone. Rather than
ignore diagnostics wholesale, the generator distinguishes: **errors originating
in our own headers are fatal and refuse to write output; errors inside standard
library headers are tolerated.** Verified that extraction from a header with 13
libc++ errors and zero own-header errors is complete and correct — all 28
declarations, correct return types and signatures.

Include paths are asked of `zig c++ -E -v` rather than hardcoded, so the
reference describes the API as the real build sees it and does not drift when
zig is bumped.

**118.2 Structure: one file per header, plus an index.** A single file is
tempting at nine headers and wrong at fifty. Per-header means an agent asking
"what is the logging API" reads `docs/api/Log.md` and nothing else, which is the
context-window property that matters. `index.md` carries the map: a header
table, a full symbol list, and the cross-cutting rules.

**Prose density: the full doc comment, not a summary.** This project's comments
explain *why* — why `LogCategory` is a handle, why sinks are non-owning — and
that is precisely what stops an agent misusing an API. Truncating to a one-liner
would discard the most valuable part.

**118.4 Conventions inline: a rules preamble in `index.md`.** The module-boundary
rules, the exception policy, `HP_ASSERT` semantics and the `entt::type_index`
prohibition are reproduced next to the API rather than left in
`06-engine-conventions.md`. An agent reads what it is given; a document it was
never pointed at may as well not exist.

**118.5 `zig build docs`, on demand.** Not part of the default build: it needs
python and libclang, and making `zig build` fail for someone who only wants to
compile the engine is a poor trade for output that changes rarely. Host-only and
target-independent — there is nothing to generate twice.

**118.6 Committed to the repository.** An agent working in a fresh clone must
not have to build the engine first to learn its API, and a human reading the
repo on GitHub gets the same. It also enables the CI gate below. The cost —
regenerated files in diffs — is accepted and small at this size.

**The CI gate is the one the build-step design enables**: run `zig build docs`,
then fail if `git diff --quiet -- docs/api` reports changes. No second
header-parsing pass, no drift detector to maintain. Proven to fail: changing
`Guid::generate()` to `generate(int seedHint)` changed the generated output.

## Progress

Working and verified:

```
$ zig build docs
api docs: 87 declarations across 9 headers (39 documented) -> docs/api
```

Generated: `docs/api/index.md` plus one file per public header.

## Enforcement (2026-08-03)

The question "how do we ensure `@param` is strictly there and nothing is
missing" has one good answer: **the generator already parses every declaration
and knows every parameter name, so it can prove the documentation matches.** It
is the enforcer, and the check runs by default rather than behind a flag — a
lint nobody remembers to enable is a lint that does not exist.

Three defect classes, in increasing order of how badly they mislead:

| Class | What an agent does with it |
|---|---|
| `missing-doc` | Sees a signature and guesses the semantics |
| `missing-param` | Guesses that one parameter |
| `stale-param` | **Believes documentation that is wrong** |

**`stale-param` is the reason this exists.** An `@param` naming a parameter that
was renamed is not a gap — it is a confident lie, strictly more dangerous than
silence, and nothing else in the toolchain catches it. Proven:

```
$ zig build docs      # after renaming a parameter but not its @param
error: Log.hpp:59 LogCategory: missing-param 'categoryName'
error: Log.hpp:59 LogCategory: stale-param 'name' (no such parameter)
```

**A ratchet, not a wall.** Turning the check on strictly at 39-of-87 documented
would fail every build until someone wrote 48 comments in one sitting, which in
practice means the check gets disabled. `tools/api_docs_baseline.txt` records
the 75 known defects and **may shrink but never grow**: new public API must be
documented from its first commit, while the existing gap is paid down over time.
It lists specific defects rather than a count, deliberately — a count lets
someone fix one, break another, and stay green.

Rewriting the baseline is explicit (`zig build docs -Ddocs-baseline`) and should
never be done to make a failure go away.

**Enforced in two places, for two different failures.** `zig build docs` fails
on a new documentation defect; CI additionally fails if the tree is dirty
afterwards, which catches changing a header without regenerating. Both are
correctness failures for the consumer.

Verified end to end: adding an undocumented public function fails the build
(`error: Engine.hpp:42 undocumentedThing: missing-doc`, exit 1).

## Still to do

**118.3 is not done, and the coverage number says so plainly: 39 of 87
declarations carry documentation.** The generator marks the rest
`*No documentation comment.*` rather than omitting them, so the gap is visible
to an agent rather than silent — an undocumented symbol is far safer than an
absent one, because absence invites invention.

What remains is paying down those 75 baselined defects — adding
`@param`/`@returns` to existing public functions — and writing the expectation
into `06-engine-conventions.md`. **The rule itself is now enforced**, so the
surface cannot grow undocumented while that happens, which was the urgent half. That is the same "land the rule before the surface grows" reasoning
that put T0019 ahead of the code it instruments — and it is worth doing before
the header count grows past nine.
