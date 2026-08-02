#!/usr/bin/env sh
# Install the pinned build toolchain into .harness/.
#
#   ./bootstrap.sh          then    .harness/zig/0.16.0/zig build
#
# Installs Zig, CMake and Ninja -- everything the build needs. Nothing has to be
# present on the host beyond curl (or wget), tar and a POSIX shell.
#
# CMake is pinned to the 3.x line on purpose. CMake 4 rejects
# `cmake_minimum_required(VERSION <3.5)`, and DiligentEngine's vendored
# third-party libraries still declare 2.8. 3.31 is also new enough for
# ozz-animation, which requires 3.30.
#
# POSIX sh on purpose: this has to run on a bare container as readily as on a
# workstation. See bootstrap.ps1 for the Windows host.
set -eu

ZIG_VERSION=0.16.0
CMAKE_VERSION=3.31.12
NINJA_VERSION=1.13.2

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DL="$ROOT/.harness/dl"

ARCH=$(uname -m)
case "$(uname -s)-$ARCH" in
    Linux-x86_64)
        ZIG_SLUG=x86_64-linux
        ZIG_SHA=70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00
        CMAKE_SLUG=linux-x86_64
        CMAKE_SHA=0dc2e9a6860f06bf10bd8fadc03e35d9eeb4df46e33763a7e480e987758f385c
        NINJA_FILE=ninja-linux.zip
        NINJA_SHA=5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6
        ;;
    Linux-aarch64)
        ZIG_SLUG=aarch64-linux
        ZIG_SHA=ea4b09bfb22ec6f6c6ceac57ab63efb6b46e17ab08d21f69f3a48b38e1534f17
        CMAKE_SLUG=linux-aarch64
        CMAKE_SHA=83f8fd91d2038a56556e1400390fcfe42f79602940c494f6c6f1cdae7f9e7f40
        NINJA_FILE=ninja-linux-aarch64.zip
        NINJA_SHA=fd2cacc8050a7f12a16a2e48f9e06fca5c14fc4c2bee2babb67b58be17a607fc
        ;;
    *)
        echo "error: unsupported host $(uname -s)-$ARCH" >&2
        echo "  Supported: Linux x86_64, Linux aarch64. Windows: use bootstrap.ps1" >&2
        exit 1 ;;
esac

mkdir -p "$DL"

# fetch <url> <archive-name> <sha256>
fetch() {
    _url=$1; _name=$2; _sha=$3
    if [ ! -f "$DL/$_name" ]; then
        echo "==> downloading $_name"
        if command -v curl >/dev/null 2>&1; then
            curl -fL --retry 3 -o "$DL/$_name.part" "$_url"
        elif command -v wget >/dev/null 2>&1; then
            wget -O "$DL/$_name.part" "$_url"
        else
            echo "error: need curl or wget" >&2; exit 1
        fi
        mv "$DL/$_name.part" "$DL/$_name"
    fi

    if command -v sha256sum >/dev/null 2>&1; then
        _actual=$(sha256sum "$DL/$_name" | cut -d' ' -f1)
    elif command -v shasum >/dev/null 2>&1; then
        _actual=$(shasum -a 256 "$DL/$_name" | cut -d' ' -f1)
    else
        echo "error: need sha256sum or shasum" >&2; exit 1
    fi

    if [ "$_actual" != "$_sha" ]; then
        echo "error: checksum mismatch for $_name" >&2
        echo "  expected $_sha" >&2
        echo "  actual   $_actual" >&2
        # A corrupt download must not be reused on the next run.
        rm -f "$DL/$_name"
        exit 1
    fi
}

# --- zig ---------------------------------------------------------------------

ZIG_DIR="$ROOT/.harness/zig/$ZIG_VERSION"
if [ -x "$ZIG_DIR/zig" ]; then
    echo "zig $ZIG_VERSION already present"
else
    ZIG_ARCHIVE="zig-$ZIG_SLUG-$ZIG_VERSION.tar.xz"
    fetch "https://ziglang.org/download/$ZIG_VERSION/$ZIG_ARCHIVE" "$ZIG_ARCHIVE" "$ZIG_SHA"
    echo "==> installing zig $ZIG_VERSION"
    mkdir -p "$ROOT/.harness/zig"
    rm -rf "$ZIG_DIR" "$ROOT/.harness/zig/zig-$ZIG_SLUG-$ZIG_VERSION"
    tar -xJf "$DL/$ZIG_ARCHIVE" -C "$ROOT/.harness/zig"
    mv "$ROOT/.harness/zig/zig-$ZIG_SLUG-$ZIG_VERSION" "$ZIG_DIR"
fi
"$ZIG_DIR/zig" version >/dev/null

# --- cmake -------------------------------------------------------------------

CMAKE_DIR="$ROOT/.harness/cmake/$CMAKE_VERSION"
if [ -x "$CMAKE_DIR/bin/cmake" ]; then
    echo "cmake $CMAKE_VERSION already present"
else
    CMAKE_ARCHIVE="cmake-$CMAKE_VERSION-$CMAKE_SLUG.tar.gz"
    fetch "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/$CMAKE_ARCHIVE" \
          "$CMAKE_ARCHIVE" "$CMAKE_SHA"
    echo "==> installing cmake $CMAKE_VERSION"
    mkdir -p "$ROOT/.harness/cmake"
    rm -rf "$CMAKE_DIR" "$ROOT/.harness/cmake/cmake-$CMAKE_VERSION-$CMAKE_SLUG"
    tar -xzf "$DL/$CMAKE_ARCHIVE" -C "$ROOT/.harness/cmake"
    mv "$ROOT/.harness/cmake/cmake-$CMAKE_VERSION-$CMAKE_SLUG" "$CMAKE_DIR"
fi
"$CMAKE_DIR/bin/cmake" --version >/dev/null

# --- ninja -------------------------------------------------------------------
#
# Shipped as a zip. Rather than require unzip, extract with the CMake that was
# just installed -- `cmake -E tar` handles zip archives.

NINJA_DIR="$ROOT/.harness/ninja/$NINJA_VERSION"
if [ -x "$NINJA_DIR/ninja" ]; then
    echo "ninja $NINJA_VERSION already present"
else
    fetch "https://github.com/ninja-build/ninja/releases/download/v$NINJA_VERSION/$NINJA_FILE" \
          "$NINJA_FILE" "$NINJA_SHA"
    echo "==> installing ninja $NINJA_VERSION"
    rm -rf "$NINJA_DIR"
    mkdir -p "$NINJA_DIR"
    (cd "$NINJA_DIR" && "$CMAKE_DIR/bin/cmake" -E tar xf "$DL/$NINJA_FILE")
    chmod +x "$NINJA_DIR/ninja"
fi
"$NINJA_DIR/ninja" --version >/dev/null

echo
echo "toolchain ready in .harness/"
echo "  zig    $ZIG_VERSION"
echo "  cmake  $CMAKE_VERSION"
echo "  ninja  $NINJA_VERSION"
echo
echo "next:  $ZIG_DIR/zig build all"
