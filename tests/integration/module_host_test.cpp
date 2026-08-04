// Loading, reloading and unloading gameplay modules (T0048).
//
// Bucket: integration. Every case opens a real shared library at run time.
//
// These run against the *real* sample module, not a minimal fixture, and that
// is deliberate. `libhp_sandbox` links the engine, registers reflected types
// into the shared entt::meta context and holds the statics that come with
// doing so — which is exactly the case T0105.1 could only call "strong
// evidence" for, because its own fixture was reduced to a single
// `static std::string` with no engine code in it.
#include <doctest/doctest.h>

#include <hp/Log.hpp>
#include <hp/ModuleHost.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
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
/// Not the working directory: the harness runs these from wherever it likes,
/// and under wine or WSL interop the cwd is not even reliably the same shape of
/// path. The executable can always find itself.
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
    if (n <= 0) {
        return ".";
    }
    std::string path(buf, static_cast<size_t>(n));
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
#endif
}

/// The real sample gameplay module, by a path relative to this binary.
///
/// Composed at run time rather than baked by CMake, for the two reasons this
/// suite has already learned the hard way: a `$<TARGET_FILE:...>` path is a
/// *host* path and unopenable from a Windows binary, and a POST_BUILD copy goes
/// stale whenever the module changes without this target relinking.
std::string sandbox_path() {
    return exe_dir() + PATH_SEP + ".." + PATH_SEP + "samples" + PATH_SEP + "sandbox" + PATH_SEP +
           HP_SANDBOX_MODULE_NAME;
}

std::string beside_exe(const char* name) {
    return exe_dir() + PATH_SEP + name;
}

} // namespace

TEST_CASE("a gameplay module loads, and says so" * doctest::test_suite("module")) {
    hp::ModuleHost host;
    const hp::ModuleLoadResult result = host.load(sandbox_path());

    REQUIRE_MESSAGE(result.ok, result.message);
    CHECK(result.generation == 1);
    CHECK(host.size() == 1);
    REQUIRE(host.names().size() == 1);
    CHECK(host.names()[0] == "sandbox");
    CHECK(host.totalLoads() == 1);
}

TEST_CASE("the original file can be overwritten while the module is live" *
          doctest::test_suite("module")) {
    // 48.3, and the case that decides whether hot reload works on Windows at
    // all: the OS locks a loaded DLL, so a build that cannot overwrite its own
    // output cannot produce the next version. Copy-before-load is what buys
    // this, and it is done on both targets so the mechanism is exercised on
    // both rather than only on the one that needs it.
    // Operates on its own copy of the module, never on the shared original, and
    // that is not fastidiousness -- it is the finding this case produced.
    //
    // Overwriting a file that some other mapping still holds open does not
    // fail on Linux. It succeeds, and it rewrites the pages of the live image.
    // The first version of this test overwrote the real libhp_sandbox.so while
    // the boundary suite had it mapped RTLD_NODELETE; every case passed and the
    // process then died at exit, running .fini_array out of a corrupted image.
    //
    // **That is the second, independent reason copy-before-load is right, and
    // the one that applies to Linux.** On Windows the OS refuses the write, so
    // the build fails loudly. On Linux nothing refuses it, so a rebuild would
    // quietly corrupt the running module instead. Loading a copy means the
    // build's output is never the file that is mapped, on either platform.
    std::error_code ec;
    const std::filesystem::path original(sandbox_path());
    const std::filesystem::path subject = original.parent_path() / "overwrite_subject.tmp";
    std::filesystem::copy_file(original, subject, std::filesystem::copy_options::overwrite_existing,
                               ec);
    REQUIRE_MESSAGE(!ec, "could not stage the subject module: " << ec.message());

    hp::ModuleHost host;
    REQUIRE(host.load(subject.string()).ok);

    // The build overwriting its own output while the module is live. This is
    // what fails on Windows without copy-before-load, because the OS locks a
    // loaded DLL and the next build cannot write it.
    std::filesystem::copy_file(original, subject, std::filesystem::copy_options::overwrite_existing,
                               ec);
    CHECK_MESSAGE(!ec, "the build could not overwrite a loaded module ("
                           << ec.message()
                           << ") -- copy-before-load is not working, and on Windows that means "
                              "hot reload cannot work at all");

    // And the live module is unaffected, because it is not that file.
    CHECK(host.size() == 1);
    const std::vector<hp::ModuleLoadResult> results = host.reloadAll();
    REQUIRE(results.size() == 1);
    CHECK_MESSAGE(results[0].ok, results[0].message);

    host.unloadAll();
    std::filesystem::remove(subject, ec);
}

TEST_CASE("reloading swaps the module and bumps its generation" * doctest::test_suite("module")) {
    hp::ModuleHost host;
    REQUIRE(host.load(sandbox_path()).ok);

    std::vector<std::pair<std::string, std::uint32_t>> observed;
    host.onReloaded([&observed](const std::string& name, std::uint32_t generation) {
        observed.emplace_back(name, generation);
    });

    const std::vector<hp::ModuleLoadResult> results = host.reloadAll();
    REQUIRE(results.size() == 1);
    REQUIRE_MESSAGE(results[0].ok, results[0].message);
    CHECK(results[0].generation == 2);

    // totalLoads is the evidence a swap actually happened rather than being
    // quietly skipped -- a reload that no-ops would leave every other
    // assertion here true.
    CHECK(host.totalLoads() == 2);
    REQUIRE(observed.size() == 1);
    CHECK(observed[0].first == "sandbox");
    CHECK(observed[0].second == 2);
    CHECK(host.size() == 1);
}

TEST_CASE("an unchanged module is not reloaded" * doctest::test_suite("module")) {
    // The common case, and the one that runs every frame. If this ever starts
    // reloading, the editor reloads gameplay 60 times a second and the symptom
    // is "everything is slow", not "reload is broken".
    hp::ModuleHost host;
    REQUIRE(host.load(sandbox_path()).ok);

    CHECK(host.reloadChanged().empty());
    CHECK(host.reloadChanged().empty());
    CHECK(host.totalLoads() == 1);
}

TEST_CASE("a module that fails to load leaves the previous one running" *
          doctest::test_suite("module")) {
    // 48.6. The failure mode this prevents is not "the reload failed" -- it is
    // a process left with no module at all because the old one was unloaded
    // before the new one was known to be good.
    hp::ModuleHost host;
    REQUIRE(host.load(sandbox_path()).ok);
    const std::uint32_t loads_before = host.totalLoads();

    // A stale module: well-formed, loads, resolves everything, and carries an
    // id belonging to no build of this engine (T0104's fixture).
    const hp::ModuleLoadResult refused = host.load(beside_exe(HP_STALE_MODULE_NAME));
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == hp::ModuleLoadError::Incompatible);
    CHECK_MESSAGE(refused.message.find("dead0000beef0000") != std::string::npos,
                  "the refusal must name the module's id so a developer knows what to rebuild; "
                  "got: "
                      << refused.message);

    // The point of the case: the good module is untouched.
    CHECK(host.size() == 1);
    CHECK(host.names()[0] == "sandbox");
    CHECK(host.totalLoads() == loads_before);
}

TEST_CASE("a library that is not a gameplay module is refused, and left mapped" *
          doctest::test_suite("module")) {
    // The boundary fixture is a perfectly good shared library, built with a
    // plain add_library(SHARED) and never declaring a lifecycle.
    //
    // Refusing it is the easy half. The half that matters is that the loader
    // must **not unload it**: only hp_add_gameplay_module() compiles in the
    // finalizer that makes the linker emit .fini_array, and dlclosing a library
    // without one leaves its __cxa_atexit registrations pointing into unmapped
    // memory. The process then dies at exit, nowhere near the cause
    // (T0105.1, ziglang/zig#17908).
    //
    // That is not hypothetical: this case is how it was found. It was written
    // expecting a plain refusal, and the whole suite began segfaulting after
    // 55 green cases — at process exit, with the summary already printed.
    hp::ModuleHost host;
    const hp::ModuleLoadResult result = host.load(beside_exe(HP_ABI_MODULE_NAME));

    CHECK_FALSE(result.ok);
    CHECK(result.error == hp::ModuleLoadError::NotAModule);
    CHECK_MESSAGE(result.message.find("hp_add_gameplay_module") != std::string::npos,
                  "the message should say how to fix it, not just what is missing");
    CHECK_MESSAGE(result.message.find("left mapped") != std::string::npos,
                  "the message should say the library was deliberately not unloaded, or the "
                  "next person removes the leak and reintroduces the crash");
    CHECK(host.size() == 0);

    // The assertion with teeth is the exit code of this binary. If the loader
    // ever unloads an unstamped library again, every case here still passes and
    // the process dies afterwards.
}

TEST_CASE("a missing file is refused without loading anything" * doctest::test_suite("module")) {
    hp::ModuleHost host;
    const hp::ModuleLoadResult result = host.load(beside_exe("no_such_module_at_all.so"));
    CHECK_FALSE(result.ok);
    CHECK(result.error == hp::ModuleLoadError::NotLoadable);
    CHECK(host.size() == 0);
}

TEST_CASE("an exception thrown by an entry point does not escape the module" *
          doctest::test_suite("module")) {
    // T0127's enforcement, and the reason the lifecycle is declared through the
    // engine rather than hand-written. Without the guard in HP_GAMEPLAY_MODULE
    // this test does not fail -- it takes the process down, which is what an
    // exception unwinding into the loader does.
    //
    // The fixture throws a `std::` type, the one T0127 measured as impossible
    // to match by type across this boundary on ELF. So `catch (...)` is not a
    // fallback here; it is the only handler that can run.
    hp::ModuleHost host;
    const hp::ModuleLoadResult result = host.load(beside_exe(HP_THROWING_MODULE_NAME));

    // Still a successful load: the module is live and the engine logged the
    // escape. Treating a throwing onLoad as a failed load would be defensible,
    // but it would mean a gameplay bug unloads the module the developer is
    // trying to iterate on.
    REQUIRE_MESSAGE(result.ok, result.message);
    CHECK(host.size() == 1);
    CHECK(host.names()[0] == "throwing");

    // Reaching here at all is the assertion. Unload it too, since the failure
    // this guards can also present at teardown.
    host.unloadAll();
    CHECK(host.size() == 0);
}

TEST_CASE("repeated reloads of a module carrying engine statics stay clean" *
          doctest::test_suite("module")) {
    // T0105.1 fixed the unload segfault and could only prove it against a
    // fixture containing one `static std::string` and no engine code. This is
    // the real case it deferred: libhp_sandbox links the engine and registers
    // reflected types into the shared meta context on every load, deregistering
    // them on every unload.
    //
    // The failure being watched for lands at *process exit*, not here, so a
    // green run of this case is necessary rather than sufficient -- the suite
    // exiting 0 is the other half.
    hp::ModuleHost host;
    REQUIRE(host.load(sandbox_path()).ok);

    constexpr int kCycles = 10;
    for (int i = 0; i < kCycles; ++i) {
        const std::vector<hp::ModuleLoadResult> results = host.reloadAll();
        REQUIRE(results.size() == 1);
        REQUIRE_MESSAGE(results[0].ok, results[0].message);
        CHECK(results[0].generation == static_cast<std::uint32_t>(i + 2));
    }
    CHECK(host.totalLoads() == kCycles + 1);
    CHECK(host.size() == 1);
}

TEST_CASE("several modules are hosted at once, and unload newest first" *
          doctest::test_suite("module")) {
    // "The module" is really "a set of modules" -- the editor and the runtime
    // both host them and a project may split gameplay across several.
    // Designing the host for one and generalising later is the expensive order.
    hp::ModuleHost host;
    REQUIRE(host.load(sandbox_path()).ok);
    REQUIRE(host.load(beside_exe(HP_THROWING_MODULE_NAME)).ok);

    CHECK(host.size() == 2);
    const std::vector<std::string> names = host.names();
    REQUIRE(names.size() == 2);
    CHECK(names[0] == "sandbox");
    CHECK(names[1] == "throwing");

    host.unloadAll();
    CHECK(host.size() == 0);
    CHECK(host.names().empty());
}
