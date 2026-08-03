#!/usr/bin/env bash
# Report Windows SDK headers and import libraries that the engine spells with
# capitals but MinGW-w64 ships lowercase.
#
# Cross-compiling to Windows from a case-sensitive filesystem fails on every one
# of these, so cmake/toolchains/zig-common.cmake generates a forwarding header
# (or a capitalised import library, via `zig dlltool`) for each. Re-run this
# after updating DiligentEngine or Zig and fold new names into the
# _hp_win_headers / _hp_win_libs lists there.
#
# Only the file name differs in case, never the directory: <GL/GL.h> resolves to
# GL/gl.h, not gl/gl.h.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Host-keyed since T0102. A Linux-host helper: it inspects the case-sensitivity
# problem that only arises when cross-compiling from one.
ZIG="${ZIG:-$ROOT/.harness/zig/linux-$(uname -m)/0.16.0/zig}"
SRC="$ROOT/third_party/DiligentEngine"

[ -x "$ZIG" ] || { echo "error: no zig at $ZIG (set ZIG=...)" >&2; exit 1; }

# `zig env` reports lib_dir relative to the cwd, so ask from zig's own directory.
LIB_DIR="$(cd "$(dirname "$ZIG")" && "$ZIG" env | sed -n 's/.*\.lib_dir = "\(.*\)",/\1/p')"
case "$LIB_DIR" in /*) ;; *) LIB_DIR="$(dirname "$ZIG")/$LIB_DIR" ;; esac
ANY="$LIB_DIR/libc/include/any-windows-any"

[ -d "$ANY" ] || { echo "error: no mingw headers at $ANY" >&2; exit 1; }

echo "# headers (add to _hp_win_headers)"
grep -rhoE '#[[:space:]]*include[[:space:]]*[<"][A-Za-z0-9_./-]+\.h[>"]' "$SRC" 2>/dev/null \
| sed -E 's/.*[<"]([^<>"]+)[>"]/\1/' | sort -u \
| while read -r h; do
    d="$(dirname "$h")"; b="$(basename "$h")"
    lcb="$(printf '%s' "$b" | tr 'A-Z' 'a-z')"
    if [ "$d" = "." ]; then cand="$lcb"; else cand="$d/$lcb"; fi
    if [ "$h" != "$cand" ] && [ ! -e "$ANY/$h" ] && [ -e "$ANY/$cand" ]; then
        echo "  $h"
    fi
done

# Import libraries can only be read off a configured build tree, because that is
# where the resolved link lines live.
echo "# import libraries (add to _hp_win_libs)"
found=0
for nj in "$ROOT"/build/windows-*/build.ninja; do
    [ -f "$nj" ] || continue
    found=1
    grep -ohE '\-l[A-Z][A-Za-z0-9_]*' "$nj" | sed 's/^-l//' | sort -u \
    | while read -r n; do
        lc="$(printf '%s' "$n" | tr 'A-Z' 'a-z')"
        # A lowercase .def in the MinGW tree means it is a Windows system
        # library. Anything else -- SPIRV, glslang -- is a project target and
        # must NOT be added: the generated dir is first on the search path and
        # would shadow the real library.
        if find "$LIB_DIR/libc/mingw" -name "$lc.def" | grep -q .; then
            echo "  $n"
        fi
    done
done
[ "$found" = 1 ] || echo "  (configure a Windows build tree first)"
