# HollowPoint toolchain: x86_64 Windows, built with Zig.
#
# Zig targets Windows through the MinGW-w64 ABI and bundles the headers and
# import libraries, so nothing external is needed -- this cross-compiles from
# Linux as readily as it builds natively.
#
# Consequence worth knowing: MinGW-w64 has no atlbase.h, and DiligentCore gates
# its D3D11/D3D12 backends on ATL (DiligentCore/CMakeLists.txt:160-181). Those
# probes fail on their own and the engine configures itself for Vulkan + OpenGL.
# That is expected, not a misconfiguration. Direct3D would require the MSVC ABI
# and a real Windows SDK, which cannot be driven from a Linux host.

set(CMAKE_SYSTEM_NAME      Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(HP_TARGET "x86_64-windows-gnu")

include("${CMAKE_CURRENT_LIST_DIR}/zig-common.cmake")

# --- resource compiler -------------------------------------------------------
#
# DiligentSamples/SampleBase compiles a Win32 .rc. CMake would otherwise reach
# for windres and drive it with windres syntax; `zig rc` is a drop-in for
# Microsoft's rc.exe instead, so point at it and use the rc.exe command form.

# zig rc does not inherit the C include path, so bake the SDK headers into the
# shim -- along with the case-forwarding directory zig-common.cmake generates
# when the filesystem needs it (Win32AppResource.rc does `#include "Windows.h"`).
set(_hp_rc_includes "-I\"${HP_ZIG_LIB_DIR}/libc/include/any-windows-any\"")
if(_hp_case_sensitive)
    set(_hp_rc_includes "-I\"${HP_SHIM_DIR}/wininc\"" "${_hp_rc_includes}")
endif()
_hp_shim(zig-rc "rc" ${_hp_rc_includes})

set(CMAKE_RC_COMPILER "${zig-rc_SHIM}" CACHE FILEPATH "" FORCE)
set(CMAKE_RC_FLAGS_INIT "")
# `--` is required: source paths are absolute, and on a Linux host they start
# with `/`, which zig rc would otherwise try to parse as an option.
set(CMAKE_RC_COMPILE_OBJECT
    "<CMAKE_RC_COMPILER> <DEFINES> <INCLUDES> <FLAGS> /fo <OBJECT> -- <SOURCE>")
