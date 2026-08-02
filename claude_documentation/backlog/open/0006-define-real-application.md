# T0006 — Define and scaffold the real application

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Created** | 2026-08-02 |
| **Blocks** | T0007 |

## Why

There is no application. The previous one (`apps/terrain_lab`, an Atmosphere-
sample derivative with custom water/terrain layers) was deliberately deleted, and
`apps/imgui_probe` is a disposable smoke test.

Everything built so far — the harness, ImGui docking, enkiTS, meshoptimizer,
ozz — is scaffolding for an application whose shape has not been decided. That
decision drives real questions that are cheap now and expensive later: whether
`SampleBase` is the right shell, how assets are laid out, and whether the
DiligentFX dependency (and therefore the mandatory ImGui dependency, D6) stays.

## Done when

- [ ] The application's purpose and scope are written down
- [ ] `apps/<name>/` exists and builds for both targets
- [ ] It is decided whether to keep Diligent's `add_sample_app()` shell or write
      a bespoke entry point
- [ ] `01-project-overview.md` updated

## Subtasks

- [ ] 6.1 Decide what it is — **needs the user**; do not guess
- [ ] 6.2 Choose the app shell (see notes)
- [ ] 6.3 Scaffold `apps/<name>/` with `CMakeLists.txt`, `src/`, and a
      `readme.md` (mandatory — G8)
- [ ] 6.4 Build both targets
- [ ] 6.5 Decide the asset layout and how it reaches `dist/`

## Notes / findings

- The root `CMakeLists.txt` globs any `apps/*/` containing a `CMakeLists.txt`, so
  adding a directory is enough — no root edit needed.
- **On the app shell:** `add_sample_app()` handles the per-platform entry points
  (`WinMain` vs `main`), the Win32 resource, input controllers, and DLL copying
  on Windows. That is a genuinely useful amount of work to inherit. The cost is a
  dependency on `DiligentSamples`, which is a samples framework rather than an
  application framework. Worth a deliberate decision, not a default.
- If DiligentFX is not needed, dropping it would also drop the forced ImGui
  dependency — but DiligentFX is the atmosphere/SSR/SSAO/bloom/tonemapping
  library, which is likely the reason Diligent is here at all.
