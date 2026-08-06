// Cooked shaders: SPIR-V produced ahead of time and shipped as content
// (T0142.7, D34).
//
// **`Cook.hpp`'s invariant does not hold here, and inheriting it would be the
// bug.** That header opens by promising the binary form is *always safely
// discardable* — missing, stale, truncated or from the future, the correct
// answer is to re-cook from the YAML, never to fail. That promise is true
// because the YAML ships beside the cook.
//
// A cooked shader has no such source to fall back on. A shipped game carries
// neither `slangc` nor the slang runtime (T0142's Done-when: *"a shipped game
// links no Slang and reads cooked output only"*), so a missing cooked shader
// cannot be recovered by anything, at any cost. **It is fatal, and this layer
// says so** — loudly, naming the shader, instead of returning a status whose
// documented meaning is "cook it again".
//
// So nothing here calls `writeCook`/`readCook`. Only the byte primitives are
// reused (`writeU32`, `readString`, …), because two implementations of "write a
// u64" is exactly how an endianness decision gets made twice and differently.
//
// ---
//
// ## One producer, one key, one format — two locations, two contracts
//
// T0141.3 landed a persistent SPIR-V cache on Diligent's `IBytecodeCache`,
// beside the executable, keyed on a content hash of the shader and every
// transitive include plus the macros. The question 142.7 had to answer, before
// writing a line, was whether cooked output is *that* artefact, a superset of
// it, or a different thing. **It is the same artefact.** There is exactly one
// thing in this engine that turns `.slang` into SPIR-V — `compileSlangToSpirv`
// — and cooking is that function run early, with its output relocated into
// content. Two mechanisms that both produce SPIR-V is the outcome T0142.13
// exists to prevent; this is how it is prevented.
//
// What differs is not the bytes but the promise attached to them:
//
// | | dev cache (T0141.3) | cooked shaders (this file) |
// |---|---|---|
// | lives | beside the executable | in the VFS, in a pack (**D13**) |
// | written by | the process, as it compiles | the cook, ahead of time |
// | a miss means | compile it | **fatal** — there is nothing to compile with |
// | deleting it | costs a recompile | breaks the game |
// | authoritative | no | yes, and then the dev cache is not consulted at all |
//
// The last row is deliberate and it is the one worth arguing. In cooked-only
// mode the dev cache is neither read nor written: a stale developer cache
// silently standing in for content that failed to cook is precisely the
// "renders here, black on the player's machine" failure this whole ticket
// exists to make impossible.
//
// ## Cooked output is per-variant SPIR-V, and that is decided *with* T0151
//
// T0151's Done-when requires the shape of cooked output to be settled jointly,
// so it is settled here: **an entry is one opaque SPIR-V module for one
// variant, keyed by content hash.** Precompiled `.slang-module`s and link-time
// specialisation — T0151's mechanisms — are permitted as *inputs to the cook*
// and forbidden as *shipped artefacts*, for one decisive reason: linking at
// load requires the slang runtime in the shipped game, which is the thing
// T0142's Done-when forbids and the thing this file exists to remove. T0151 may
// make the cook faster and the variant set smaller; it may not change what a
// player's machine has to be able to do, which is `memcpy` and nothing else.
//
// ## Why the engine's shader *source* still ships (and `hp_embed_shaders`
// survives)
//
// The lookup key is a content hash **over the resolved source text**. Computing
// it needs the source — so a cooked build still reads `HpSurface.slang`, every
// header it includes, and the game's own module, and only then finds the
// bytecode. That is not a flaw to route around: it is what makes editing any
// header invalidate the key with no staleness rule to remember, and it is why
// there is one key mechanism rather than a second, cheaper, lie-prone one.
//
// The practical consequence is that `cmake/hp_embed_shaders.cmake` is **not**
// replaced by cooking, which T0142.13 had listed as an open possibility. Engine
// shader source stays compiled into the binary (a few tens of kilobytes);
// cooked SPIR-V is the thing that ships as content beside it.
#pragma once

#include <hp/Api.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hp {

/// The container layout version of a cooked shader archive.
///
/// Bumped when the *header* below changes. A reader refuses anything it does
/// not recognise rather than guessing — and in a cooked build that refusal is
/// terminal for rendering, so it is reported rather than swallowed.
inline constexpr std::uint32_t kCookedShaderFormatVersion = 1;

/// Where cooked shader archives live in the virtual tree (**D13**).
///
/// Every archive under this directory is loaded, from every mount, and their
/// entries merge — which is what makes a DLC pack that adds a material also
/// able to add its shader without republishing the base game's archive.
/// **Archive file names must therefore be unique across packs**: two packs
/// shipping `base.hpsv` collide on the path and only the first-mounted one is
/// read. Name an archive after the pack that carries it.
inline constexpr std::string_view kCookedShaderDirectory = "shaders/cooked";

/// The extension a cooked shader archive carries.
inline constexpr std::string_view kCookedShaderExtension = ".hpsv";

/// Why a cooked shader archive could not be used.
///
/// **Unlike `CookStatus`, none of these means "cook it again".** A shipped
/// build cannot; every value here is a reason the game will not render, and is
/// reported as such.
enum class CookedShaderStatus : std::uint8_t {
    /// Usable.
    Ok,

    /// Too short to hold a header, or the magic does not match. Also what an
    /// empty or truncated file looks like.
    NotACookedShaderArchive,

    /// Written by a different container version.
    FormatMismatch,

    /// Cooked by a different shader compiler than this build loads. Bytecode
    /// produced by one compiler is not evidence about another, so it is
    /// refused rather than trusted.
    CompilerMismatch,

    /// The header is well-formed and the payload is shorter than it claims.
    Truncated,
};

/// @param status a cooked-archive status.
/// @returns a short human-readable reason, for logs.
[[nodiscard]] HP_API const char* describe(CookedShaderStatus status);

/// Identifies the compiler whose output this build can consume.
///
/// The pinned slang version. It is written into every archive and checked on
/// load, because a pin bump changes the bytecode a given source produces —
/// which reads as a cold cache in development and must read as a **refusal**
/// in a cooked build rather than as bytecode from a compiler nobody has.
///
/// @returns the compiler identity, stable for the lifetime of the process.
[[nodiscard]] HP_API std::string_view shaderCompilerId();

/// Wraps a cooked payload in an archive container.
///
/// Layout, little-endian throughout — chosen rather than assumed, exactly as
/// `Cook.hpp` argues:
///
/// ```text
///   magic        8 bytes   "HPSHADER"
///   format       u32       kCookedShaderFormatVersion
///   compilerId   string    length-prefixed; `shaderCompilerId()` when written
///   payloadSize  u64       bytes that follow
///   payload      ...       the variant store, opaque to this layer
/// ```
///
/// @param payload the cooked bytes.
/// @param compilerId the compiler that produced them; normally
///        `shaderCompilerId()`.
/// @returns the complete file contents, ready to write.
[[nodiscard]] HP_API std::vector<std::byte> writeCookedShaderArchive(
    const std::vector<std::byte>& payload, std::string_view compilerId);

/// Reads and validates a cooked shader archive.
///
/// @param bytes the file contents.
/// @param expectedCompilerId the compiler this build can consume; normally
///        `shaderCompilerId()`.
/// @param outPayload receives the payload when the result is `Ok`, and is left
///        untouched otherwise.
/// @returns `Ok`, or why the archive is unusable. **Every non-`Ok` value is an
///          error to report**, not a cache miss to absorb.
[[nodiscard]] HP_API CookedShaderStatus readCookedShaderArchive(
    const std::vector<std::byte>& bytes, std::string_view expectedCompilerId,
    std::vector<std::byte>& outPayload);

/// Loads every cooked shader archive the virtual filesystem can see.
///
/// Clears whatever was loaded before and re-scans `kCookedShaderDirectory`
/// across all mounts, so this is what a game calls after mounting its packs —
/// and what a test calls after remounting. **It is not automatic on mount**:
/// the VFS has no change notification, and inventing one here would be T0058's
/// job done badly. A build that never calls it simply has no cooked shaders,
/// which in a development build is the status quo.
///
/// @returns the number of archives loaded, or 0 when there are none. A
///          malformed archive is logged with its reason and skipped, and does
///          not stop the ones after it.
HP_API int loadCookedShaders();

/// Writes everything this process knows how to render into a cooked archive.
///
/// **This is the cook.** It seals the SPIR-V the process has compiled — plus
/// anything already cooked and loaded, so re-cooking a partially cooked project
/// does not lose entries — into one archive at a host path.
///
/// *What set of variants that is* is the caller's problem and deliberately so:
/// a variant exists because something asked for a pipeline, so the cook is
/// "drive the content, then seal", the same shape Godot's shader baker has.
/// Enumerating a project's variants exhaustively belongs to the export pipeline
/// (T0043) and to T0151's bound on how many there are; neither exists yet, and
/// pretending otherwise here would be the more expensive mistake.
///
/// @param hostPath a real path on the host filesystem — **not** a virtual one.
///        The cook is a build step writing into content, not a game writing a
///        save, so it does not go through the VFS write directory.
/// @returns whether the archive was written. False is logged with the reason.
HP_API bool cookShaders(const std::string& hostPath);

/// Whether cooked shaders are authoritative in this process.
///
/// When true, nothing is compiled: a variant that is not in a loaded archive is
/// an error naming the shader, and the developer cache beside the executable is
/// neither read nor written.
///
/// The default is decided once, on first use:
///
/// - `HP_COOKED_SHADERS=1` forces it on, `HP_COOKED_SHADERS=0` forces it off.
///   The switch exists so the shipped behaviour can be exercised on a machine
///   that does have a compiler, which is the only way it is ever tested.
/// - Otherwise it is **on exactly when the slang runtime cannot be loaded**. A
///   build with no compiler *is* a cooked-only build whether or not anybody
///   told it so, and it should say the useful thing — "this shader was not
///   cooked" — rather than the incidental one about a missing library.
///
/// @returns whether cooked output is the only source of shader bytecode.
[[nodiscard]] HP_API bool cookedShadersOnly();

/// Overrides `cookedShadersOnly` for this process.
///
/// @param only whether cooked output is authoritative.
/// @returns nothing.
HP_API void setCookedShadersOnly(bool only);

} // namespace hp
