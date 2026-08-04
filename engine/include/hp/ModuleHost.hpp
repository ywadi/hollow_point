// Loading, reloading and unloading gameplay modules (T0048, D12).
//
// The engine is a shared library and a gameplay module links it, so engine
// state exists once per process (D12). This is the piece that brings such a
// module into a running process and — the point of the ticket — swaps it for a
// freshly built one without restarting the editor.
//
// Three properties are structural rather than incidental, and each exists
// because the alternative failed somewhere already recorded:
//
//   * **A module declares its lifecycle through the engine**, by way of
//     `HP_GAMEPLAY_MODULE`, rather than exporting hand-written `extern "C"`
//     functions. That is what lets the macro wrap every entry point in
//     `catch (...)`: on Linux a typed exception cannot cross this boundary, and
//     an exception escaping into the loader is undefined behaviour rather than
//     a stack trace (T0127). A convention nobody can forget beats a convention
//     written down.
//
//   * **The build id is checked before any module code runs.** Not after
//     loading and not on first use — a stale module resolves its symbols
//     perfectly well and then reads fields at offsets that have moved (T0104).
//
//   * **Modules are plural.** The editor and the runtime both host them and a
//     project may split gameplay across several. Designing for one and
//     generalising later is the expensive order, which is why the host owns a
//     set from the start.
//
// What this deliberately does *not* do is keep state for you. All persistent
// state belongs to the engine; a module holds behaviour. Statics inside a
// module are destroyed on unload, so anything kept there is gone — and unlike
// most of this file, that is not enforced, only stated.
#pragma once

#include <hp/Api.hpp>
#include <hp/Module.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hp {

/// Handed to a module's lifecycle entry points.
///
/// A struct rather than a set of arguments because it will grow — the registry
/// (T0021), the asset manager (T0023) and the message bus (T0075) all belong
/// here — and growing a struct does not break a module's signature the way
/// adding a parameter would. That matters more here than elsewhere: a signature
/// change is exactly what the build id refuses, so every one of them costs a
/// full rebuild of every module.
struct ModuleContext {
    /// How many times this module has been loaded into this process, starting
    /// at 1. A module can tell a fresh start from a hot reload by reading it,
    /// which is the only thing it is allowed to conclude about its own past.
    std::uint32_t generation = 1;

    /// The name the module declared.
    const char* name = "";
};

/// What a gameplay module implements. The engine calls these; the module never
/// calls the engine's loader.
///
/// Both are optional (null is fine) so a module that only registers reflected
/// types has nothing to write.
struct ModuleApi {
    /// Module name, for diagnostics. Never null — the macro supplies it.
    const char* name = "";

    /// Called once per load, after the build id has been accepted and before
    /// the module is considered live.
    void (*onLoad)(ModuleContext&) = nullptr;

    /// Called once per unload, before the image goes away. The last chance to
    /// hand anything back to the engine, and the place where reflected types
    /// must be deregistered — `entt::meta` holds function pointers and unowned
    /// name literals that live in this image (T0053).
    void (*onUnload)(ModuleContext&) = nullptr;
};

/// The symbol a module exports to describe itself.
///
/// Resolved by name, so it is `extern "C"`: a mangled name cannot be spelled in
/// a string literal without guessing the mangling, and that is the *only*
/// reason C linkage appears anywhere near this boundary. What crosses it is
/// rich C++, per D12.
inline constexpr const char* kModuleApiSymbol = "hp_module_api";

/// Signature of that symbol.
using ModuleApiFn = const ModuleApi* (*)();

/// Why a load did not happen.
enum class ModuleLoadError {
    None,
    /// The file could not be opened by the platform loader.
    NotLoadable,
    /// Loaded, but does not export `hp_module_api`.
    NotAModule,
    /// Built against a different engine (T0104). The most likely one in
    /// practice, and the only one whose message is worth showing verbatim.
    Incompatible,
    /// `onLoad` threw, or the module's own guard reported failure.
    EntryPointFailed,
    /// The copy-before-load step failed; see `message`.
    CopyFailed,
};

/// Outcome of a load or reload attempt.
struct ModuleLoadResult {
    bool ok = false;
    ModuleLoadError error = ModuleLoadError::None;
    /// Developer-facing text, populated on failure. Names the module and what
    /// to do about it.
    std::string message;
    /// Load count after this call, 1 on a first successful load.
    std::uint32_t generation = 0;
};

/// Owns the set of loaded gameplay modules.
///
/// Not a singleton and not global: the editor and the runtime each construct
/// one, and a test constructs its own. Engine state is shared through the
/// engine library (D12); the *host* is just an object.
class HP_API ModuleHost {
public:
    /// Constructs an empty host. Loads nothing until asked.
    ModuleHost();

    /// Unloads every live module, newest first.
    ~ModuleHost();

    /// Not copyable: a copy would hold the same platform handles and unload
    /// them twice.
    ModuleHost(const ModuleHost&) = delete;

    /// Not copyable; see the copy constructor.
    /// @returns nothing -- deleted.
    ModuleHost& operator=(const ModuleHost&) = delete;

    /// Load a module from `path`.
    ///
    /// The file is copied before loading, so the build that produces the next
    /// version can overwrite the original while this one is live. On Windows
    /// that is not an optimisation: the OS locks a loaded DLL and the next
    /// build fails outright (48.3). It is done on both targets anyway, because
    /// a mechanism that only runs on one platform is a mechanism that is broken
    /// on that platform and nobody notices.
    ///
    /// @param path the module file as the build produced it.
    /// @returns the outcome. On failure nothing was loaded and no module code
    ///          ran; on a build-id mismatch, `message` names both ids.
    ModuleLoadResult load(const std::string& path);

    /// Reload every module whose file has changed since it was loaded.
    ///
    /// **Call this only at the end-of-frame safe point (frame phase 12).**
    /// Nothing is iterating and nothing is mid-draw there, which is the one
    /// moment replacing code out from under the process is safe. `Application`
    /// does this for you; a host driving its own loop must not do it anywhere
    /// else.
    ///
    /// A module whose new build fails to load — a compile error mid-save, a
    /// stale build id — leaves the previous one running and reports why (48.6).
    ///
    /// @returns one result per module that was *attempted*, in load order.
    ///          Empty when nothing changed, which is the common case and costs
    ///          one stat() per module.
    std::vector<ModuleLoadResult> reloadChanged();

    /// Force a reload of every module regardless of file timestamps.
    /// Same safe-point rule as `reloadChanged`.
    std::vector<ModuleLoadResult> reloadAll();

    /// Unload everything, newest first. Called by the destructor.
    void unloadAll();

    /// How many modules are currently live.
    std::size_t size() const;

    /// Names of the live modules, in load order.
    std::vector<std::string> names() const;

    /// Total successful loads across every module, including reloads. Cheap
    /// evidence for a test that a reload actually happened rather than being
    /// silently skipped.
    std::uint32_t totalLoads() const;

    /// Sets the callback invoked after each successful reload.
    ///
    /// The editor uses this to refresh anything holding module-derived data; a
    /// test uses it to observe that a swap actually occurred. Called with the
    /// module's name and its new generation, from inside the reload, which
    /// means inside frame phase 12.
    ///
    /// @param callback invoked as `callback(name, generation)`. Replaces any
    ///        previously set callback; pass `{}` to clear.
    void onReloaded(std::function<void(const std::string&, std::uint32_t)> callback);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

namespace detail {

/// Reports an exception that a module entry point let escape.
///
/// Logs rather than rethrows. Rethrowing would put the exception straight back
/// into the loader, which is the thing being prevented.
///
/// @param module_name the module that threw, for the message. May be null.
/// @param entry_point which entry point threw, e.g. "onLoad". May be null.
HP_API void moduleEntryPointThrew(const char* module_name, const char* entry_point) noexcept;

/// Calls a module entry point with the guard T0127 requires.
///
/// `inline` in the header on purpose: it is compiled into the *module*, so the
/// `catch (...)` sits in the same image as the throw and the unwind never
/// crosses the boundary at all. Correct either way — `catch (...)` does match
/// across it — but the shorter unwind is the one with fewer assumptions in it.
///
/// @param fn the entry point to call. Null means the module does not implement
///        it, which is allowed and is not an error.
/// @param ctx the context handed to the entry point.
/// @param module_name the module's name, for the message if it throws.
/// @param entry_point the entry point's name, for the same message.
inline void invokeGuarded(void (*fn)(ModuleContext&), ModuleContext& ctx, const char* module_name,
                          const char* entry_point) noexcept {
    if (fn == nullptr) {
        return;
    }
    try {
        fn(ctx);
    } catch (...) {
        moduleEntryPointThrew(module_name, entry_point);
    }
}

} // namespace detail

} // namespace hp

/// Declares a gameplay module's lifecycle.
///
/// Put it once in a module, at namespace scope:
///
/// ```cpp
/// static void onLoad(hp::ModuleContext& ctx) { ... }
/// static void onUnload(hp::ModuleContext& ctx) { ... }
/// HP_GAMEPLAY_MODULE("sandbox", onLoad, onUnload)
/// ```
///
/// **The guard is the reason this is a macro rather than a documented
/// convention.** Each entry point is wrapped in `catch (...)` here, so an
/// exception cannot escape a module into the loader. That is not stylistic: a
/// typed exception does not survive this boundary on Linux at all (T0127), and
/// one that escapes into foreign code is undefined behaviour. Written as a rule
/// it would be forgotten once; written here it cannot be.
///
/// @param module_name string literal naming the module in diagnostics.
/// @param load_fn function called on load, or `nullptr`.
/// @param unload_fn function called on unload, or `nullptr`.
#define HP_GAMEPLAY_MODULE(module_name, load_fn, unload_fn)                                        \
    namespace hp_module_detail {                                                                   \
    void guardedLoad(::hp::ModuleContext& ctx) {                                                   \
        ::hp::detail::invokeGuarded(load_fn, ctx, module_name, "onLoad");                          \
    }                                                                                              \
    void guardedUnload(::hp::ModuleContext& ctx) {                                                 \
        ::hp::detail::invokeGuarded(unload_fn, ctx, module_name, "onUnload");                      \
    }                                                                                              \
    }                                                                                              \
    extern "C" HP_EXPORT const ::hp::ModuleApi* hp_module_api() {                                  \
        static const ::hp::ModuleApi api{module_name, &hp_module_detail::guardedLoad,              \
                                         &hp_module_detail::guardedUnload};                        \
        return &api;                                                                               \
    }
