// Makes a gameplay module survive being unloaded (T0105.1).
//
// This file is compiled into every gameplay module by `hp_add_gameplay_module()`
// rather than being something a module author remembers to include. Forgetting
// it produces a process that segfaults at *exit*, arbitrarily long after the
// unload that caused it, with a stack pointing at unmapped memory — which is
// exactly the class of failure that must not be opt-in.
//
// The problem is a toolchain bug, not a C++ rule. `zig cc -shared` does not link
// `crtbeginS.o`/`crtendS.o`, so a module built with it has **no `.fini_array`
// at all** while still importing `__cxa_atexit` and defining `__dso_handle`. It
// registers destructors for its statics and has nothing that retires them. On
// `dlclose` the code is unmapped with those registrations still live, and the
// process dies when the C runtime walks them at exit.
//
// Measured, same source, only the compiler driver differing:
//
//   zig cc -shared   -> no .fini_array  -> dlclose then exit = SIGSEGV
//   host g++ -shared -> .fini_array     -> dlclose then exit = clean
//
// Upstream is ziglang/zig#17908, open since 2023 with no fix. Its suggested
// workaround is to borrow the host GCC's crtbeginS.o, which would break the
// hermetic build (D5) — and zig ships no Linux crtbegin to borrow instead.
//
// Declaring any destructor is what makes the linker emit `.fini_array`, and the
// dynamic loader calls it through `DT_FINI_ARRAY` without needing crtbegin. So
// one destructor that finalizes this DSO's own registrations is sufficient, and
// is source-only.
//
// Verified over 50 load/unload cycles with the host passing neither
// RTLD_NODELETE nor -z,nodelete. See tests/integration/module_boundary_test.cpp,
// which fails if this file stops working.
//
// Windows needs none of this: LoadLibrary/FreeLibrary of the same construct was
// measured clean over 25 cycles. The defect is ELF-only.

#if defined(__ELF__)

extern "C" {

// Provided by the toolchain; unique per shared object.
extern void* __dso_handle;

// Runs every destructor this DSO registered with __cxa_atexit, and — the part
// that matters — removes them from the global list, so nothing walks into the
// unmapped mapping afterwards.
int __cxa_finalize(void*);

} // extern "C"

namespace {

// Priority left unspecified deliberately. This must run *after* the module's
// own static destructors, and the default (last registered, first run) already
// orders it correctly relative to them; pinning a priority would only create a
// way to get it wrong.
__attribute__((destructor)) void hpModuleFinalize() {
    __cxa_finalize(&__dso_handle);
}

} // namespace

#endif // __ELF__
