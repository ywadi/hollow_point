# Common CMake toolchain logic for building HollowPoint with Zig.
#
# Not used directly -- included by cmake/toolchains/<triple>.cmake, which sets
# HP_TARGET (and HP_SYSROOT where needed) first.
#
# CMake requires a compiler to be a single executable, but a Zig compiler
# invocation is `zig cc -target <triple> ...`. So we generate small shims that
# bake in the target (and sysroot search paths) and forward everything else.
# Baking the sysroot flags into the shim instead of CMAKE_<LANG>_FLAGS_INIT
# keeps path quoting in the shell, where it is actually reliable.
#
# Cache variables expected from the caller (build.zig passes all of them):
#   HP_ZIG       absolute path to the zig executable
#   HP_SHIM_DIR  directory to generate the shims into
# Optional:
#   HP_SYSROOT   root holding extra include/ and lib/ trees (Linux target)
#
# This file is also usable standalone, without the harness:
#   cmake -B out -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-linux-gnu.cmake \
#         -DHP_ZIG=/path/to/zig -DHP_SHIM_DIR=out/toolchain

if(NOT HP_TARGET)
    message(FATAL_ERROR "HP_TARGET must be set before including zig-common.cmake")
endif()

if(NOT HP_ZIG)
    find_program(HP_ZIG NAMES zig)
    if(NOT HP_ZIG)
        message(FATAL_ERROR "zig not found: pass -DHP_ZIG=<path> or put zig on PATH")
    endif()
endif()

if(NOT HP_SHIM_DIR)
    set(HP_SHIM_DIR "${CMAKE_BINARY_DIR}/toolchain")
endif()

# Zig's bundled headers live under <lib_dir>. Ask zig rather than deriving it
# from the exe path, which only holds for the official tarball layout.
# `zig env` emits ZON, not JSON, so match the field directly.
#
# `zig env` reports lib_dir *relative to the current directory* whenever that is
# shorter, so the working directory is pinned to zig's own and the result
# resolved against it. Absolute results pass through unchanged. Getting this
# wrong yields a path that happens to work at configure time and then breaks
# under Ninja, which runs every command from the build directory.
if(NOT HP_ZIG_LIB_DIR)
    get_filename_component(_hp_zig_dir "${HP_ZIG}" DIRECTORY)
    execute_process(COMMAND "${HP_ZIG}" env
                    WORKING_DIRECTORY "${_hp_zig_dir}"
                    OUTPUT_VARIABLE _hp_zig_env RESULT_VARIABLE _hp_rc
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT _hp_rc EQUAL 0 OR NOT _hp_zig_env MATCHES "\\.lib_dir = \"([^\"]*)\"")
        message(FATAL_ERROR "could not determine zig lib_dir from `${HP_ZIG} env`")
    endif()
    get_filename_component(HP_ZIG_LIB_DIR "${CMAKE_MATCH_1}" ABSOLUTE BASE_DIR "${_hp_zig_dir}")
endif()

# try_compile() runs in a fresh CMake process that re-reads this toolchain file,
# so these must be forwarded or the probe builds would target the host.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
     HP_ZIG HP_TARGET HP_SYSROOT HP_SHIM_DIR HP_ZIG_LIB_DIR)

# --- shim generation ---------------------------------------------------------

file(MAKE_DIRECTORY "${HP_SHIM_DIR}")

# Extra flags baked into the compile shims (sysroot search paths).
set(_hp_extra "")
if(HP_SYSROOT)
    if(CMAKE_HOST_WIN32)
        set(_hp_extra " -isystem \"${HP_SYSROOT}/include\" -L\"${HP_SYSROOT}/lib\"")
    else()
        set(_hp_extra " -isystem '${HP_SYSROOT}/include' -L'${HP_SYSROOT}/lib'")
    endif()
endif()

# _hp_shim(<basename> <zig subcommand> [extra flags baked into the shim])
function(_hp_shim name subcmd)
    set(_args "")
    foreach(_a IN LISTS ARGN)
        string(APPEND _args " ${_a}")
    endforeach()

    if(CMAKE_HOST_WIN32)
        set(_path "${HP_SHIM_DIR}/${name}.cmd")
        # `%*` forwards args verbatim; cmd propagates the child's exit code.
        file(WRITE "${_path}" "@echo off\r\n\"${HP_ZIG}\" ${subcmd}${_args} %*\r\n")
    else()
        set(_path "${HP_SHIM_DIR}/${name}")
        file(WRITE "${_path}" "#!/bin/sh\nexec '${HP_ZIG}' ${subcmd}${_args} \"$@\"\n")
        file(CHMOD "${_path}" PERMISSIONS
             OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()
    set(${name}_SHIM "${_path}" PARENT_SCOPE)
endfunction()

# --- Windows SDK header case forwarding --------------------------------------
#
# Diligent spells Windows SDK headers the way Microsoft documents them --
# <Windows.h>, <SDKDDKVer.h>, <D3D12.h> -- but MinGW-w64 ships every header
# lowercase. That is invisible on Windows and fatal when cross-compiling from a
# case-sensitive filesystem, so generate forwarding headers and put them last on
# the include path. third_party stays unpatched.
#
# Only when the filesystem is actually case-sensitive: where it is not,
# "Windows.h" and "windows.h" name the same file and a forwarder would include
# itself. Probed rather than inferred from the host OS, because macOS is
# case-insensitive by default and case-sensitive when the user chose that.
if(HP_TARGET MATCHES "windows")
    file(WRITE "${HP_SHIM_DIR}/hpcaseprobe" "")
    if(EXISTS "${HP_SHIM_DIR}/HPCASEPROBE")
        set(_hp_case_sensitive FALSE)
    else()
        set(_hp_case_sensitive TRUE)
    endif()
    file(REMOVE "${HP_SHIM_DIR}/hpcaseprobe")

    if(_hp_case_sensitive)
        # Refresh this list with tools/find_win_header_case.sh. Only the file
        # name differs in case -- the directory does not (GL/GL.h -> GL/gl.h).
        set(_hp_win_headers
            Windows.h SDKDDKVer.h VersionHelpers.h Unknwn.h ObjIdl.h Psapi.h
            Shlwapi.h ShellScalingApi.h D3D12.h D3Dcompiler.h DirectXMath.h
            Memory.h GL/GL.h Windowsx.h CommCtrl.h ShellAPI.h WinUser.h WinResrc.h)
        set(_hp_wininc "${HP_SHIM_DIR}/wininc")
        file(MAKE_DIRECTORY "${_hp_wininc}")
        foreach(_h IN LISTS _hp_win_headers)
            get_filename_component(_h_dir  "${_h}" DIRECTORY)
            get_filename_component(_h_name "${_h}" NAME)
            string(TOLOWER "${_h_name}" _h_lc)
            if(_h_dir)
                set(_h_target "${_h_dir}/${_h_lc}")
                file(MAKE_DIRECTORY "${_hp_wininc}/${_h_dir}")
            else()
                set(_h_target "${_h_lc}")
            endif()
            if(EXISTS "${HP_ZIG_LIB_DIR}/libc/include/any-windows-any/${_h_target}")
                file(WRITE "${_hp_wininc}/${_h}" "#include <${_h_target}>\n")
            endif()
        endforeach()
        # Last on the search path: a real header of the same name always wins.
        if(CMAKE_HOST_WIN32)
            string(APPEND _hp_extra " -isystem \"${_hp_wininc}\"")
        else()
            string(APPEND _hp_extra " -isystem '${_hp_wininc}'")
        endif()

        # Same problem for import libraries: Diligent links `Shlwapi`, MinGW
        # names it `shlwapi`. Zig synthesises import libraries from .def files
        # rather than shipping .a files, so there is nothing to symlink --
        # generate the capitalised import library with `zig dlltool` instead.
        #
        # Deliberately an explicit list, never a sweep: this directory is first
        # on the library search path, so generating (say) libSPIRV.a here would
        # shadow the real glslang target of that name and silently produce a
        # broken link.
        set(_hp_win_libs Shlwapi Version)
        set(_hp_winlib "${HP_SHIM_DIR}/winlib")
        file(MAKE_DIRECTORY "${_hp_winlib}")
        if(HP_TARGET MATCHES "^x86_64")
            set(_hp_dlltool_machine "i386:x86-64")
        elseif(HP_TARGET MATCHES "^aarch64")
            set(_hp_dlltool_machine "arm64")
        else()
            set(_hp_dlltool_machine "i386")
        endif()
        # _hp_import_lib(<system library name> <output file>)
        #   Builds an import library from MinGW's .def for that name. No-op if
        #   the output already exists or MinGW has no such library.
        function(_hp_import_lib name out)
            if(EXISTS "${out}")
                return()
            endif()
            string(TOLOWER "${name}" _lc)
            file(GLOB_RECURSE _defs "${HP_ZIG_LIB_DIR}/libc/mingw/lib*/${_lc}.def")
            # list(GET) on an empty list is a hard error, not an empty result.
            if(NOT _defs)
                return()
            endif()
            list(GET _defs 0 _def)
            execute_process(COMMAND "${HP_ZIG}" dlltool -m ${_hp_dlltool_machine}
                                    -d "${_def}" -l "${out}"
                            RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
            if(NOT _rc EQUAL 0)
                message(WARNING "could not generate import library ${out} from ${_def}")
            endif()
        endfunction()

        foreach(_l IN LISTS _hp_win_libs)
            _hp_import_lib("${_l}" "${_hp_winlib}/lib${_l}.a")
        endforeach()
        if(CMAKE_HOST_WIN32)
            string(APPEND _hp_extra " -L\"${_hp_winlib}\"")
        else()
            string(APPEND _hp_extra " -L'${_hp_winlib}'")
        endif()

        # Diligent also names some Windows system libraries MSVC-style, e.g.
        # `target_link_libraries(Diligent-Imgui PRIVATE dwmapi.lib)` under
        # MINGW_BUILD. CMake passes such an item through verbatim, and a bare
        # filename is opened relative to the working directory -- `-L` is not
        # consulted. Ninja runs every command from the build directory, so put
        # MSVC-named copies there.
        #
        # Only reachable non-D3D names are listed; the D3D ones sit behind
        # backend guards that are off for this toolchain.
        get_filename_component(_hp_build_root "${HP_SHIM_DIR}" DIRECTORY)
        foreach(_l dwmapi winmm shcore ninput opengl32 Shlwapi)
            _hp_import_lib("${_l}" "${_hp_build_root}/${_l}.lib")
        endforeach()
    endif()
endif()

# --- precompiled headers, ELF target, Windows host ---------------------------
#
# zig 0.16.0 cannot produce a precompiled header for an ELF target when it is
# itself running on Windows. Clang emits the PCH into zig's cache under an `.o`
# name, and zig then hands that file to its linker instead of treating it as the
# final artifact:
#
#     ld.lld: error: ...\.zig-cache\o\<hash>\cmake_pch.hxx.o: unknown file type
#
# Diligent's vendored glslang enables PCH, which is 99 of the Linux target's
# build rules -- so the whole target is unbuildable from a Windows host without
# this. Measured, not guessed (T0004); the deciding variables are the host and
# the target's object format, not the flags:
#
#   | host    | target          | -MD | result |
#   | Linux   | linux  (ELF)    | no  | fail   |
#   | Linux   | linux  (ELF)    | yes | pass   |
#   | Windows | linux  (ELF)    | no  | fail   |
#   | Windows | linux  (ELF)    | yes | fail   |
#   | either  | windows (COFF)  | -   | pass   |
#
# CMake always passes `-MD`, which is why a Linux host never hits this and the
# defect stayed invisible until the diagonal was first attempted. Scoped as
# tightly as the evidence allows: a Windows host is the only one affected, so a
# Linux host keeps its precompiled headers and its build times.
#
# Revisit when the zig pin moves -- this is an upstream defect, not a project
# one, and the cost here is compile time on one host/target pair.
if(CMAKE_HOST_WIN32 AND NOT HP_TARGET MATCHES "windows")
    set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON CACHE BOOL "" FORCE)
endif()

_hp_shim(zig-cc     "cc"     "-target ${HP_TARGET}${_hp_extra}")
_hp_shim(zig-cxx    "c++"    "-target ${HP_TARGET}${_hp_extra}")
_hp_shim(zig-ar     "ar")
_hp_shim(zig-ranlib "ranlib")

# --- compiler configuration --------------------------------------------------

set(CMAKE_C_COMPILER   "${zig-cc_SHIM}"     CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${zig-cxx_SHIM}"    CACHE FILEPATH "" FORCE)
set(CMAKE_AR           "${zig-ar_SHIM}"     CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB       "${zig-ranlib_SHIM}" CACHE FILEPATH "" FORCE)

# zig cc is clang; say so up front so CMake skips a compiler-probe round trip.
set(CMAKE_C_COMPILER_ID   Clang)
set(CMAKE_CXX_COMPILER_ID Clang)

# Zig links via lld internally. Letting CMake also pass -fuse-ld=... would only
# confuse it, so pin the linker choice here.
set(CMAKE_C_USING_LINKER_MODE   FLAG)
set(CMAKE_CXX_USING_LINKER_MODE FLAG)

# --- WHOLE_ARCHIVE link feature ----------------------------------------------
#
# Diligent links its static engine libraries into the GraphicsEngine* shared
# libraries with $<LINK_LIBRARY:WHOLE_ARCHIVE,...>. CMake implements that on
# GNU-like linkers as `--push-state,--whole-archive ... --pop-state`, which Zig's
# linker driver rejects outright:
#
#     error: unsupported linker arg: --push-state
#
# CMake decides between that and a plain `--whole-archive/--no-whole-archive`
# pair by running `${CMAKE_LINKER} --push-state --pop-state` -- but the binary it
# probes is the host `ld`, which does support the option, while linking actually
# goes through `zig cc`, which does not. So the probe answers for the wrong
# program.
#
# Platform/Linker/GNU.cmake only probes `if(NOT DEFINED ...)`, so answering ahead
# of time both skips the bogus probe and selects CMake's own fallback form.
# Declared for the bare name and per language, matching how that module builds
# the variable name.
foreach(_v LINKER C_LINKER CXX_LINKER)
    set(CMAKE_${_v}_PUSHPOP_STATE_SUPPORTED FALSE)
endforeach()

# --- find() behaviour --------------------------------------------------------
#
# LIBRARY/INCLUDE are restricted to the sysroot so a Linux-host build cannot
# silently pick up /usr/lib and lose reproducibility. PROGRAM stays on the host
# (SPIRV-Tools shells out to the host python during generation) and PACKAGE is
# left at its BOTH default for the same reason.
if(HP_SYSROOT)
    set(CMAKE_FIND_ROOT_PATH "${HP_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

    # find_package(X11)/find_package(OpenGL) hand back absolute paths into the
    # sysroot, and CMake adds the directory of every such library to the build
    # RPATH. That would be actively harmful here: the sysroot holds link-time
    # *stubs* with empty function bodies, so a binary that found them first at
    # runtime would call into nothing.
    #
    # Listing the directory as an implicit link directory makes CMake leave it
    # out of the RPATH, exactly as it does for /usr/lib. The loader then resolves
    # libX11.so.6 and friends from the user's system by SONAME, as intended.
    list(APPEND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${HP_SYSROOT}/lib")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
