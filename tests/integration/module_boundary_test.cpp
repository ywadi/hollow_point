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

#include "../fixtures/abi_boundary.h"

#include <filesystem>
#include <string>

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
