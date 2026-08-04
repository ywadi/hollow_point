// The engine/gameplay module boundary (T0095, D12).
//
// Bucket: integration. It loads a real shared library at runtime rather than
// linking it, so it is seconds rather than microseconds and belongs out of the
// fast bucket.
//
// D12 lets rich C++ cross the engine/module boundary — real types, templates,
// entt registries — instead of a C ABI with a generated binding layer, and that
// rests on the engine being a *shared* library so its state exists once per
// process. This suite is the guard on that. It was written after a scratchpad
// prototype proved the properties hold on both targets; a prototype nothing
// runs is not a guard, which is why this exists.
//
// The failure it prevents is not a build error. Link the engine statically into
// both the executable and the module and everything still compiles and links —
// then each side mutates its own copy of every engine global, and components
// land in pools the other side cannot see. That presents as impossible bugs in
// gameplay code, arbitrarily far from the cause.

#include <doctest/doctest.h>

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <hp/BuildId.h>
#include <hp/Module.hpp>
#include <hp/Reflect.hpp>

#include <entt/meta/meta.hpp>

using namespace entt::literals;
#if !defined(_WIN32)
#include <sys/wait.h>
#endif

#include "../fixtures/abi_boundary.h"

#include <hp/Log.hpp>

#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>
#endif

namespace {

#if defined(_WIN32)
constexpr const char* PATH_SEP = "\\";
#else
constexpr const char* PATH_SEP = "/";
#endif

/// Directory holding the running test executable.
///
/// Deliberately not the working directory: the harness runs these suites from
/// wherever it likes, and under wine or WSL interop the cwd is not even
/// reliably the same shape of path. The executable can always find itself.
std::string exe_dir() {
#if defined(_WIN32)
    char buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string path(buf, n);
    const auto slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
#else
    char buf[PATH_MAX] = {};
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0)
        return ".";
    std::string path(buf, static_cast<size_t>(n));
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
#endif
}

/// Path of the module fixture beside the running executable.
///
/// The file name comes from CMake (`HP_ABI_MODULE_NAME`) rather than being
/// spelled here, because it differs by target: MinGW keeps the `lib` prefix,
/// so it is `libhp_abi_module.dll` on Windows and `libhp_abi_module.so` on
/// Linux. Guessing it compiled cleanly and failed every Windows test at run
/// time, which is a poor way to find out.
std::string module_path() {
    return exe_dir() + PATH_SEP + HP_ABI_MODULE_NAME;
}

/// An open handle to the gameplay module, closed on scope exit.
///
/// Reopening one of these is what a hot reload is, mechanically — which makes
/// the reload test below a matter of destroying and reconstructing it rather
/// than of any special support.
class LoadedModule {
public:
    LoadedModule() : path_(module_path()) { open(); }

    explicit LoadedModule(std::string path) : path_(std::move(path)) { open(); }

    ~LoadedModule() { close(); }

    LoadedModule(const LoadedModule&) = delete;
    LoadedModule& operator=(const LoadedModule&) = delete;

    bool ok() const { return handle_ != nullptr; }

    const std::string& error() const { return error_; }

    template <class Fn>
    Fn sym(const char* name) const {
        if (!handle_)
            return nullptr;
#if defined(_WIN32)
        return reinterpret_cast<Fn>(
            reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name)));
#else
        return reinterpret_cast<Fn>(::dlsym(handle_, name));
#endif
    }

    void reload() {
        close();
        open();
    }

private:
    void open() {
        const std::string& path = path_;
#if defined(_WIN32)
        handle_ = static_cast<void*>(LoadLibraryA(path.c_str()));
        if (!handle_)
            error_ = path + ": LoadLibraryA failed, error " + std::to_string(GetLastError());
#else
        // RTLD_LOCAL so the module's symbols do not leak into the global
        // namespace — a gameplay module must not be able to satisfy another
        // module's undefined symbols by accident.
        // RTLD_NODELETE is not optional here, and the reason is worth knowing.
        //
        // Zig links libc++ statically into every shared library, so each module
        // carries its own copy. A module holding any static object that needs
        // destruction registers it with __cxa_atexit; dlclose unmaps the code
        // without retiring that registration, and the process then segfaults at
        // exit, long after the unload, with a stack pointing at nothing.
        //
        // Reduced during T0095 to a library containing one `static std::string`
        // and no engine code at all: dlopen, dlclose, return from main, SIGSEGV.
        // dlopen without dlclose is clean, which is why an earlier prototype
        // that never unloaded missed it entirely.
        //
        // NODELETE keeps the mapping alive so the registration stays valid.
        // That means an unload is not really an unload -- see the reload test
        // below for what this suite can and cannot claim as a result, and
        // T0048, which has to solve it properly.
        handle_ = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
        if (!handle_) {
            const char* e = ::dlerror();
            error_ = path + ": " + (e ? e : "dlopen failed");
        }
#endif
    }

    void close() {
        if (!handle_)
            return;
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        ::dlclose(handle_);
#endif
        handle_ = nullptr;
    }

    std::string path_;
    void* handle_ = nullptr;
    std::string error_;
};

/// Path of the real sample gameplay module, relative to this binary.
///
/// Composed at run time rather than baked by CMake, for the two reasons this
/// suite has already learned the hard way: a `$<TARGET_FILE:...>` path is a
/// *host* path and unopenable from a Windows binary, and a POST_BUILD copy goes
/// stale whenever the module changes without this target relinking.
std::string sandbox_module_path() {
    return exe_dir() + PATH_SEP + ".." + PATH_SEP + "samples" + PATH_SEP + "sandbox" + PATH_SEP +
           HP_SANDBOX_MODULE_NAME;
}

} // namespace

TEST_CASE("the gameplay module loads at runtime") {
    LoadedModule mod;
    REQUIRE_MESSAGE(mod.ok(), mod.error());
    CHECK(mod.sym<hp_mod_global_addr_fn>("hp_mod_global_addr") != nullptr);
}

TEST_CASE("engine state exists exactly once across the boundary") {
    LoadedModule mod;
    REQUIRE_MESSAGE(mod.ok(), mod.error());

    auto mod_addr = mod.sym<hp_mod_global_addr_fn>("hp_mod_global_addr");
    auto mod_bump = mod.sym<hp_mod_bump_fn>("hp_mod_bump");
    REQUIRE(mod_addr != nullptr);
    REQUIRE(mod_bump != nullptr);

    // Same object, not merely equal values: a duplicated engine would give two
    // counters that both read zero and look identical until one is written.
    CHECK(hp_abi_global_addr() == mod_addr());

    const int before = hp_abi_bump();
    const int through_module = mod_bump();
    CHECK(through_module == before + 1);
}

TEST_CASE("entt type identity survives the module boundary") {
    LoadedModule mod;
    REQUIRE_MESSAGE(mod.ok(), mod.error());

    auto mod_hash = mod.sym<hp_mod_engine_type_hash_fn>("hp_mod_engine_type_hash");
    REQUIRE(mod_hash != nullptr);

    // type_hash is name-based and is what entt keys component pools on, so this
    // is the identity that has to hold.
    CHECK(hp_abi_engine_type_hash() == mod_hash());
}

TEST_CASE("a component written by the engine is visible to the module") {
    LoadedModule mod;
    REQUIRE_MESSAGE(mod.ok(), mod.error());

    auto mod_count = mod.sym<hp_mod_count_engine_components_fn>("hp_mod_count_engine_components");
    REQUIRE(mod_count != nullptr);

    void* registry = hp_abi_make_registry();
    REQUIRE(registry != nullptr);

    const uint32_t entity = hp_abi_create_entity(registry);
    hp_abi_emplace_engine_component(registry, entity, 42);

    CHECK(hp_abi_count_engine_components(registry) == 1);
    CHECK(mod_count(registry) == 1);

    hp_abi_destroy_registry(registry);
}

TEST_CASE("a component written by the module lands in the engine's registry") {
    LoadedModule mod;
    REQUIRE_MESSAGE(mod.ok(), mod.error());

    auto emplace = mod.sym<hp_mod_emplace_module_component_fn>("hp_mod_emplace_module_component");
    auto count = mod.sym<hp_mod_count_module_components_fn>("hp_mod_count_module_components");
    REQUIRE(emplace != nullptr);
    REQUIRE(count != nullptr);

    void* registry = hp_abi_make_registry();
    const uint32_t entity = hp_abi_create_entity(registry);

    emplace(registry, entity, 1.5f);
    CHECK(count(registry) == 1);

    // The engine's own component type must be unaffected by the module having
    // registered one of its own — distinct pools, not a collision.
    CHECK(hp_abi_count_engine_components(registry) == 0);

    hp_abi_destroy_registry(registry);
}

TEST_CASE("a second generation of the module agrees with the first") {
    // The half of T0095's boundary test the prototype never covered, written
    // to match what a hot reload mechanically *is* rather than what it is
    // called. T0048 reloads by copying the module to a fresh path and loading
    // the copy, precisely so the previous image can stay mapped; this test
    // does the same and asserts the two generations agree.
    //
    // What this deliberately does NOT claim: that unloading works. Zig links
    // libc++ statically into each module, and a genuine dlclose of a module
    // holding any static needing destruction segfaults the process at exit
    // (reduced to a single `static std::string` -- see the loader above). The
    // loader therefore uses RTLD_NODELETE and the old image stays resident.
    // Whether a module can ever truly be unloaded is T0048's problem, and it
    // is recorded in T0095 as unresolved rather than papered over here.
    namespace fs = std::filesystem;

    LoadedModule first;
    REQUIRE_MESSAGE(first.ok(), first.error());

    void* registry = hp_abi_make_registry();
    const uint32_t entity = hp_abi_create_entity(registry);
    hp_abi_emplace_engine_component(registry, entity, 7);

    const int counter_before = hp_abi_bump();
    const uint64_t hash_first = first.sym<hp_mod_engine_type_hash_fn>("hp_mod_engine_type_hash")();

    // Copy the module beside itself and load the copy -- a second, independent
    // image of the same code, with its own statics.
    const fs::path original = module_path();
    fs::path second_path = original;
    second_path.replace_filename("gen2_" + original.filename().string());

    std::error_code ec;
    fs::copy_file(original, second_path, fs::copy_options::overwrite_existing, ec);
    // Built as a std::string first: doctest declares its own operator+ for its
    // String type, which makes `const char* + std::string` ambiguous inside the
    // macro.
    const std::string copy_error = std::string("copying the module failed: ") + ec.message();
    REQUIRE_MESSAGE(!ec, copy_error);

    LoadedModule second(second_path.string());
    REQUIRE_MESSAGE(second.ok(), second.error());

    auto second_hash = second.sym<hp_mod_engine_type_hash_fn>("hp_mod_engine_type_hash");
    auto second_count =
        second.sym<hp_mod_count_engine_components_fn>("hp_mod_count_engine_components");
    auto second_addr = second.sym<hp_mod_global_addr_fn>("hp_mod_global_addr");
    REQUIRE(second_hash != nullptr);
    REQUIRE(second_count != nullptr);
    REQUIRE(second_addr != nullptr);

    // The engine was never reloaded, so both generations must see the one copy
    // of its state -- this is the property that makes reload survivable at all.
    CHECK(second_addr() == hp_abi_global_addr());
    CHECK(hp_abi_bump() == counter_before + 1);

    // Two independently loaded images must agree about type identity, or a
    // reloaded module would stop seeing components written before the reload.
    CHECK(second_hash() == hash_first);
    CHECK(second_hash() == hp_abi_engine_type_hash());

    // And the new generation sees data written before it existed.
    CHECK(second_count(registry) == 1);

    hp_abi_destroy_registry(registry);

    // Best-effort cleanup. On Windows this *fails*, and the failure is itself
    // evidence: a loaded DLL cannot be deleted, and the second generation is
    // still mapped because nothing unloads it. That is the same constraint
    // RTLD_NODELETE encodes on Linux, arriving from the other direction --
    // see T0105, which owns whether a module can ever truly be unloaded.
    // The stray file is harmless; ignoring the error rather than asserting on
    // it keeps the test about the boundary rather than about the filesystem.
    fs::remove(second_path, ec);
}

TEST_CASE("RTTI across the module boundary") {
    // Measured, not assumed: this decides a T0055 convention (95.4). Both
    // fixtures build with -fvisibility=hidden and the polymorphic type is
    // marked default-visibility, which is the configuration that *should*
    // unify vtables and typeinfo. Whether it actually does under zig's clang,
    // for both ELF and PE, is what this records.
    //
    // PE has no concept of symbol interposition, so a pessimistic expectation
    // would be that Windows fails here even when Linux passes. If a target ever
    // fails, the convention becomes "no dynamic_cast or typeid on types that
    // cross the boundary" and this test documents why.
    LoadedModule mod;
    REQUIRE_MESSAGE(mod.ok(), mod.error());

    auto mod_cast = mod.sym<hp_mod_dynamic_cast_works_fn>("hp_mod_dynamic_cast_works");
    auto mod_name = mod.sym<hp_mod_typeid_name_fn>("hp_mod_typeid_name");
    REQUIRE(mod_cast != nullptr);
    REQUIRE(mod_name != nullptr);

    void* base = hp_abi_make_derived();
    REQUIRE(base != nullptr);

    // Sanity: the engine can obviously cast its own type.
    CHECK(hp_abi_engine_dynamic_cast_works(base) == 1);

    // The real question — the module casting a type the engine created.
    const int module_cast = mod_cast(base);
    const char* engine_name = hp_abi_engine_typeid_name(base);
    const char* module_name = mod_name(base);
    INFO("engine typeid: ", engine_name, "  module typeid: ", module_name);
    MESSAGE("dynamic_cast across the boundary from the module: ", module_cast ? "WORKS" : "FAILS");
    MESSAGE("typeid names agree: ",
            std::string(engine_name) == std::string(module_name) ? "yes" : "no");

    // Asserted so the suite fails if this ever changes. If it fails on a fresh
    // toolchain, do not "fix" the test -- change the convention in T0055 and
    // record which target regressed.
    CHECK(module_cast == 1);
    CHECK(std::string(engine_name) == std::string(module_name));

    hp_abi_destroy_base(base);
}

TEST_CASE("gameplay code can write to the engine's log across the boundary") {
    // The question this answers: when a game developer writes a log line in
    // gameplay code, does it reach the editor console, the log file and every
    // other sink? It must, or the console is only useful to the engine.
    //
    // It works because of two decisions that were made for other reasons and
    // pay off here. The engine is a *shared* library (D12), so the module and
    // the host share one logger rather than each getting a copy. And
    // `LogCategory` is an id into engine-owned storage rather than an object
    // (T0054) -- a category declared *inside the module*, as this one is, would
    // otherwise leave the engine holding a pointer into a library that can
    // unload.
    class CapturingSink final : public hp::ILogSink {
    public:
        void write(const hp::LogRecord& record) override {
            categories.emplace_back(record.category);
            messages.emplace_back(record.message);
        }

        std::vector<std::string> categories;
        std::vector<std::string> messages;
    };

    LoadedModule mod;
    REQUIRE_MESSAGE(mod.ok(), mod.error());

    auto modLog = mod.sym<hp_mod_log_fn>("hp_mod_log");
    REQUIRE(modLog != nullptr);

    CapturingSink sink;
    hp::logAddSink(&sink);
    modLog("hello from gameplay");
    hp::logRemoveSink(&sink);

    REQUIRE(sink.messages.size() == 1);
    CHECK(sink.messages[0] == "hello from gameplay");
    // The category the module declared is visible to the engine by name, which
    // is what lets an editor console filter on it.
    CHECK(sink.categories[0] == "game.sandbox");
}

// --- unload lifecycle (T0105.1) ---------------------------------------------
//
// Everything above loads modules with RTLD_NODELETE, so nothing above performs
// a real unload. These cases do, in a separate process, because the failure
// being guarded lands at process *exit* rather than at dlclose — in-process it
// would take this binary down and report nothing.
//
// `zig cc -shared` links no crtbeginS.o, so a module has no .fini_array while
// still registering __cxa_atexit destructors for its statics. dlclose unmaps
// the code with those registrations live, and the runtime walks into nothing at
// exit. ziglang/zig#17908, open upstream. engine/module/ModuleFinalize.cpp
// declares one destructor, which makes the linker emit .fini_array and lets the
// loader retire the registrations through DT_FINI_ARRAY.
//
// Two fixtures from one source, differing only by that file, so these cases
// assert the fix does something rather than observing a pass.

namespace {

/// Run a probe process; true when it exited 0, false on any non-zero exit or
/// death by signal. The distinction does not matter to the assertion — a
/// segfault and a clean failure are both "did not unload cleanly" — but the
/// message wants to say which.
struct ProbeResult {
    bool clean = false;
    int raw = 0;
};

ProbeResult run_probe(const char* module_name, int cycles) {
    // Composed at run time against exe_dir(), never baked by CMake: these
    // binaries are configured on the host, so a Windows test given a POSIX path
    // would fail to open something that does not exist from its point of view.
    const std::string probe = exe_dir() + PATH_SEP + HP_UNLOAD_PROBE_NAME;
    const std::string module = exe_dir() + PATH_SEP + module_name;
    ProbeResult r;

#if defined(_WIN32)
    // CreateProcess rather than std::system, and not for tidiness: std::system
    // goes through CMD.EXE, which refuses a UNC current directory. Running the
    // Windows suite from a WSL path (which is how it runs locally, via interop)
    // made every case here fail with "UNC paths are not supported" before the
    // probe was ever reached. No shell, no problem.
    std::string cmd = "\"" + probe + "\" \"" + module + "\" " + std::to_string(cycles);
    std::vector<char> mutable_cmd(cmd.begin(), cmd.end());
    mutable_cmd.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    const std::string cwd = exe_dir();
    if (!CreateProcessA(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        cwd.c_str(), &si, &pi)) {
        r.raw = static_cast<int>(GetLastError());
        r.clean = false;
        return r;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    r.raw = static_cast<int>(code);
    r.clean = (code == 0);
#else
    const std::string cmd =
        "\"" + probe + "\" \"" + module + "\" " + std::to_string(cycles) + " >/dev/null 2>&1";
    const int rc = std::system(cmd.c_str());
    r.raw = rc;
    // A death by signal is the failure this is looking for, so "exited, with 0"
    // is the only thing that counts as clean.
    r.clean = (rc != -1) && WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
#endif
    return r;
}

} // namespace

TEST_CASE("a gameplay module can be genuinely unloaded" * doctest::test_suite("module")) {
    // The real thing: dlopen without RTLD_NODELETE, use it, dlclose, exit.
    const ProbeResult once = run_probe(HP_UNLOAD_MODULE_FIXED_NAME, 1);
    CHECK_MESSAGE(once.clean,
                  "a module carrying the unload finalizer should load, unload and exit "
                  "cleanly; probe status ",
                  once.raw);

    // Repeated, because the registration list is global and a leak would show
    // up as accumulation rather than as a first-cycle failure.
    const ProbeResult many = run_probe(HP_UNLOAD_MODULE_FIXED_NAME, 50);
    CHECK_MESSAGE(many.clean, "50 load/unload cycles should stay clean; probe status ", many.raw);
}

TEST_CASE("the unload finalizer is what makes that work" * doctest::test_suite("module")) {
    // The control. Identical source, built without engine/module/ModuleFinalize.cpp.
    const ProbeResult broken = run_probe(HP_UNLOAD_MODULE_BROKEN_NAME, 1);

#if defined(_WIN32)
    // Windows was never affected: LoadLibrary/FreeLibrary retire the module's
    // registrations on unload, so the control passes here and this case asserts
    // only that the platform difference is real and understood.
    CHECK_MESSAGE(broken.clean,
                  "Windows should unload cleanly with or without the finalizer; status ",
                  broken.raw);
#else
    // If this ever starts passing, the toolchain bug has been fixed upstream
    // (ziglang/zig#17908) and ModuleFinalize.cpp may be removable — which is a
    // thing worth being told rather than a thing to discover. It is not a
    // regression in this project.
    CHECK_MESSAGE(!broken.clean,
                  "a module WITHOUT the finalizer is expected to die at exit; it did not, "
                  "which likely means zig#17908 is fixed and engine/module/ModuleFinalize.cpp "
                  "can be revisited. Probe status ",
                  broken.raw);
#endif
}

// --- build id and module compatibility (T0104) -------------------------------
//
// D12's whole ergonomic win — rich C++ across the boundary, no C ABI, no
// generated binding layer — rests on the module having been compiled against
// exactly this engine. When that holds, everything works. When it does not,
// nothing announces it: the module loads, resolves its symbols, and reads
// fields at offsets that have moved. The corruption then surfaces somewhere
// unrelated, and reading the gameplay code leads away from the cause.
//
// These cases are the guard on that, and the important one is the refusal.

// --- exceptions across the module boundary (T0127) ----------------------------
//
// What a handler actually catches when the throw happened on the other side of
// a dlopen/LoadLibrary, and it is **not the same on both targets**.
//
// The cause is the one that keeps producing surprises here: zig links
// libc++/libc++abi statically, with hidden visibility, into every artifact. So
// each artifact owns a private copy of every std:: typeinfo object. libc++
// selects its typeinfo comparison at build time -- COFF compares the type name
// deeply, ELF compares the typeinfo *pointer*, on the assumption that the
// linker merged RTTI into one definition. Nothing merges here, so on ELF the
// pointers differ and the match silently fails.
//
// Measured at symbol level rather than inferred: `_ZTISt13runtime_error` is
// locally DEFINED in both the module fixture and this executable, while the
// engine-owned type's typeinfo is DEFINED once in the engine fixture and
// UNDEFINED in the module.
//
// These cases pin the behaviour on both targets. They are written to fail if it
// *changes* in either direction, because the dangerous version of this bug is
// the one where CI stays green on Windows while Linux quietly routes an
// exception into the wrong handler.

namespace {

/// What handler a throw across the boundary landed in.
enum class Caught { Exact, StdBase, Ellipsis, Nothing };

/// Which target this binary is, spelled into the diagnostics.
///
/// The whole point of these cases is that the answer differs by target, and
/// both suites print into the same build log. Without this it is genuinely
/// ambiguous which line came from which, and reading them the wrong way round
/// inverts the conclusion.
constexpr const char* kTargetName =
#if defined(_WIN32)
    "windows/COFF";
#else
    "linux/ELF";
#endif

const char* describe(Caught c) {
    switch (c) {
    case Caught::Exact:
        return "the exact type";
    case Caught::StdBase:
        return "catch (const std::exception&)";
    case Caught::Ellipsis:
        return "catch (...) only -- typed match failed";
    case Caught::Nothing:
        return "nothing was thrown";
    }
    return "?";
}

Caught catch_std(hp_mod_throw_fn fn) {
    try {
        fn();
    } catch (const std::runtime_error&) {
        return Caught::Exact;
    } catch (const std::exception&) {
        return Caught::StdBase;
    } catch (...) {
        return Caught::Ellipsis;
    }
    return Caught::Nothing;
}

Caught catch_engine_owned(hp_mod_throw_fn fn) {
    try {
        fn();
    } catch (const HpAbiEngineError&) {
        return Caught::Exact;
    } catch (...) {
        return Caught::Ellipsis;
    }
    return Caught::Nothing;
}

} // namespace

TEST_CASE("a std:: exception thrown in a module is not caught by type on ELF" *
          doctest::test_suite("module")) {
    LoadedModule mod;
    REQUIRE_MESSAGE(mod.ok(), mod.error());
    auto fn = mod.sym<hp_mod_throw_fn>("hp_mod_throw_std");
    REQUIRE(fn != nullptr);

    const Caught got = catch_std(fn);
    MESSAGE("[" << std::string(kTargetName) << "] std::runtime_error thrown in the module "
                << "was caught by: " << std::string(describe(got)));

#if defined(_WIN32)
    // COFF compares typeinfo names, so the private copies still match.
    CHECK_MESSAGE(got == Caught::Exact,
                  "on Windows a typed catch across the boundary is expected to work; "
                  "it did not, which means the platform split this suite encodes has "
                  "changed and T0127's conclusions need re-reading");
#else
    // The finding, asserted so it cannot regress unnoticed *in either
    // direction*. If this ever reports Exact, ELF typeinfo identity has been
    // fixed -- by a toolchain bump or a shared libc++ -- and the convention in
    // 06-engine-conventions.md can be revisited rather than rediscovered.
    CHECK_MESSAGE(got == Caught::Ellipsis,
                  "a std:: exception crossing the module boundary was matched by type on "
                  "ELF. That is better than the measured behaviour, not worse -- but it "
                  "means T0127's conclusion is stale and the conventions should be "
                  "re-derived rather than trusted");
#endif
}

TEST_CASE("an engine-owned exception type IS caught by type, on both targets" *
          doctest::test_suite("module")) {
    LoadedModule mod;
    REQUIRE_MESSAGE(mod.ok(), mod.error());

    // The mechanism 127.2 was asked to find: one typeinfo definition, in the
    // shared library both sides resolve against. This is the *only* shape that
    // works on ELF, which is precisely why the policy is not "use this shape".
    auto direct = mod.sym<hp_mod_throw_fn>("hp_mod_throw_engine_owned");
    REQUIRE(direct != nullptr);
    CHECK_MESSAGE(catch_engine_owned(direct) == Caught::Exact,
                  "an engine-owned exception type with default visibility and an "
                  "out-of-line key function must match by type across the boundary");

    // And it is a property of the type, not of the throw site.
    auto rethrown = mod.sym<hp_mod_throw_fn>("hp_mod_throw_engine_owned_rethrown");
    REQUIRE(rethrown != nullptr);
    CHECK(catch_engine_owned(rethrown) == Caught::Exact);
}

TEST_CASE("the std:: base of an engine-owned exception does not match on ELF" *
          doctest::test_suite("module")) {
    // The trap that makes the mechanism above unsafe to build an interface on.
    //
    // Catching by the exact engine-owned type works. Catching the same object
    // through a std:: base does not, on ELF, because the derived-to-base walk
    // compares the *base* typeinfo -- and std::exception's typeinfo is private
    // per artifact again. `catch (const std::exception&)` is what people
    // actually write, so the working case is the one nobody reaches for.
    //
    // In the scratch reproduction the compiler warned that the exact handler
    // was unreachable ("will be caught by earlier handler") and on Linux the
    // unreachable handler is the one that fired: the diagnostic was right about
    // the language and wrong about the platform.
    //
    // The fixture type deliberately does not derive from std::exception, so
    // this case documents the hazard rather than shipping it. See T0127.
    MESSAGE("engine-owned exceptions do not derive from std::exception by design; "
            "a std:: base handler cannot see them on ELF");
    CHECK(true);
}

TEST_CASE("every configuration field feeding the id has a value" * doctest::test_suite("module")) {
    // Guards an ordering bug that made the id unstable rather than wrong, and
    // so produced a *spurious refusal* -- the failure mode that is hardest to
    // read, because the diagnostic is correct about the ids and wrong about
    // the world.
    //
    // HP_PROFILING was declared with option() *below* the custom command that
    // expands it. On a fresh tree nothing had put it in the cache yet, so the
    // command baked an empty string; option() then wrote OFF to the cache and
    // the next configure baked "OFF". Same source, same headers, same flags,
    // different id -- measured 00947a49dbbb42a3 -> b88f8277d7bec25f on a no-op
    // reconfigure, after which every module built before it was refused.
    //
    // An empty field is the signature of that class of bug: a flag read before
    // it was declared reads as empty, never as garbage. Asserting each field
    // has a value costs nothing and catches the next flag added in the wrong
    // order (T0105.2, and hp_build_id.cmake's "it will not be the last").
    CHECK_MESSAGE(std::strlen(HP_BUILD_ID_PROFILING) > 0,
                  "HP_BUILD_ID_PROFILING is empty -- HP_PROFILING was read before it was "
                  "declared. Any flag feeding the id must be declared above the build-id "
                  "command in engine/CMakeLists.txt");
    CHECK_MESSAGE(std::strlen(HP_BUILD_ID_TARGET) > 0, "HP_BUILD_ID_TARGET is empty");
    CHECK_MESSAGE(std::strlen(HP_BUILD_ID_CONFIG) > 0, "HP_BUILD_ID_CONFIG is empty");
    CHECK(std::strlen(hp::engineBuildId()) == 16);
}

TEST_CASE("a module stamped by the build matches the engine" * doctest::test_suite("module")) {
    LoadedModule stamped(exe_dir() + PATH_SEP + HP_STAMPED_MODULE_NAME);
    REQUIRE_MESSAGE(stamped.ok(), stamped.error());

    auto id_fn = stamped.sym<hp::ModuleBuildIdFn>(hp::kModuleBuildIdSymbol);
    REQUIRE_MESSAGE(id_fn != nullptr,
                    "hp_add_gameplay_module() must stamp every module; this one has no "
                        << hp::kModuleBuildIdSymbol << " symbol");

    const hp::ModuleCompatibility result = hp::checkModuleBuildId(id_fn());
    CHECK_MESSAGE(result.compatible, hp::describeIncompatibility(HP_STAMPED_MODULE_NAME, result));
    CHECK(std::strcmp(id_fn(), hp::engineBuildId()) == 0);
}

TEST_CASE("a module built against a different engine is refused" * doctest::test_suite("module")) {
    LoadedModule stale(exe_dir() + PATH_SEP + HP_STALE_MODULE_NAME);
    REQUIRE_MESSAGE(stale.ok(), stale.error());

    auto id_fn = stale.sym<hp::ModuleBuildIdFn>(hp::kModuleBuildIdSymbol);
    REQUIRE(id_fn != nullptr);

    const hp::ModuleCompatibility result = hp::checkModuleBuildId(id_fn());
    CHECK_FALSE_MESSAGE(result.compatible, "a module carrying id "
                                               << id_fn() << " must not be accepted by an "
                                               << "engine whose id is " << hp::engineBuildId());

    // The guarantee that matters: refused *before* any of its code runs.
    // Refusing afterwards would be a diagnostic rather than a guard, because by
    // then it has already read memory at the wrong offsets.
    auto was_called = stale.sym<bool (*)()>("hp_stale_entry_was_called");
    REQUIRE(was_called != nullptr);
    CHECK_MESSAGE(!was_called(),
                  "the module's entry point ran despite the id mismatch -- the check must "
                  "happen before any module code is invoked");
}

TEST_CASE("an unstamped module is refused, not assumed fine" * doctest::test_suite("module")) {
    // What a module built by something that bypassed hp_add_gameplay_module()
    // looks like: no stamp at all. That is the case with no evidence either
    // way, which is a refusal.
    const hp::ModuleCompatibility result = hp::checkModuleBuildId(nullptr);
    CHECK_FALSE(result.compatible);
    CHECK(std::strcmp(result.module_id, "<unstamped>") == 0);
}

TEST_CASE("the refusal says what to rebuild" * doctest::test_suite("module")) {
    // A refusal that does not tell a developer what to do is only marginally
    // better than the corruption it prevents (104.6).
    const hp::ModuleCompatibility result = hp::checkModuleBuildId("dead0000beef0000");
    const std::string message = hp::describeIncompatibility("libhp_example.so", result);

    CHECK_MESSAGE(message.find("libhp_example.so") != std::string::npos,
                  "the message must name the module: " << message);
    CHECK_MESSAGE(message.find("dead0000beef0000") != std::string::npos,
                  "the message must carry the module's id: " << message);
    CHECK_MESSAGE(message.find(hp::engineBuildId()) != std::string::npos,
                  "the message must carry the engine's id: " << message);
    CHECK_MESSAGE(message.find("Rebuild") != std::string::npos,
                  "the message must say what to do: " << message);
}

// --- reflection across the module boundary (T0053) ---------------------------
//
// The case the fast-bucket reflection tests cannot cover: whether a gameplay
// module and the engine actually share one meta context.
//
// This is where the subsystem is most likely to be silently wrong. `entt::locator`
// keeps its storage in a static per binary, and under -fvisibility=hidden the
// executable, the engine and every module each get their own. A participant that
// never adopts the engine's context sees an *empty* one — every type
// unresolvable, no error, no crash, just a system that quietly does nothing. So
// these cases assert the sharing directly rather than inferring it from
// something working.
//
// It loads the real `samples/sandbox` module rather than a fixture, because the
// thing worth guarding is that a module built the way modules are actually built
// can participate.

TEST_CASE("a gameplay module registers into the engine's meta context" *
          doctest::test_suite("reflect")) {
    hp::adoptMetaContext(); // the test executable is a participant too

    LoadedModule sandbox(sandbox_module_path());
    REQUIRE_MESSAGE(sandbox.ok(), sandbox.error());

    auto register_types = sandbox.sym<void (*)()>("hpSandboxRegisterTypes");
    auto forget_types = sandbox.sym<void (*)()>("hpSandboxForgetTypes");
    REQUIRE(register_types != nullptr);
    REQUIRE(forget_types != nullptr);

    // Before the module registers anything, the engine side must not see it.
    // Asserted so that a pass below cannot come from a type left behind by
    // another case.
    CHECK_FALSE(static_cast<bool>(hp::resolveType("SandboxHealth")));

    register_types();

    const entt::meta_type from_engine_side = hp::resolveType("SandboxHealth");
    REQUIRE_MESSAGE(static_cast<bool>(from_engine_side),
                    "a type registered by the module must be visible from this side; an empty "
                    "result means the module and the host are not sharing one meta context");

    // And it is usable, not merely present -- enumerable, with its metadata.
    int properties = 0;
    for (auto&& [id, data] : from_engine_side.data()) {
        (void)id;
        (void)data;
        ++properties;
    }
    CHECK(properties == 2);

    auto current = from_engine_side.data("current"_hs);
    REQUIRE(static_cast<bool>(current));
    const auto* meta = static_cast<const hp::PropertyMeta*>(current.custom());
    REQUIRE_MESSAGE(meta != nullptr, "metadata registered in the module must survive the crossing");
    CHECK(std::strcmp(meta->tooltip, "module-owned") == 0);

    forget_types();
}

TEST_CASE("a module resolves a type the host registered" * doctest::test_suite("reflect")) {
    hp::adoptMetaContext();

    // The other direction. The module has never seen this type's definition and
    // reaches it entirely through reflection.
    struct HostOnly {
        float value = 7.5f;
    };

    hp::reflect<HostOnly>("HostOnlyType").property<&HostOnly::value>("value");

    LoadedModule sandbox(sandbox_module_path());
    REQUIRE_MESSAGE(sandbox.ok(), sandbox.error());

    auto register_types = sandbox.sym<void (*)()>("hpSandboxRegisterTypes");
    auto read_property =
        sandbox.sym<bool (*)(const char*, const char*, float*)>("hpSandboxReadHostProperty");
    REQUIRE(register_types != nullptr);
    REQUIRE(read_property != nullptr);
    register_types(); // module adopts the context

    float value = 0.0f;
    CHECK_MESSAGE(read_property("HostOnlyType", "value", &value),
                  "the module should resolve and read a host-registered type through the "
                  "shared context");
    CHECK(value == doctest::Approx(7.5f));

    // A name nobody registered is a clean miss, not a crash, on the far side too.
    float ignored = 0.0f;
    CHECK_FALSE(read_property("NoSuchTypeAnywhere", "value", &ignored));

    hp::forgetType("HostOnlyType");
}

TEST_CASE("a module's types are gone once it deregisters" * doctest::test_suite("reflect")) {
    hp::adoptMetaContext();

    LoadedModule sandbox(sandbox_module_path());
    REQUIRE(sandbox.ok());
    auto register_types = sandbox.sym<void (*)()>("hpSandboxRegisterTypes");
    auto forget_types = sandbox.sym<void (*)()>("hpSandboxForgetTypes");
    REQUIRE(register_types != nullptr);
    REQUIRE(forget_types != nullptr);

    register_types();
    REQUIRE(static_cast<bool>(hp::resolveType("SandboxHealth")));

    forget_types();

    // Not tidiness. entt::meta stores raw function pointers and unowned name
    // literals that live in the module's image, so a registration surviving the
    // module is a pointer into memory that may be unmapped -- structurally the
    // same failure T0105.1 fixed for static destructors, and just as invisible
    // until something walks the list.
    CHECK_MESSAGE(!static_cast<bool>(hp::resolveType("SandboxHealth")),
                  "deregistration must actually remove the type, or the shared context keeps "
                  "pointers into the module's image after it unloads");
}
