// The declared-parameter vocabulary (T0160.1). See `hp/ShaderParams.hpp` for
// what a module declares and why the buffer's name is the engine's while the
// field names are the author's.
//
// Almost nothing lives here: the *types* are the deliverable and the reflection
// that fills them is `SlangCompiler.cpp`'s, on the one compile that already
// happens. What is here is the slot-name table, which exists in one place so
// the signature, the shader and the binder cannot disagree about a string.

#include <hp/ShaderParams.hpp>

#include <array>

namespace hp {
namespace {

/// The slot names, spelled out rather than formatted per call.
///
/// **String literals, so the signature may hold the pointer.**
/// `PipelineResourceSignatureDesc` stores `const char*` and does not copy, so a
/// name built into a `std::string` local would dangle the moment the signature
/// was created — the exact bug shape that produces a resource matched by a name
/// nobody can read.
constexpr std::array<const char*, kShaderTextureSlots> kSlotNames = {
    "HpTexture0",
    "HpTexture1",
    "HpTexture2",
    "HpTexture3",
};

/// The matching immutable samplers, same rule.
constexpr std::array<const char*, kShaderTextureSlots> kSamplerNames = {
    "HpTexture0_sampler",
    "HpTexture1_sampler",
    "HpTexture2_sampler",
    "HpTexture3_sampler",
};

} // namespace

const char* shaderTextureSlotName(std::uint32_t slot) {
    if (slot >= kShaderTextureSlots) {
        return nullptr;
    }
    return kSlotNames[slot];
}

const char* shaderTextureSamplerName(std::uint32_t slot) {
    if (slot >= kShaderTextureSlots) {
        return nullptr;
    }
    return kSamplerNames[slot];
}

const ShaderParam* ShaderParamLayout::find(std::string_view name) const {
    for (const ShaderParam& param : params) {
        if (param.name == name) {
            return &param;
        }
    }
    return nullptr;
}

} // namespace hp
