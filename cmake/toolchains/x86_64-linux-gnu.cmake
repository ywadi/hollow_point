# HollowPoint toolchain: x86_64 Linux, built with Zig.
#
# glibc is pinned to 2.28 (RHEL 8 / Debian 10 era). Zig synthesises the stubs
# for that version regardless of what the build host runs, so the output works
# on distributions considerably older than the machine that produced it.

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(HP_TARGET "x86_64-linux-gnu.2.28")

# X11/xcb/GL headers and link stubs -- see tools/mk_linux_sysroot.sh.
if(NOT HP_SYSROOT)
    set(HP_SYSROOT "${CMAKE_CURRENT_LIST_DIR}/../../third_party/sysroot/linux-x86_64")
endif()
get_filename_component(HP_SYSROOT "${HP_SYSROOT}" ABSOLUTE)
if(NOT EXISTS "${HP_SYSROOT}/include/X11/Xlib.h")
    message(FATAL_ERROR "Linux sysroot missing at ${HP_SYSROOT} -- run tools/mk_linux_sysroot.sh")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/zig-common.cmake")
