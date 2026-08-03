#!/usr/bin/env bash
# Assemble the vendored Linux sysroot used when building the Linux target.
#
# Zig ships libc and the Windows SDK, but not the X11/xcb/GL stack, so those
# headers and link stubs live in the repo. Vendoring them means a Linux build
# needs no -dev packages on the host and produces identical inputs everywhere --
# including on a Windows host, which has no way to apt-get them.
#
# The libraries here are generated *stubs*, not copies of the host's shared
# objects. A real libX11.so.6 from a modern distribution carries undefined
# references to dlopen@GLIBC_2.34 and friends; linking an executable against it
# while targeting glibc 2.28 fails outright:
#
#     ld.lld: error: undefined reference: dlopen@GLIBC_2.34
#
# A stub exports the same symbol names under the same SONAME but has no libc
# dependency of its own, so it satisfies the linker without dragging the build
# host's glibc version into the output. At runtime the loader binds to the
# user's real libX11.so.6 by SONAME, as usual.
#
# Re-run this only to refresh the sysroot; the result is committed.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/third_party/sysroot/linux-x86_64"
SRC_INC=/usr/include
SRC_LIB=/usr/lib/x86_64-linux-gnu
# Host-keyed since T0102; this script already refuses to run anywhere but Linux.
ZIG="${ZIG:-$ROOT/.harness/zig/linux-$(uname -m)/0.16.0/zig}"

# Must match cmake/toolchains/x86_64-linux-gnu.cmake.
ZIG_TARGET=x86_64-linux-gnu.2.28

[ "$(uname -s)" = "Linux" ] || { echo "error: must be run on a Linux host" >&2; exit 1; }
[ -x "$ZIG" ] || { echo "error: no zig at $ZIG (run ./bootstrap.sh, or set ZIG=...)" >&2; exit 1; }
command -v readelf >/dev/null || { echo "error: readelf required (binutils)" >&2; exit 1; }

HEADER_DIRS=(X11 xcb GL KHR)
# libGLdispatch/libXau/libXdmcp are DT_NEEDED of the libs above; the linker
# wants them present even though we never name them directly.
LIBS=(libX11 libxcb libGL libGLX libOpenGL libGLdispatch libXau libXdmcp)

echo "==> assembling $OUT"
rm -rf "$OUT"
mkdir -p "$OUT/include" "$OUT/lib"

for d in "${HEADER_DIRS[@]}"; do
    [ -d "$SRC_INC/$d" ] || { echo "error: missing $SRC_INC/$d (install libx11-dev libxcb1-dev libgl-dev)" >&2; exit 1; }
    cp -aL "$SRC_INC/$d" "$OUT/include/"
    echo "    include/$d"
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

for l in "${LIBS[@]}"; do
    real="$(readlink -f "$SRC_LIB/$l.so" 2>/dev/null || true)"
    [ -n "$real" ] && [ -f "$real" ] || { echo "error: missing $SRC_LIB/$l.so" >&2; exit 1; }
    soname="$(readelf -d "$real" | awk -F'[][]' '/SONAME/{print $2}')"
    [ -n "$soname" ] || soname="$l.so"

    # Exported, globally visible symbols only. For versioned names keep the
    # default version (name@@ver) and drop the compat aliases (name@ver), which
    # would otherwise collide once the version suffix is stripped. The stub
    # exports them unversioned; the loader then binds each to the real library's
    # default version at runtime.
    readelf --dyn-syms -W "$real" | awk '
        NR>3 && $7!="UND" && ($4=="FUNC"||$4=="OBJECT") && ($5=="GLOBAL"||$5=="WEAK") {
            n=$8
            if (n ~ /@@/) sub(/@@.*/,"",n)
            else if (n ~ /@/) next
            if (n=="") next
            print $4, n, $3
        }' | sort -u > "$WORK/syms"

    # Data symbols keep their size: a copy relocation against an
    # undersized object would corrupt adjacent memory.
    awk '$1=="FUNC"  { printf "void %s(void){}\n", $2 }
         $1=="OBJECT"{ s=$3+0; if (s<1) s=1; printf "char %s[%d];\n", $2, s }' \
        "$WORK/syms" > "$WORK/stub.c"

    "$ZIG" cc -target "$ZIG_TARGET" -shared -nostdlib -fPIC \
        -Wl,-soname,"$soname" -o "$OUT/lib/$soname" "$WORK/stub.c"

    # The .so name is what -l<name> resolves against at link time.
    #
    # A real copy, not a symlink. This tree gets checked out on Windows too, and
    # neither git can produce a symlink a Win32 process can read: WSL git writes
    # an LX reparse point (Win32 cannot open it at all -- "the file cannot be
    # accessed by the system"), and Git for Windows with the default
    # core.symlinks=false writes a text file containing the target's name. Both
    # make `find_library` fail, which fails configure of the Linux target from a
    # Windows host with `Could NOT find X11 (missing: X11_X11_LIB)` (T0004).
    #
    # The copy keeps the versioned SONAME, so link-time and runtime behaviour are
    # unchanged -- the loader still binds the user's real library by SONAME, and
    # the RPATH reasoning in zig-common.cmake (gotcha G6) still holds.
    #
    # Costs ~1.4 MB of duplication. Emitting the stub *once* as lib<n>.so with
    # -Wl,-soname,<versioned> would avoid that and is probably what this should
    # do eventually, but the duplicated form is what has actually been built and
    # run on both hosts.
    cp -f "$OUT/lib/$soname" "$OUT/lib/$l.so"
    echo "    lib/$l.so (copy of $soname, $(wc -l < "$WORK/syms") symbols)"
done

{
    echo "Linux x86_64 sysroot for the HollowPoint zig build harness."
    echo
    echo "Headers are copied from the host. Libraries are GENERATED STUBS: they"
    echo "export the same symbols under the same SONAME but have no libc"
    echo "dependency, so the glibc pin ($ZIG_TARGET) holds regardless of what"
    echo "the build host runs. They are link-time only -- at runtime the loader"
    echo "binds to the user's real libraries by SONAME."
    echo
    echo "Generated by tools/mk_linux_sysroot.sh"
    echo
    echo "Origin host: $( (. /etc/os-release && echo "$PRETTY_NAME") 2>/dev/null || uname -sr)"
    echo "Arch:        x86_64"
    echo
    echo "Package versions the headers and symbol lists came from:"
    if command -v dpkg-query >/dev/null 2>&1; then
        for p in libx11-dev libxcb1-dev libgl-dev libglx-dev libopengl-dev libglvnd-dev \
                 libxau-dev libxdmcp-dev x11proto-dev mesa-common-dev; do
            v="$(dpkg-query -W -f='${Version}' "$p" 2>/dev/null || true)"
            [ -n "$v" ] && echo "  $p $v"
        done
    else
        echo "  (dpkg unavailable)"
    fi
} > "$OUT/PROVENANCE.txt"

echo "==> done: $(du -sh "$OUT" | cut -f1)"
