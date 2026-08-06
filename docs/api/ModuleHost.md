# `<hp/ModuleHost.hpp>`

*Generated from `engine/include/hp/ModuleHost.hpp` — do not edit.*

```cpp
#include <hp/ModuleHost.hpp>
```

25 public declaration(s), 25 documented.

## `ModuleServices`

```cpp
struct ModuleServices
```

 What the host lends the modules it has loaded (T0062.11).

 **Every pointer here is borrowed and may be null**, and null is an ordinary
 state rather than an error: a headless host has no device, and a host that
 has not built its scene yet has no scene. A module checks before using one.

 The host refreshes these into every `ModuleContext` immediately before each
 entry point runs, so a module never sees a stale pointer and the order in
 which an application sets them up cannot matter.

## `ModuleContext`

```cpp
struct ModuleContext
```

 Handed to a module's lifecycle entry points.

 A struct rather than a set of arguments because it will grow — the registry
 (T0021), the asset manager (T0023) and the message bus (T0075) all belong
 here — and growing a struct does not break a module's signature the way
 adding a parameter would. That matters more here than elsewhere: a signature
 change is exactly what the build id refuses, so every one of them costs a
 full rebuild of every module.

## `ModuleApi`

```cpp
struct ModuleApi
```

 What a gameplay module implements. The engine calls these; the module never
 calls the engine's loader.

 Both are optional (null is fine) so a module that only registers reflected
 types has nothing to write.

## `kModuleApiSymbol`

```cpp
inline constexpr const char * kModuleApiSymbol = "hp_module_api"
```

 The symbol a module exports to describe itself.

 Resolved by name, so it is `extern "C"`: a mangled name cannot be spelled in
 a string literal without guessing the mangling, and that is the *only*
 reason C linkage appears anywhere near this boundary. What crosses it is
 rich C++, per D12.

## `ModuleApiFn`

```cpp
using ModuleApiFn = const ModuleApi *(*)()
```

 Signature of that symbol.

## `ModuleLoadError`

```cpp
enum class ModuleLoadError
```

| Enumerator | Value |
|---|---|
| `None` | 0 |
| `NotLoadable` | 1 |
| `NotAModule` | 2 |
| `Incompatible` | 3 |
| `EntryPointFailed` | 4 |
| `CopyFailed` | 5 |

 Why a load did not happen.

## `ModuleLoadResult`

```cpp
struct ModuleLoadResult
```

 Outcome of a load or reload attempt.

## `ModuleHost`

```cpp
class ModuleHost
```

 Owns the set of loaded gameplay modules.

 Not a singleton and not global: the editor and the runtime each construct
 one, and a test constructs its own. Engine state is shared through the
 engine library (D12); the *host* is just an object.

## `ModuleHost::ModuleHost`

```cpp
ModuleHost()
```

 Constructs an empty host. Loads nothing until asked.

## `ModuleHost::ModuleHost`

```cpp
ModuleHost(const ModuleHost &)
```

 Not copyable: a copy would hold the same platform handles and unload
 them twice.

## `ModuleHost::operator=`

```cpp
ModuleHost & operator=(const ModuleHost &)
```

 Not copyable; see the copy constructor.
 @returns nothing -- deleted.

## `ModuleHost::load`

```cpp
ModuleLoadResult load(const std::string & path)
```

 Load a module from `path`.

 The file is copied before loading, so the build that produces the next
 version can overwrite the original while this one is live. On Windows
 that is not an optimisation: the OS locks a loaded DLL and the next
 build fails outright (48.3). It is done on both targets anyway, because
 a mechanism that only runs on one platform is a mechanism that is broken
 on that platform and nobody notices.

 @param path the module file as the build produced it.
 @returns the outcome. On failure nothing was loaded and no module code
          ran; on a build-id mismatch, `message` names both ids.

## `ModuleHost::setServices`

```cpp
void setServices(const ModuleServices & services)
```

 Sets what every loaded module is lent (T0062.11).

 Applies to modules already loaded as well as to ones loaded afterwards,
 so an application may call it before or after `load` and gets the same
 result. That is not a convenience: the editor cannot load its modules
 until the render device exists, and it cannot build the device until the
 layer stack is up, so "set them up in the right order" is a rule that
 would be broken by the first host with a different startup shape.

 @param services the borrowed pointers. Anything null stays null.
 @returns nothing.

## `ModuleHost::services`

```cpp
const ModuleServices & services() const
```

 @returns what modules are currently lent.

## `ModuleHost::update`

```cpp
void update(double deltaSeconds)
```

 Runs every live module's `onUpdate`, in load order.

 **Frame phase 4**, and the caller decides that — `Application` does it
 for you. A host driving its own loop calls this once per frame after its
 layers have updated and before transforms propagate, or a module that
 moves something renders one frame stale.

 @param deltaSeconds the scaled frame delta.
 @returns nothing.

## `ModuleHost::reloadChanged`

```cpp
std::vector<ModuleLoadResult> reloadChanged()
```

 Reload every module whose file has changed since it was loaded.

 **Call this only at the end-of-frame safe point (frame phase 12).**
 Nothing is iterating and nothing is mid-draw there, which is the one
 moment replacing code out from under the process is safe. `Application`
 does this for you; a host driving its own loop must not do it anywhere
 else.

 A module whose new build fails to load — a compile error mid-save, a
 stale build id — leaves the previous one running and reports why (48.6).

 @returns one result per module that was *attempted*, in load order.
          Empty when nothing changed, which is the common case and costs
          one stat() per module.

## `ModuleHost::reloadAll`

```cpp
std::vector<ModuleLoadResult> reloadAll()
```

 Force a reload of every module regardless of file timestamps.
 Same safe-point rule as `reloadChanged`.

## `ModuleHost::unloadAll`

```cpp
void unloadAll()
```

 Unload everything, newest first. Called by the destructor.

## `ModuleHost::size`

```cpp
std::size_t size() const
```

 How many modules are currently live.

## `ModuleHost::names`

```cpp
std::vector<std::string> names() const
```

 Names of the live modules, in load order.

## `ModuleHost::totalLoads`

```cpp
std::uint32_t totalLoads() const
```

 Total successful loads across every module, including reloads. Cheap
 evidence for a test that a reload actually happened rather than being
 silently skipped.

## `ModuleHost::onReloaded`

```cpp
void onReloaded(std::function<void (const std::string &, std::uint32_t)> callback)
```

 Sets the callback invoked after each successful reload.

 The editor uses this to refresh anything holding module-derived data; a
 test uses it to observe that a swap actually occurred. Called with the
 module's name and its new generation, from inside the reload, which
 means inside frame phase 12.

 @param callback invoked as `callback(name, generation)`. Replaces any
        previously set callback; pass `{}` to clear.

## `moduleEntryPointThrew`

```cpp
void moduleEntryPointThrew(const char * module_name, const char * entry_point)
```

 Reports an exception that a module entry point let escape.

 Logs rather than rethrows. Rethrowing would put the exception straight back
 into the loader, which is the thing being prevented.

 @param module_name the module that threw, for the message. May be null.
 @param entry_point which entry point threw, e.g. "onLoad". May be null.

## `invokeGuarded`

```cpp
void invokeGuarded(void (*)(ModuleContext &) fn, ModuleContext & ctx, const char * module_name, const char * entry_point)
```

 Calls a module entry point with the guard T0127 requires.

 `inline` in the header on purpose: it is compiled into the *module*, so the
 `catch (...)` sits in the same image as the throw and the unwind never
 crosses the boundary at all. Correct either way — `catch (...)` does match
 across it — but the shorter unwind is the one with fewer assumptions in it.

 @param fn the entry point to call. Null means the module does not implement
        it, which is allowed and is not an error.
 @param ctx the context handed to the entry point.
 @param module_name the module's name, for the message if it throws.
 @param entry_point the entry point's name, for the same message.

## `invokeGuarded`

```cpp
void invokeGuarded(void (*)(ModuleContext &, double) fn, ModuleContext & ctx, double deltaSeconds, const char * module_name, const char * entry_point)
```

 Calls a per-frame entry point with the same guard.

 An overload rather than a template: the guard must be *the* thing this
 header offers, and a template would let a signature that is not a module
 entry point be wrapped by it.

 @param fn the entry point to call. Null means the module does not implement
        it, which is allowed and is not an error.
 @param ctx the context handed to the entry point.
 @param deltaSeconds forwarded to the entry point.
 @param module_name the module's name, for the message if it throws.
 @param entry_point the entry point's name, for the same message.
