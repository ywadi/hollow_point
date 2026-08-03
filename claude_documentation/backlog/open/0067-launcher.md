# T0067 — Project launcher

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 6 — Editor |
| **Order** | 690 |
| **Created** | 2026-08-03 |

## Why

The editor's entry point: create a new project, or open an existing one, before
the main window appears. Without it the editor either opens nothing useful or has
to guess, and creating a project means constructing a folder layout by hand.

Explicitly wanted — the editor is a tool for authoring projects, and this is the
front door.

## Done when

- [ ] A launcher window appears before the editor
- [ ] Create a new project: name, location, validated
- [ ] Open an existing project via a folder picker
- [ ] A recent-projects list, persisted
- [ ] Invalid selections are rejected with a clear reason, not a crash
- [ ] Opening transitions cleanly into the editor
- [ ] The launcher can be bypassed by passing a project path on the command line

## Subtasks

- [ ] 67.1 Launcher window, before the main editor window
- [ ] 67.2 New project: validate name and path, refuse to overwrite silently
- [ ] 67.3 Create the project structure via `ProjectManager::New` (T0024)
- [ ] 67.4 Open with validation that the folder really is a project (`.hpproj`)
- [ ] 67.5 Recent projects, stored in user config not project config
- [ ] 67.6 Command-line bypass — see notes
- [ ] 67.7 Transition into the editor without recreating the window if avoidable

## Notes / findings

**The command-line bypass is not a convenience, it is a development necessity.**
Being forced through a launcher on every run makes iterating on the editor itself
tedious, and makes automated testing of the editor awkward. `hp-editor <path>`
should skip straight in.

Recent projects belong in **user** config, not project config — they are about
this machine and this person, not about any project.

Validation should be genuinely helpful: "that folder has no `.hpproj`" is useful,
"failed to open project" is not. Creating into a non-empty directory should
require confirmation rather than silently merging.
