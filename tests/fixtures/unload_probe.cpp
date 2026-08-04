// Loads a module, uses it, unloads it, and exits — as a separate process.
//
// A separate process is not incidental. The failure T0105.1 is about happens at
// *process exit*, when the C runtime walks destructor registrations belonging to
// a mapping that is no longer there. A test that triggered it in-process would
// take the whole doctest binary down with it and report nothing useful, so the
// only way to assert on it is to run it somewhere else and look at the exit
// status.
//
// Crucially this does **not** pass RTLD_NODELETE. The boundary suite's own
// loader does, which is what has kept this latent: with NODELETE the mapping
// survives, the registrations stay valid, and an "unload" is not really an
// unload. This probe performs a real one.
//
// Usage: hp_unload_probe <module-path> [cycles]
// Exit:  0 = loaded, used and unloaded cleanly, and exited cleanly
//        1 = load failed        2 = symbol missing or wrong value
//        3 = unload failed      (a signal death is the failure being tested for)

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: hp_unload_probe <module-path> [cycles]\n");
        return 64;
    }
    const char* path = argv[1];
    const int cycles = (argc > 2) ? std::atoi(argv[2]) : 1;

    for (int i = 0; i < cycles; ++i) {
#if defined(_WIN32)
        HMODULE h = LoadLibraryA(path);
        if (!h) {
            std::fprintf(stderr, "cycle %d: LoadLibraryA failed, error %lu\n", i, GetLastError());
            return 1;
        }
        auto fn = reinterpret_cast<const char* (*)()>(
            reinterpret_cast<void*>(GetProcAddress(h, "hp_unload_fixture_value")));
#else
        // No RTLD_NODELETE: this is a real unload, which is the point.
        void* h = ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            const char* e = ::dlerror();
            std::fprintf(stderr, "cycle %d: %s\n", i, e ? e : "dlopen failed");
            return 1;
        }
        auto fn = reinterpret_cast<const char* (*)()>(::dlsym(h, "hp_unload_fixture_value"));
#endif
        if (!fn || std::strcmp(fn(), "hello") != 0) {
            std::fprintf(stderr, "cycle %d: symbol missing or wrong value\n", i);
            return 2;
        }

#if defined(_WIN32)
        if (!FreeLibrary(h)) {
            std::fprintf(stderr, "cycle %d: FreeLibrary failed\n", i);
            return 3;
        }
#else
        if (::dlclose(h) != 0) {
            std::fprintf(stderr, "cycle %d: dlclose failed\n", i);
            return 3;
        }
#endif
    }

    // Returning from main is the moment of truth: the C runtime now walks every
    // registered destructor, including any belonging to a mapping that has been
    // unmapped. Reaching the end of this function is not success — exiting is.
    std::printf("%d cycle(s) clean\n", cycles);
    return 0;
}
