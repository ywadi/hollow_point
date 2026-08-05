# T0140 — Editing game code from the editor, with Ned as an option

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 6 — Editor |
| **Order** | 672 |
| **Created** | 2026-08-05 |
| **Blocked by** | T0032 (a code editor is a panel, and there is no panel framework yet) |
| **Refs** | T0032, T0066, [../completed/0048-hot-reloadable-gameplay-module.md](../completed/0048-hot-reloadable-gameplay-module.md), [../open/0024-project-manager.md](../open/0024-project-manager.md), [../inprogress/0078-settings-and-config.md](../inprogress/0078-settings-and-config.md), [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D12, D14 |

## Why

**Gameplay here is C++, compiled against the real headers, hot-reloaded** (D12,
D14, T0048). There is no scripting layer, which is a deliberate decision and not
one this ticket reopens — but it means the authoring loop for behaviour is *edit
a `.cpp`, rebuild, hot reload*, and today the "edit a `.cpp`" step happens
entirely outside the editor with no connection to the project it belongs to.

The gap is not that people lack a text editor. It is that the editor knows which
project is open, which module it builds, and when a reload succeeded or failed —
and none of that reaches the place the code is actually written. Opening the
right file at the right line after a compile error is the smallest version of
this and is already worth having.

**The owner has asked for [Ned](https://github.com/nealmick/ned) to be one of the
options.** "One of" is the requirement: this ticket builds a *choice*, not a
single blessed editor.

## Done when

- [ ] The editor can open a gameplay source file for editing, from a context that
      knows the project (asset panel, console error, module load failure)
- [ ] **Which editor is used is configurable**, and at least one option is an
      external editor already on the machine
- [ ] Ned is supported as an option, embedded or launched — see the open question
- [ ] A compile error can be jumped to: file **and line**, not just the file
- [ ] Choosing an editor that is not installed fails with a message naming what
      it looked for, rather than doing nothing
- [ ] The choice persists across sessions (T0078) and is per-user, **not** stored
      in the project file — a teammate's editor preference is not project data
- [ ] Nothing about this is required to run the editor: with no editor configured
      everything else still works

## Subtasks

- [ ] 140.1 Decide embedded vs external, and record it (see below)
- [ ] 140.2 An `openInEditor(path, line)` seam with a null implementation
- [ ] 140.3 External-editor backend, configured by command template
- [ ] 140.4 Ned backend
- [ ] 140.5 Wire the call sites: asset panel, console (T0066), module load failure
- [ ] 140.6 Persist the preference through T0078
- [ ] 140.7 Tests for the seam and the command templating; the editors themselves
      are not testable here and should not pretend to be

## The open question, which is the owner's and not an engineering one

**Which Ned, and embedded or launched?** The name is ambiguous and the answer
changes the size of this ticket by an order of magnitude:

| Candidate | What it is | Fit |
|---|---|---|
| [nealmick/ned](https://github.com/nealmick/ned) | **ImGui** text editor with GL shaders, tree-sitter highlighting, LSP, terminal | Most likely what was meant. ImGui is already in the tree and docking is verified, so this could be a *panel* rather than a process |
| [NEdit](https://sourceforge.net/projects/nedit/) | Motif/X11 editor from 1992, mature | **GPL-2.0.** Launchable as a process; linking it is not an option for a proprietary engine. Unix only, so it fails the Windows target outright |
| [vinc/ned](https://github.com/vinc/ned) | A small terminal editor | Launchable, nothing to embed |

**Assume nealmick/ned until told otherwise** — it is ImGui-based, which is the
only one of the three that could become a panel in this editor rather than a
process beside it.

Three things must be checked before any of it is vendored, and each has killed a
candidate here before:

- **Licence.** NEdit is GPL-2.0, which is disqualifying for anything linked into
  a proprietary engine. nealmick/ned's licence has **not** been checked and must
  be, first, before any other evaluation — it decides whether embedding is even
  on the table.
- **The build constraints in `03-build-harness.md`.** No network fetch at
  configure time, builds under the pinned zig/cmake toolchain for **both** Linux
  and Windows, not MSVC-only, not POSIX-only, and no build system of its own that
  fights the harness. T0048's survey rejected several libraries on exactly these.
- **Maintenance surface**, which is the owner's call and not a technical one.
  An embedded editor with tree-sitter, LSP and a terminal is a large thing to
  carry for a studio building games. Launching whatever editor the developer
  already uses costs almost nothing and is what most engines do.

## Design

### Build the seam first, and the backends behind it

Whatever is decided above, **the editor must not call an editor directly from
half a dozen call sites.** One seam:

```cpp
/// Opens a source file for editing, at a line when one is known.
/// @returns false when no editor is configured or the launch failed -- callers
///          treat it as "nothing happened", never as an error worth a dialog.
bool openInEditor(std::string_view path, int line = 0);
```

The null implementation returning `false` is the default, and it is what keeps
the last Done-when true: an editor with nothing configured is not degraded, it
simply has one affordance switched off.

Doing this first also means 140.1 can be answered *late* and cheaply, which is
the right shape for a question that is the owner's to settle.

### External first, embedded second — whichever Ned wins

The external backend is a command template (`code --goto {file}:{line}`,
`ned {file}`, whatever) and is perhaps a hundred lines. It covers every editor
anyone might already use, including Ned as a process, and it is the fallback if
embedding is rejected on licence, build or maintenance grounds.

Embedding is then a strictly additive second backend, not a rewrite.

### Do not build a text editor

If embedding is rejected, **the answer is the external backend, not writing
one.** A serviceable code editor is years of work — syntax highlighting,
undo that survives a large paste, find and replace, encodings, large files —
and none of it makes a better game engine. This is precisely the
"do not reinvent wheels" rule in `CLAUDE.md`, with the extra wrinkle that
every developer already has an editor they are faster in.

## Notes / findings

**T0032 is a hard prerequisite for the embedded option only.** The external
backend needs nothing but a settings entry and a process launch, so it could land
before the panel framework exists if the value is wanted sooner.

**Line numbers come from the build, not from the editor.** The compile error that
gets jumped to originates in the harness output, so whatever parses that output
into `(file, line)` is shared with anything else that wants build diagnostics in
the UI — do not bury it inside this feature.

**This does not reopen scripting.** D12 and D14 settled that gameplay is C++
against the real headers. An in-editor code editor edits those sources and
triggers the existing rebuild-and-reload path (T0048); it is a convenience over
the loop that already exists, and it must not grow into a second way to define
behaviour.
