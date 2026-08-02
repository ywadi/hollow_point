# T0082 — Schema versioning and migration

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Created** | 2026-08-03 |

## Why

Scenes, prefabs, materials, project files and save games are all serialized, and
their schemas **will** change — a field renamed, a component split, a type's
meaning revised. Without a migration path, the options at that moment are: break
every existing file, or never change the schema.

Both are bad, and this is very cheap to build now and expensive to retrofit —
retrofitting means writing migrations for versions that were never recorded.

## Done when

- [ ] Every serialized file records a schema version
- [ ] Loading an older version runs registered migrations up to current
- [ ] Loading a *newer* version fails clearly rather than corrupting data
- [ ] Migrations are testable in isolation, with fixtures per version
- [ ] Migration is reported, so a silently-upgraded file is not a surprise
- [ ] The binary cache invalidates when the schema version changes (T0020)

## Subtasks

- [ ] 82.1 Version field in every serialized root (scene, prefab, project, material)
- [ ] 82.2 Migration registry: from-version → transform
- [ ] 82.3 Chained migrations across several versions
- [ ] 82.4 Refuse newer-than-current with a clear message
- [ ] 82.5 Tie the binary cache key to the schema version
- [ ] 82.6 Keep fixture files per historical version as test data
- [ ] 82.7 Document how to add a migration when changing a schema

## Notes / findings

**Add the version field before it is needed.** A file with no version cannot be
migrated, only guessed at. The field costs nothing today and is impossible to add
retroactively to files already in the wild.

**Refusing newer files matters as much as migrating older ones.** If someone opens
a project saved by a newer build, best case is a confusing error; worst case is
loading it partially, discarding the fields it did not understand, and saving that
back — silently destroying work. Fail closed.

**Migrate on load, in memory — do not rewrite files behind the user's back.** The
file upgrades when they next save. Rewriting on open means opening a project in a
newer build silently makes it unopenable in the older one.

Keeping fixture files per version is what makes migrations trustworthy. They are
tiny, and without them a migration chain is only ever tested against whatever the
current schema happens to be.
