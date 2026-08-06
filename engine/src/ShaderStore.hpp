// The one place compiled SPIR-V is looked up and kept (T0141.3 + T0142.7).
//
// **There is exactly one store, and it holds two things with different
// contracts** — cooked archives that ship as content, and a developer cache
// beside the executable. `hp/ShaderCook.hpp` argues why they are one mechanism
// rather than two, and what a miss means in each. This header is only the seam
// `SlangCompiler.cpp` reaches it through: describe a variant, ask for its
// bytecode, hand back what you compiled.
//
// The key is Diligent's content hash over the resolved source text of the
// shader and every transitive include, plus the macros — which is why the
// caller passes a *description* of the variant rather than a hash it computed
// itself. One hashing rule, in one file.
#pragma once

#include "SlangCompiler.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace Diligent {
struct IShaderSourceInputStreamFactory;
} // namespace Diligent

namespace hp {

/// Everything that identifies one shader variant.
///
/// Two inputs are invisible to a hash of the source text and ride along
/// explicitly: the **prelude**, which `compileSlangToSpirv` prepends rather
/// than includes so the walker never sees it, and the **driving schema**
/// (`kSlangDrivingSchema`), which is compile-request state rather than source.
struct ShaderVariantKey {
    /// The shader's file name as the source factory knows it.
    const char* filePath = nullptr;

    /// Which stage it is compiled for.
    ShaderStage stage = ShaderStage::Pixel;

    /// The entry point's name in the source (T0146.5).
    ///
    /// **Part of the key since the engine owns both stages.** `HpSurface.slang`
    /// carries `vsMain` and `psMain` in one file, so the file name no longer
    /// identifies a variant on its own — and while `stage` already separates
    /// those two, a key that named only the file and the stage would collide
    /// the moment anything compiled two entry points of the *same* stage from
    /// one source. Cheap to carry, and the alternative is a cache that serves
    /// the wrong function with no symptom but a wrong image.
    const char* entryPoint = "main";

    /// The permutation's preprocessor definitions.
    const SlangMacro* macros = nullptr;

    /// How many.
    std::size_t macroCount = 0;

    /// Text prepended to the source before compiling; hashed, not walked.
    std::string_view prelude;

    /// Resolves the file and its includes — the same compound factory the
    /// compiler uses, so the hash covers exactly what the compiler saw.
    Diligent::IShaderSourceInputStreamFactory* sources = nullptr;
};

/// Whether looking a variant up is worth the include walk.
///
/// False only when there are no cooked archives, the developer cache is
/// disabled and cooked output is not authoritative — in which case hashing
/// would read every include for a lookup that cannot hit.
///
/// @param cookedOnly the already-resolved `cookedShadersOnly()` answer. Passed
///        in rather than queried, because resolving it may load the slang
///        runtime and the caller holds the compile lock.
/// @returns whether `shaderStoreLookup` should be called.
[[nodiscard]] bool shaderStoreActive(bool cookedOnly);

/// Looks a variant up: cooked archives first, then the developer cache.
///
/// **In cooked-only mode the developer cache is not consulted**, deliberately —
/// see `hp/ShaderCook.hpp`. A stale dev cache standing in for content that
/// failed to cook is the "renders here, black on the player's machine" bug this
/// whole path exists to prevent.
///
/// @param key what to look for.
/// @param cookedOnly whether cooked output is authoritative.
/// @param outSpirv receives the bytecode on a hit, and is left untouched
///        otherwise.
/// @returns whether the variant was found.
[[nodiscard]] bool shaderStoreLookup(const ShaderVariantKey& key, bool cookedOnly,
                                     std::vector<std::uint8_t>& outSpirv);

/// Records a freshly compiled variant in the developer cache and writes the
/// cache through to disk.
///
/// A no-op in cooked-only mode: nothing compiles there, so nothing can be
/// recorded.
///
/// @param key what was compiled.
/// @param spirv the bytecode.
/// @returns nothing.
void shaderStoreInsert(const ShaderVariantKey& key, const std::vector<std::uint8_t>& spirv);

} // namespace hp
