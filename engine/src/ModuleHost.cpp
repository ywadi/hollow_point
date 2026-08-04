// Gameplay module loading and hot reload (T0048).
//
// See hp/ModuleHost.hpp for the shape and why it is that shape. This file is
// the platform half plus the swap logic, and the swap logic is where the care
// is: a reload that half-succeeds is worse than one that fails, because the
// process keeps running with a module that is neither the old one nor the new.
#include <hp/ModuleHost.hpp>

#include <hp/Log.hpp>

#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace hp {
namespace {

const LogCategory kLog("module");

/// A live module image plus what is needed to reload it.
struct Loaded {
    std::string path;     ///< where the build writes it
    std::string loadedAs; ///< the copy actually handed to the loader
    void* handle = nullptr;
    const ModuleApi* api = nullptr;
    ModuleContext context;
    std::filesystem::file_time_type stamp{};
    std::uintmax_t size = 0;
};

void* platformOpen(const std::string& path, std::string& error) {
#if defined(_WIN32)
    void* handle = static_cast<void*>(LoadLibraryA(path.c_str()));
    if (handle == nullptr) {
        error = path + ": LoadLibraryA failed, error " + std::to_string(GetLastError());
    }
    return handle;
#else
    // RTLD_LOCAL so a module's symbols cannot satisfy another module's
    // undefined symbols by accident -- two gameplay modules exporting the same
    // name is a mistake, not a linking strategy.
    //
    // Deliberately NOT RTLD_NODELETE. That used to be mandatory: dlclose of a
    // library holding any static needing destruction segfaulted the process at
    // exit. T0105.1 root-caused it to a missing .fini_array (ziglang/zig#17908)
    // and fixed it -- hp_add_gameplay_module() compiles in a finalizer that
    // makes the linker emit one -- so a real unload works, which is the whole
    // premise of reloading.
    void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* e = ::dlerror();
        error = path + ": " + (e != nullptr ? e : "dlopen failed");
    }
    return handle;
#endif
}

void platformClose(void* handle) {
    if (handle == nullptr) {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    ::dlclose(handle);
#endif
}

template <class Fn>
Fn platformSymbol(void* handle, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<Fn>(
        reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name)));
#else
    return reinterpret_cast<Fn>(::dlsym(handle, name));
#endif
}

/// Where the working copy of `path` for `generation` goes.
///
/// Beside the original rather than in a temp directory, and this is not
/// arbitrary: on Linux the copy has to resolve libhp_engine.so, and RUNPATH is
/// `$ORIGIN`-relative (T0105.4). A copy in /tmp would have the right bytes and
/// the wrong neighbours.
std::filesystem::path copyPathFor(const std::filesystem::path& path, std::uint32_t generation) {
    std::filesystem::path copy = path;
    copy.replace_filename(path.stem().string() + ".hot" + std::to_string(generation) +
                          path.extension().string());
    return copy;
}

ModuleLoadResult failure(ModuleLoadError error, std::string message) {
    ModuleLoadResult result;
    result.ok = false;
    result.error = error;
    result.message = std::move(message);
    return result;
}

} // namespace

namespace detail {

void moduleEntryPointThrew(const char* module_name, const char* entry_point) noexcept {
    // noexcept, and logging is the only thing this may do. Rethrowing would
    // hand the exception back to the loader, which is the case T0127 exists to
    // prevent; aborting would turn a gameplay bug into a dead editor.
    HP_LOG_ERROR(kLog,
                 "module '{}': {} threw and the exception was swallowed at the boundary. "
                 "An exception must not cross the module boundary -- on Linux a typed "
                 "catch does not survive it at all. Return a result instead.",
                 module_name != nullptr ? module_name : "<module>",
                 entry_point != nullptr ? entry_point : "<entry point>");
}

} // namespace detail

struct ModuleHost::Impl {
    std::vector<Loaded> modules;
    std::function<void(const std::string&, std::uint32_t)> reloaded;
    std::uint32_t totalLoads = 0;
    /// Distinguishes successive working copies so a new one never collides with
    /// an image the process still has mapped.
    std::uint32_t copyCounter = 0;

    /// Stages, opens and *validates* a module — without running a line of its
    /// code. Everything that can be checked cheaply is checked here: the file
    /// exists, the loader accepts it, the build id matches, the api symbol is
    /// present.
    ///
    /// Splitting this from `activate` is what makes 48.6 true. A reload
    /// validates the new image while the old one is still live and untouched,
    /// so a bad build leaves the running module exactly where it was. Doing it
    /// the other way -- unload, then load, then discover the new one is stale
    /// -- leaves the process with no module at all, which is the failure mode
    /// the requirement exists to prevent.
    ///
    /// It also avoids two images of the same module being live at once, which
    /// would double-register every reflected type (T0053).
    ModuleLoadResult open(const std::string& path, std::uint32_t generation, Loaded& out);

    /// Runs the module's `onLoad`. Separate from `open` for the reason above.
    void activate(Loaded& module);

    void close(Loaded& module);

    /// Replace the module at `index` with a freshly built copy of its file.
    ModuleLoadResult reloadAt(std::size_t index);
};

ModuleLoadResult ModuleHost::Impl::open(const std::string& path, std::uint32_t generation,
                                        Loaded& out) {
    std::error_code ec;
    const std::filesystem::path source(path);
    if (!std::filesystem::exists(source, ec) || ec) {
        return failure(ModuleLoadError::NotLoadable, path + ": no such file");
    }

    // Copy before loading. On Windows the OS locks a loaded DLL and the next
    // build cannot overwrite it, which stops hot reload dead (48.3). Done on
    // Linux too: a platform-specific mechanism is one that only gets exercised
    // on one platform, and this is the one that has to work under pressure.
    const std::filesystem::path copy = copyPathFor(source, ++copyCounter);
    std::filesystem::remove(copy, ec);
    std::filesystem::copy_file(source, copy, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return failure(ModuleLoadError::CopyFailed, path + ": could not stage a working copy at " +
                                                        copy.string() + " -- " + ec.message());
    }

    std::string error;
    void* handle = platformOpen(copy.string(), error);
    if (handle == nullptr) {
        std::filesystem::remove(copy, ec);
        return failure(ModuleLoadError::NotLoadable, error);
    }

    // Is this ours at all? Answered first, and not for tidiness: **an unstamped
    // library must not be unmapped.**
    //
    // Only `hp_add_gameplay_module()` compiles in the finalizer that makes the
    // linker emit `.fini_array`, and without one a `dlclose` leaves the
    // library's `__cxa_atexit` registrations pointing into unmapped memory —
    // the process then dies at exit, far from here (T0105.1,
    // ziglang/zig#17908). Measured on this tree with an engine reference held:
    // a stamped module unloads at exit 0, `libhp_abi_module` — a normal
    // `add_library(SHARED)` — takes the process down with SIGSEGV.
    //
    // The stamp is a reliable proxy because the helper is the only thing that
    // emits it, and it emits the finalizer at the same time. So: stamped means
    // safe to unload; unstamped means refuse *and leave it mapped*. Leaking one
    // mapping until process exit is the strictly better failure, and a refused
    // module is a developer-time event rather than a steady state.
    auto idFn = platformSymbol<ModuleBuildIdFn>(handle, kModuleBuildIdSymbol);
    if (idFn == nullptr) {
        // Deliberately no platformClose(handle) here. See above.
        std::filesystem::remove(copy, ec);
        return failure(ModuleLoadError::NotAModule,
                       path + ": no " + kModuleBuildIdSymbol +
                           " symbol, so this was not built by hp_add_gameplay_module() and is "
                           "not a gameplay module. Declare it with HP_GAMEPLAY_MODULE() and "
                           "build it with hp_add_gameplay_module(). It has been left mapped "
                           "rather than unloaded, because a library built without that helper "
                           "has no .fini_array and unloading it crashes the process at exit.");
    }

    // The id check comes before *anything* the module wrote runs. A stale
    // module resolves its symbols perfectly and then reads fields at offsets
    // that have moved, so checking after calling onLoad would be a diagnostic
    // rather than a guard (T0104).
    const ModuleCompatibility compatibility = checkModuleBuildId(idFn());
    if (!compatibility.compatible) {
        const std::string message =
            describeIncompatibility(source.filename().string().c_str(), compatibility);
        platformClose(handle);
        std::filesystem::remove(copy, ec);
        return failure(ModuleLoadError::Incompatible, message);
    }

    auto apiFn = platformSymbol<ModuleApiFn>(handle, kModuleApiSymbol);
    const ModuleApi* api = apiFn != nullptr ? apiFn() : nullptr;
    if (api == nullptr) {
        platformClose(handle);
        std::filesystem::remove(copy, ec);
        return failure(ModuleLoadError::NotAModule,
                       path + ": no " + kModuleApiSymbol +
                           " symbol. Declare the module with HP_GAMEPLAY_MODULE(), which is "
                           "what exports it.");
    }

    out.path = path;
    out.loadedAs = copy.string();
    out.handle = handle;
    out.api = api;
    out.context.generation = generation;
    out.context.name = api->name;
    out.stamp = std::filesystem::last_write_time(source, ec);
    out.size = std::filesystem::file_size(source, ec);

    ModuleLoadResult result;
    result.ok = true;
    result.generation = generation;
    return result;
}

void ModuleHost::Impl::activate(Loaded& module) {
    if (module.api != nullptr && module.api->onLoad != nullptr) {
        module.api->onLoad(module.context);
    }
    ++totalLoads;
}

ModuleLoadResult ModuleHost::Impl::reloadAt(std::size_t index) {
    Loaded& current = modules[index];
    const std::string path = current.path;
    const std::uint32_t generation = current.context.generation + 1;

    // Validate first, while the running module is still untouched.
    Loaded fresh;
    ModuleLoadResult result = open(path, generation, fresh);
    if (!result.ok) {
        HP_LOG_ERROR(kLog, "reload of {} refused, keeping generation {} running: {}", path,
                     current.context.generation, result.message);
        return result;
    }

    // Only now is the old one disturbed. From here the swap cannot fail: the
    // new image is open and has already passed every check that does not
    // require running its code.
    const std::string name = current.context.name;
    close(current);
    modules[index] = std::move(fresh);
    activate(modules[index]);

    HP_LOG_INFO(kLog, "reloaded '{}' -> generation {}", modules[index].context.name, generation);
    if (reloaded) {
        reloaded(modules[index].context.name, generation);
    }
    return result;
}

void ModuleHost::Impl::close(Loaded& module) {
    if (module.api != nullptr && module.api->onUnload != nullptr) {
        module.api->onUnload(module.context);
    }
    platformClose(module.handle);
    module.handle = nullptr;
    module.api = nullptr;

    std::error_code ec;
    std::filesystem::remove(module.loadedAs, ec);
    if (ec) {
        // Not fatal. On Windows the file can stay locked briefly after
        // FreeLibrary; leaving one behind costs disk, not correctness.
        HP_LOG_DEBUG(kLog, "could not remove the working copy {} ({})", module.loadedAs,
                     ec.message());
    }
}

ModuleHost::ModuleHost() : impl_(std::make_unique<Impl>()) {}

ModuleHost::~ModuleHost() {
    unloadAll();
}

ModuleLoadResult ModuleHost::load(const std::string& path) {
    Loaded loaded;
    ModuleLoadResult result = impl_->open(path, 1, loaded);
    if (!result.ok) {
        HP_LOG_ERROR(kLog, "{}", result.message);
        return result;
    }
    impl_->modules.push_back(std::move(loaded));
    impl_->activate(impl_->modules.back());
    HP_LOG_INFO(kLog, "loaded '{}' from {}", impl_->modules.back().context.name, path);
    return result;
}

std::vector<ModuleLoadResult> ModuleHost::reloadChanged() {
    std::vector<ModuleLoadResult> results;
    for (std::size_t i = 0; i < impl_->modules.size(); ++i) {
        Loaded& current = impl_->modules[i];
        std::error_code ec;
        const std::filesystem::path source(current.path);

        const auto stamp = std::filesystem::last_write_time(source, ec);
        if (ec) {
            // Mid-write: the build has the file open, or it does not exist for
            // an instant. Not an error and not a reload -- try again next
            // frame. This is the debounce (48.4), and it is why the watcher is
            // a poll rather than an inotify/ReadDirectoryChanges subscription:
            // a change notification fires on the *first* write, which is
            // reliably a half-written file.
            continue;
        }
        const auto size = std::filesystem::file_size(source, ec);
        if (ec || (stamp == current.stamp && size == current.size)) {
            continue;
        }

        // Changed. Record what we saw even if the load fails, so a module that
        // does not compile is reported once rather than every frame.
        current.stamp = stamp;
        current.size = size;

        results.push_back(impl_->reloadAt(i));
    }
    return results;
}

std::vector<ModuleLoadResult> ModuleHost::reloadAll() {
    std::vector<ModuleLoadResult> results;
    for (std::size_t i = 0; i < impl_->modules.size(); ++i) {
        results.push_back(impl_->reloadAt(i));
    }
    return results;
}

void ModuleHost::unloadAll() {
    // Newest first, so a module that loaded later -- and may therefore have
    // registered on top of an earlier one -- comes off first.
    for (auto it = impl_->modules.rbegin(); it != impl_->modules.rend(); ++it) {
        impl_->close(*it);
    }
    impl_->modules.clear();
}

std::size_t ModuleHost::size() const {
    return impl_->modules.size();
}

std::vector<std::string> ModuleHost::names() const {
    std::vector<std::string> out;
    out.reserve(impl_->modules.size());
    for (const Loaded& module : impl_->modules) {
        out.emplace_back(module.context.name);
    }
    return out;
}

std::uint32_t ModuleHost::totalLoads() const {
    return impl_->totalLoads;
}

void ModuleHost::onReloaded(std::function<void(const std::string&, std::uint32_t)> callback) {
    impl_->reloaded = std::move(callback);
}

} // namespace hp
