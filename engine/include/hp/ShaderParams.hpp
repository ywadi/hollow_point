// What a game's shader module **declares**, and what a material **fills in**
// (T0160, T0161, D28, D35).
//
// **This is the half of the material model that is not the engine's.** Every
// other parameter a surface has — base colour, roughness, the five texture
// slots, the height map — is a field of `hp::Material`, named by the engine,
// meaning the same thing on every material in every game. That vocabulary
// cannot grow to fit a technique the engine has never heard of, and until this
// existed a game shader's own knobs had nowhere to live: the first material
// shader ever written here hard-coded its one parameter as a compile-time
// literal and said so in its own comment.
//
// So a module declares its parameters *and its resources* in Slang, the engine
// reflects them, and a `.hpmat` carries values and bindings by name. Godot's
// `uniform float x : hint_range(0,1)` and `uniform sampler2D detail` in a
// language that can express the hint as an attribute.
//
// ---
//
// ## The declaration, in a module
//
// ```slang
// Texture2DArray detailAlbedo;      // the author's name, at any count (T0161)
//
// cbuffer HpMaterialParams
// {
//     [HpRange(0.0, 1.0)]
//     [HpTooltip("Where the height field sits relative to the polygon.")]
//     float referencePlane;
//
//     [HpColor]
//     float3 tint;
// }
//
// struct HpMaterial : IHpMaterial { /* ... reads referencePlane ... */ }
// ```
//
// and in the `.hpmat` beside it:
//
// ```yaml
// params:
//   - name: referencePlane
//     value: 0.75
// textures:
//   - name: detailAlbedo
//     texture: 1570000000000031
// ```
//
// **The attribute definitions live in `HpMaterial.slang`**, which the engine
// includes before the module, so a module still includes nothing (D27).
//
// ## Where a module's resources live (T0161, D35)
//
// **In a second, per-module resource signature, built from the reflection of
// the compile that already happens** and bound beside the engine's base one.
// The base signature carries only the engine's own vocabulary; everything an
// author declares — textures today, buffers when the compute and post-process
// stages land — is reflected off the module's compiled code under the author's
// own names, at whatever count the module wants. `kShaderParamsBlock` is the
// one reserved name left: the *block* is the engine's so a `.hpmat`'s values
// have one place to go, and the fields inside it are the author's.
//
// Sampler *state* is the one deliberate limit (D35): a module samples through
// the engine's immutable palette — `HpSamplerLinearWrap` and friends, declared
// in `HpMaterial.slang` — so filtering travels inside the SPIR-V and a cooked
// build needs no metadata to carry it. A module declaring its own
// `SamplerState` is refused by name, with the palette named in the log.
//
// ## Three rules, each of which is a measurement rather than a preference
//
// **Declarations must be unconditional and at module top level.** A declaration
// inside a permutation `#if` makes "what does this module declare" a question
// with one answer per macro set — `rock_pom.slang`'s entire body sits inside
// `#if HP_USE_HEIGHT_MAP && USE_TEXCOORD0` — and neither the `.hpmat` nor the
// inspector can be permutation-dependent.
//
// **Values are data, not permutations.** Changing one rebuilds no pipeline and
// invalidates no cook: the block is a constant buffer written per draw, exactly
// as the engine's own material attribs are. Module *identity* already keys the
// pipeline cache, so parameters add no PSO axis at all — recorded against
// T0151, whose rule is now "prefer a runtime parameter over a permutation bit".
//
// **A module that declares nothing costs nothing.** No module signature is
// built, no second commit happens, and a plain material's descriptors are
// *fewer* than they were when four engine-named slots sat in the shared
// signature for every pipeline (T0161's counted budget: a plain glTF material
// dropped from 10 sampled images and 11 immutable samplers to 6 and 7).
#pragma once

#include <hp/Api.hpp>
#include <hp/Reflect.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hp {

/// The reserved name of the constant buffer a module declares its parameters in.
///
/// **Engine-owned, because the *signature* has to name it before any module
/// exists.** A pipeline resource signature is created once, at renderer
/// construction, and Diligent matches a shader's resources to it **by name** —
/// so the buffer's name is the engine's and the field names inside it are the
/// author's. That split is the whole trick: fields are not resources, so they
/// cost the signature nothing and a module may name and order them freely.
inline constexpr const char* kShaderParamsBlock = "HpMaterialParams";

/// The engine's sampler palette: the sampler names a module may sample with
/// (T0161, D35).
///
/// **Sampler *state* is the engine's vocabulary; which sampler to use is the
/// author's choice, made by naming one of these in code.** The names are
/// declared in `HpMaterial.slang` and backed by immutable samplers in the base
/// resource signature, so the choice travels inside the SPIR-V and a cooked
/// build carries no sampler metadata at all — the one thing SPIR-V cannot
/// express never needs expressing. Godot ships the identical restriction.
///
/// `Aniso` is 8x, wrap and clamp both. A module declaring its own
/// `SamplerState` is refused by name at pipeline build, with this palette in
/// the log line; widening it — a compare sampler, an author-set aniso level —
/// is additive when something real asks (recorded on D35).
inline constexpr const char* kShaderSamplerPalette[] = {
    "HpSamplerLinearWrap",  "HpSamplerLinearClamp", "HpSamplerPointWrap",
    "HpSamplerPointClamp",  "HpSamplerAnisoWrap",   "HpSamplerAnisoClamp",
};

/// The largest parameter block a module may declare, in bytes.
///
/// The engine binds **one** buffer of this size to every material, so a
/// module's block may be smaller and never larger. 256 bytes is sixteen
/// `float4`s, which is more than any technique in the 2026-08-06 capability
/// audit wanted; a module that overruns it is reported by name at compile time
/// and its parameters are not written, rather than the buffer being silently
/// truncated into whatever the shader reads past the end.
inline constexpr std::uint32_t kShaderParamsMaxBytes = 256;

/// What a declared parameter is, in the shader.
///
/// Deliberately the small set a material inspector can edit and a `.hpmat` can
/// hold: scalars and vectors of up to four components. A matrix, an array or a
/// struct in the block is reported as unsupported and skipped by name — not
/// silently mis-sized, which is the failure a "close enough" mapping produces.
enum class ShaderParamType : std::uint8_t {
    /// A single `float`.
    Float,

    /// `float2`.
    Float2,

    /// `float3`.
    Float3,

    /// `float4`.
    Float4,

    /// A signed 32-bit `int`. Authored as a number and written as an integer.
    Int,

    /// A `bool`. Occupies four bytes in the block, as HLSL and Slang define it.
    Bool,
};

/// A value authored for a declared parameter: one to four floats.
///
/// **One type for every parameter, and the arity is carried rather than
/// inferred**, so `value: 0.5` and `value: [0.5, 0, 0, 0]` are different
/// documents that round-trip to themselves. The *shader's* declared type is
/// what decides how many components are written into the block; this is what
/// the file said, and normalising it away at load would rewrite a person's
/// file on the next save.
///
/// Integers and booleans ride the same four floats and are converted at the
/// point of writing, from the declared type — so a `.hpmat` never has to say
/// what type a parameter is, which is the shader's business and would be a
/// second source of truth here.
struct ShaderValue {
    /// The components, in order. Unwritten components are zero.
    float components[4] = {0.0F, 0.0F, 0.0F, 0.0F};

    /// How many components the document carried, 1 to 4.
    std::uint8_t count = 1;
};

/// One parameter a module declared, as reflection reports it.
struct ShaderParam {
    /// The field's name in the module's block. The key a `.hpmat` uses.
    std::string name;

    /// What the shader declared it as.
    ShaderParamType type = ShaderParamType::Float;

    /// Byte offset into the parameter block, from slang's own layout rules.
    ///
    /// **Not computed here, on purpose.** Constant-buffer packing has rules
    /// (a `float3` is 12 bytes with 16-byte alignment, a `float2` straddles or
    /// does not depending on what precedes it) and a second implementation of
    /// them is a class of bug that renders as parameters reading each other's
    /// bytes. The compiler that laid the buffer out is the one that says where
    /// each field is.
    std::uint32_t offset = 0;

    /// The field's size in bytes, as laid out.
    std::uint32_t size = 0;

    /// Inclusive range from `[HpRange(lo, hi)]`. Equal values mean unbounded,
    /// matching `PropertyMeta`.
    double min = 0.0;

    /// The range's upper bound; see `min`.
    double max = 0.0;

    /// Text from `[HpTooltip("…")]`, or empty.
    std::string tooltip;

    /// Whether `[HpColor]` asked for a colour swatch rather than three or four
    /// number boxes. Presentation only — the bytes written are the same.
    bool colour = false;

    /// The same editor metadata a reflected C++ property carries (T0053).
    ///
    /// **This is the unification D28 anticipated in as many words**: "the
    /// inspector consumes a single description regardless of which reflection
    /// produced it". A material's `roughness` comes from `entt::meta` and its
    /// `referencePlane` comes from slang, and an inspector that had to know
    /// which is which would be the second mechanism D23 exists to prevent.
    ///
    /// The returned `tooltip` points into this parameter's own storage, so it
    /// is valid exactly as long as this object is — copy the `ShaderParam`, not
    /// the `PropertyMeta`.
    /// @returns the metadata view.
    [[nodiscard]] PropertyMeta meta() const {
        return PropertyMeta{min, max, tooltip.c_str(), /*read_only = */ false,
                            /*hidden = */ false};
    }
};

/// One texture a module's compiled code samples, under the author's own name
/// (T0161).
///
/// **The name is the author's, not the engine's** — `detailAlbedo`, not
/// `HpTexture0` — because the resource lives in the per-module signature built
/// from this very reflection, so no name has to exist before the module does.
/// It is the key a `.hpmat`'s `textures:` list binds by, and the label an
/// inspector row shows.
struct ShaderTextureSlot {
    /// The texture's declared name in the module.
    std::string name;
};

/// Everything a module declares, reflected once and stored with its pipeline.
///
/// Empty for the standard material and for a module that declares nothing —
/// which is the common case, and which the writer treats as "skip the whole
/// parameter path" rather than "write zeroes".
struct ShaderParamLayout {
    /// The declared parameters, in the order the module declared them.
    std::vector<ShaderParam> params;

    /// The textures the module's compiled code samples, by the author's names
    /// (T0161). **The used set, not the declared set**: reflection reads the
    /// compiled shader, and slang strips a declared resource nothing samples —
    /// so a texture referenced only inside an inactive permutation `#if` is
    /// absent here for that permutation. Declarations are unconditional by the
    /// rule above; *uses* may not be, which is why a `.hpmat` naming a texture
    /// this list lacks is leniently ignored rather than an error.
    std::vector<ShaderTextureSlot> textures;

    /// The block's size in bytes, as slang laid it out. Zero when the module
    /// declares no parameters.
    std::uint32_t blockBytes = 0;

    /// @returns whether the module declared anything at all.
    [[nodiscard]] bool empty() const { return params.empty() && textures.empty(); }

    /// Looks a parameter up by the name a `.hpmat` used.
    ///
    /// @param name the parameter's declared name.
    /// @returns the parameter, or nullptr when this module does not declare
    ///          one by that name. **Not an error**: a `.hpmat` may name a
    ///          parameter a later revision of the shader removed, and dropping
    ///          the value is the same leniency every other field has.
    [[nodiscard]] HP_API const ShaderParam* find(std::string_view name) const;
};

} // namespace hp
