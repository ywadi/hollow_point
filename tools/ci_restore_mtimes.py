#!/usr/bin/env python3
"""Restore commit-derived mtimes after a fresh CI checkout.

Why this exists
---------------
CI caches the build tree (`actions/cache`), and the cache round-trip preserves
mtimes: `tar --posix` on save, plain `tar -xf` on restore. The checkout,
however, is a fresh clone whose every file is stamped with clone time. So on a
warm run every build *output* is older than every *input*, ninja's mtime
comparison marks the entire graph dirty, and the restored objects -- verified
byte-for-byte present -- buy zero compile avoidance. Run 30946039676 rebuilt
all 1476 compile edges of a fully restored tree, ~18 minutes of the 22-minute
job. The same inversion makes ninja re-run CMake every run (build.ninja is
older than every CMakeLists.txt), which rewrites the compiler shims, which is
what silently defeated ccache back in T0121: with compiler_check=mtime a
rewritten shim is a "new compiler" and every cached object is unreachable.
Measured locally: 16 hits out of 1710 on a bit-identical rebuild.

What it does
------------
Stamps every file with the newest commit that could have changed it, so mtimes
carry the same information git content does:

- Tracked files in this repo: the timestamp of the last commit touching that
  path. A file changed by the commit under test gets that commit's (recent)
  timestamp and correctly rebuilds; everything else predates the cached outputs
  and is correctly left alone.
- Submodule files: max(submodule HEAD commit time, time the pin last moved in
  the superproject, containing submodule's own stamp). The pin-move term keeps
  a *rollback* honest -- checking out an older submodule commit must still look
  newer than the previous run's outputs, or ninja would keep linking objects of
  the newer code it no longer has sources for.

Files the log cannot attribute (shallow-clone boundary) keep their checkout
mtime and simply rebuild: the failure mode is wasted work, never a stale
binary. Run from the repository root after checkout, before the build.
"""

import os
import subprocess
import sys
import time


def git(args, cwd=None):
    return subprocess.run(
        ["git", "-c", "core.quotePath=false", *args],
        cwd=cwd, check=True, capture_output=True, text=True, encoding="utf-8",
    ).stdout


def ensure_history(repo_root):
    """A shallow clone attributes every file to the boundary commit, which
    stamps everything with HEAD's (recent) time and rebuilds the world -- safe
    but useless. Deepen the commit graph without blobs: `git log --name-only`
    needs commits and trees only, and for this repository that is a fraction of
    a second of fetch. On failure, warn and degrade to rebuilds."""
    shallow = git(["rev-parse", "--is-shallow-repository"], cwd=repo_root).strip()
    if shallow != "true":
        return
    try:
        git(["fetch", "--quiet", "--filter=blob:none", "--unshallow", "origin"],
            cwd=repo_root)
    except subprocess.CalledProcessError as e:
        print(f"warning: could not unshallow ({e.stderr.strip()}); "
              "unattributed files keep checkout mtimes and will rebuild",
              file=sys.stderr)


def last_commit_times(repo_root):
    """One pass over history: path -> timestamp of the newest commit touching it.

    -m --first-parent makes merge commits list their full diff against the
    first parent, so a file changed only by a merge is still attributed to it.
    """
    sentinel = "\x01"
    out = git(
        ["log", "-m", "--first-parent", f"--format={sentinel}%ct", "--name-only"],
        cwd=repo_root,
    )
    times = {}
    current = None
    for line in out.splitlines():
        if line.startswith(sentinel):
            current = int(line[1:])
        elif line and current is not None:
            # First (newest) attribution wins.
            times.setdefault(line, current)
    return times


def stamp(path, ts):
    try:
        os.utime(path, (ts, ts))
        return True
    except OSError:
        return False


def stamp_tree(root, ts):
    """Stamp every file under root, skipping .git (dir in a clone, file in a
    submodule -- prune both, they are not build inputs)."""
    count = 0
    for dirpath, dirnames, filenames in os.walk(root):
        if ".git" in dirnames:
            dirnames.remove(".git")
        for name in filenames:
            if name == ".git":
                continue
            count += stamp(os.path.join(dirpath, name), ts)
    return count


def main():
    repo_root = os.getcwd()
    if not os.path.exists(os.path.join(repo_root, ".git")):
        sys.exit("run from the repository root")

    t0 = time.monotonic()
    ensure_history(repo_root)

    # --- tracked files of the main repository -----------------------------
    times = last_commit_times(repo_root)
    tracked = [p for p in git(["ls-files", "-z"]).split("\0") if p]
    stamped = unattributed = 0
    for path in tracked:
        ts = times.get(path)
        if ts is None:
            unattributed += 1  # shallow boundary: keep checkout mtime, rebuild
        elif stamp(os.path.join(repo_root, path), ts):
            stamped += 1

    # --- submodules -------------------------------------------------------
    # Parents before children (sorted by depth), so a child can fold its
    # containing tree's stamp into its own and re-stamp itself afterwards.
    status = git(["submodule", "status", "--recursive"])
    subs = sorted(
        (line.split()[1] for line in status.splitlines() if line.strip()),
        key=lambda p: p.count("/"),
    )
    sub_ts = {}
    sub_files = 0
    for sub in subs:
        head_ct = int(git(["log", "-1", "--format=%ct"], cwd=sub).strip())
        pin_moved = times.get(sub, 0)  # gitlink path in superproject history
        parents = [t for p, t in sub_ts.items() if sub.startswith(p + "/")]
        ts = max([head_ct, pin_moved, *parents])
        sub_ts[sub] = ts
        sub_files += stamp_tree(os.path.join(repo_root, sub), ts)
        # SDL's CMake shells out to git for SDL_REVISION, so CMake records the
        # submodule's gitdir HEAD as a configure input. Checkout recreates it
        # with clone-time mtime, which alone re-ran CMake on every CI run --
        # and the re-run regenerates a handful of headers, which recompiles
        # their consumers, which is how run 30951493282 tripped PCH validation
        # on an otherwise clean tree. HEAD's content only changes when the pin
        # moves, so it earns the submodule's stamp like every other file.
        gitdir = git(["rev-parse", "--absolute-git-dir"], cwd=sub).strip()
        stamp(os.path.join(gitdir, "HEAD"), ts)

    print(
        f"restored mtimes: {stamped} tracked files "
        f"({unattributed} unattributed, left at checkout time), "
        f"{sub_files} files in {len(subs)} submodules, "
        f"{time.monotonic() - t0:.1f}s"
    )


if __name__ == "__main__":
    main()
