# `<hp/ShaderCook.hpp>`

*Generated from `engine/include/hp/ShaderCook.hpp` — do not edit.*

```cpp
#include <hp/ShaderCook.hpp>
```

12 public declaration(s), 12 documented.

## `kCookedShaderFormatVersion`

```cpp
inline constexpr std :: uint32_t kCookedShaderFormatVersion = 1
```

 The container layout version of a cooked shader archive.

 Bumped when the *header* below changes. A reader refuses anything it does
 not recognise rather than guessing — and in a cooked build that refusal is
 terminal for rendering, so it is reported rather than swallowed.

## `kCookedShaderDirectory`

```cpp
inline constexpr std :: string_view kCookedShaderDirectory = "shaders/cooked"
```

 Where cooked shader archives live in the virtual tree (**D13**).

 Every archive under this directory is loaded, from every mount, and their
 entries merge — which is what makes a DLC pack that adds a material also
 able to add its shader without republishing the base game's archive.
 **Archive file names must therefore be unique across packs**: two packs
 shipping `base.hpsv` collide on the path and only the first-mounted one is
 read. Name an archive after the pack that carries it.

## `kCookedShaderExtension`

```cpp
inline constexpr std :: string_view kCookedShaderExtension = ".hpsv"
```

 The extension a cooked shader archive carries.

## `CookedShaderStatus`

```cpp
enum class CookedShaderStatus
```

| Enumerator | Value |
|---|---|
| `Ok` | 0 |
| `NotACookedShaderArchive` | 1 |
| `FormatMismatch` | 2 |
| `CompilerMismatch` | 3 |
| `Truncated` | 4 |

 Why a cooked shader archive could not be used.

 **Unlike `CookStatus`, none of these means "cook it again".** A shipped
 build cannot; every value here is a reason the game will not render, and is
 reported as such.

## `describe`

```cpp
const char * describe(CookedShaderStatus status)
```

 @param status a cooked-archive status.
 @returns a short human-readable reason, for logs.

## `shaderCompilerId`

```cpp
std::string_view shaderCompilerId()
```

 Identifies the compiler whose output this build can consume.

 The pinned slang version. It is written into every archive and checked on
 load, because a pin bump changes the bytecode a given source produces —
 which reads as a cold cache in development and must read as a **refusal**
 in a cooked build rather than as bytecode from a compiler nobody has.

 @returns the compiler identity, stable for the lifetime of the process.

## `writeCookedShaderArchive`

```cpp
std::vector<std::byte> writeCookedShaderArchive(const std::vector<std::byte> & payload, std::string_view compilerId)
```

 Wraps a cooked payload in an archive container.

 Layout, little-endian throughout — chosen rather than assumed, exactly as
 `Cook.hpp` argues:

 ```text
   magic        8 bytes   "HPSHADER"
   format       u32       kCookedShaderFormatVersion
   compilerId   string    length-prefixed; `shaderCompilerId()` when written
   payloadSize  u64       bytes that follow
   payload      ...       the variant store, opaque to this layer
 ```

 @param payload the cooked bytes.
 @param compilerId the compiler that produced them; normally
        `shaderCompilerId()`.
 @returns the complete file contents, ready to write.

## `readCookedShaderArchive`

```cpp
CookedShaderStatus readCookedShaderArchive(const std::vector<std::byte> & bytes, std::string_view expectedCompilerId, std::vector<std::byte> & outPayload)
```

 Reads and validates a cooked shader archive.

 @param bytes the file contents.
 @param expectedCompilerId the compiler this build can consume; normally
        `shaderCompilerId()`.
 @param outPayload receives the payload when the result is `Ok`, and is left
        untouched otherwise.
 @returns `Ok`, or why the archive is unusable. **Every non-`Ok` value is an
          error to report**, not a cache miss to absorb.

## `loadCookedShaders`

```cpp
int loadCookedShaders()
```

 Loads every cooked shader archive the virtual filesystem can see.

 Clears whatever was loaded before and re-scans `kCookedShaderDirectory`
 across all mounts, so this is what a game calls after mounting its packs —
 and what a test calls after remounting. **It is not automatic on mount**:
 the VFS has no change notification, and inventing one here would be T0058's
 job done badly. A build that never calls it simply has no cooked shaders,
 which in a development build is the status quo.

 @returns the number of archives loaded, or 0 when there are none. A
          malformed archive is logged with its reason and skipped, and does
          not stop the ones after it.

## `cookShaders`

```cpp
bool cookShaders(const std::string & hostPath)
```

 Writes everything this process knows how to render into a cooked archive.

 **This is the cook.** It seals the SPIR-V the process has compiled — plus
 anything already cooked and loaded, so re-cooking a partially cooked project
 does not lose entries — into one archive at a host path.

 *What set of variants that is* is the caller's problem and deliberately so:
 a variant exists because something asked for a pipeline, so the cook is
 "drive the content, then seal", the same shape Godot's shader baker has.
 Enumerating a project's variants exhaustively belongs to the export pipeline
 (T0043) and to T0151's bound on how many there are; neither exists yet, and
 pretending otherwise here would be the more expensive mistake.

 @param hostPath a real path on the host filesystem — **not** a virtual one.
        The cook is a build step writing into content, not a game writing a
        save, so it does not go through the VFS write directory.
 @returns whether the archive was written. False is logged with the reason.

## `cookedShadersOnly`

```cpp
bool cookedShadersOnly()
```

 Whether cooked shaders are authoritative in this process.

 When true, nothing is compiled: a variant that is not in a loaded archive is
 an error naming the shader, and the developer cache beside the executable is
 neither read nor written.

 The default is decided once, on first use:

 - `HP_COOKED_SHADERS=1` forces it on, `HP_COOKED_SHADERS=0` forces it off.
   The switch exists so the shipped behaviour can be exercised on a machine
   that does have a compiler, which is the only way it is ever tested.
 - Otherwise it is **on exactly when the slang runtime cannot be loaded**. A
   build with no compiler *is* a cooked-only build whether or not anybody
   told it so, and it should say the useful thing — "this shader was not
   cooked" — rather than the incidental one about a missing library.

 @returns whether cooked output is the only source of shader bytecode.

## `setCookedShadersOnly`

```cpp
void setCookedShadersOnly(bool only)
```

 Overrides `cookedShadersOnly` for this process.

 @param only whether cooked output is authoritative.
 @returns nothing.
