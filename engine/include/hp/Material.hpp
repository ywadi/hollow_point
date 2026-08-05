// Material assets: what a surface looks like, as data (T0060).
//
// **The vocabulary is not ours and was not designed here.** D24 adopted
// DiligentFX's `PBR_Renderer`, and T0134 settled that its
// `PBRMaterialShaderAttribs` *is* the engine's material model rather than
// something to wrap or parallel. Every field below maps onto one of its members,
// and the mapping is the point: a parameter this struct invents is a parameter
// nothing shades.
//
// What this adds on top of Diligent's own `GLTF::Material` is the half that
// makes it an **asset**:
//
// - **Identity.** A material is addressed by `Guid` like every other asset
//   (T0021/T0023), so a scene, a mesh slot and a patch can all name the same one.
//   `GLTF::Material` is an index into one model's table and means nothing
//   outside it.
// - **Textures by GUID, not by index.** The same reason. A glTF material's
//   texture ids point into that model's own array; a material asset points at
//   `TextureAsset`s in the pool, so two models can share a texture and a
//   material can reference one that no model imported.
// - **Reflection**, so serialization (T0022) and the inspector (T0035) come for
//   free rather than being written per field. That is D23's rule and it is why
//   there is no `writeMaterial` switch anywhere below.
//
// `GLTF::Material` is still what the *importer* produces and what the renderer
// binds today; this is the authoring side, exactly as `hp::Light` is the
// authoring side of `PBRLightAttribs`.
//
// ---
//
// ## Which texture slots are here, and which are deliberately not
//
// Diligent defines **17** texture attributes. This carries **five**, and the
// missing twelve are not an oversight:
//
// | Slot | Here | Why |
// |---|---|---|
// | base colour, metallic-roughness, normal, occlusion, emissive | **yes** | `DefaultTextureAttributes`' metallic-roughness set — the five the renderer binds with the engine's current `CreateInfo` |
// | clearcoat ×3, sheen ×2, anisotropy, iridescence ×2, transmission, thickness | no | **Extended materials, off by D24.** Each one widens the PSO permutation space *and* the material attribs buffer whether or not a material uses it. Turning one on is a decision to argue on its own ticket |
// | diffuse, specular-glossiness | no | The legacy spec-gloss workflow. glTF 2.0 core is metallic-roughness; supporting both means two shading paths for one result |
//
// **There is no displacement or height slot, and Diligent does not have one.**
// `PBR_Renderer` has no parallax-occlusion or tessellation path at all, so a
// `displacementTexture` field here would be a field nothing reads — which is the
// mistake `Camera::cullingMask` spent three tickets being. Height-mapped
// surfaces need shader work, not a material field, and that belongs with T0141.
//
// **Metallic and roughness are one texture, not two**, and that is glTF's
// packing rather than a simplification: roughness in green, metallic in blue, of
// a single `metallicRoughnessTexture`. A DCC tool that exports them separately
// needs them packed at import time (T0023's business), not two slots here —
// two would have to be combined before they could be sampled anyway.
#pragma once

#include <hp/Api.hpp>
#include <hp/Assets.hpp>
#include <hp/Guid.hpp>
#include <hp/Math.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace hp {

/// How a material's alpha is interpreted.
///
/// Matches `GLTF::Material::ALPHA_MODE` value for value, because it is written
/// straight into `PBRMaterialShaderAttribs::AlphaMode`.
enum class AlphaMode : std::uint8_t {
    /// Alpha is ignored; the surface is fully opaque. The cheapest, and the
    /// default, because most surfaces are.
    Opaque,

    /// Alpha is a **cutout**: a fragment is drawn or discarded by comparing it
    /// with `Material::alphaCutoff`. Foliage and chain-link fences.
    ///
    /// **T0086 needs this one specifically.** An alpha-tested shadow caster has
    /// to discard in the depth pass too, or a leaf casts the shadow of the quad
    /// it is drawn on.
    Mask,

    /// Alpha blends. Correct-looking and the most expensive, because it needs
    /// sorting and cannot write depth the way the other two do — which is
    /// T0045's problem, and why blend mode is a **sort key** and not just a
    /// shading detail.
    Blend,
};

/// A material asset: the parameters one surface is shaded with (60.1).
///
/// Plain data with no GPU resources of its own, deliberately — the textures it
/// names are separate assets with their own lifetimes, so a material can be
/// loaded, edited, copied and serialized with no device at all. That is what
/// lets the whole of this be tested in the fast bucket.
///
/// Every field maps onto `PBRMaterialShaderAttribs`; see the header comment for
/// what is absent and why.
struct Material {
    /// Multiplied into the base colour texture, or used alone when there is
    /// none. Linear RGB, with alpha meaning what `alphaMode` says it does.
    ///
    /// White by default, so a material with a texture and nothing else set shows
    /// the texture unchanged.
    float4 baseColour{1.0F, 1.0F, 1.0F, 1.0F};

    /// Light the surface emits regardless of what falls on it. Linear RGB.
    ///
    /// **Black by default, and that is load-bearing**: emissive is added on top
    /// of shading, so any other default would make every material glow.
    float3 emissive{0.0F, 0.0F, 0.0F};

    /// How metallic the surface is, in [0, 1]. Multiplied into the blue channel
    /// of `metallicRoughnessTexture` when there is one.
    float metallic = 1.0F;

    /// How rough the surface is, in [0, 1]. Multiplied into the green channel of
    /// `metallicRoughnessTexture` when there is one.
    ///
    /// **One is Diligent's default and glTF's**, not a matte-surface preference:
    /// a material that sets a texture and nothing else must come out as the
    /// texture says, and a factor of 1 is what leaves it alone.
    float roughness = 1.0F;

    /// Scales the normal map's effect. 0 flattens it, 1 uses it as authored.
    /// Ignored without a `normalTexture`.
    float normalScale = 1.0F;

    /// How strongly `occlusionTexture` darkens ambient light, in [0, 1].
    float occlusionStrength = 1.0F;

    /// How alpha is interpreted.
    AlphaMode alphaMode = AlphaMode::Opaque;

    /// The threshold `AlphaMode::Mask` compares against. Ignored otherwise.
    float alphaCutoff = 0.5F;

    /// Whether back faces are drawn. Off by default: a closed mesh never needs
    /// them, and drawing them costs fill rate for nothing.
    bool doubleSided = false;

    /// Whether the surface ignores lighting entirely and shows `baseColour`
    /// straight out. Maps to `PBR_WORKFLOW_UNLIT`.
    ///
    /// **Not a convenience switch.** 60.10's fallback material has to be
    /// unlit — a magenta surface standing in shadow reads as plausible art,
    /// and an unlit one cannot — and T0106.7 will want it for VFX that must not
    /// be shaded by scene lights.
    bool unlit = false;

    /// Base colour texture. Default means none, and `baseColour` alone is used.
    ///
    /// **Default is not an error and never renders the fallback pattern.**
    /// "This material has no base colour map" is the normal state of most
    /// materials; "the asset this GUID names could not be loaded" is the error,
    /// and the two must not look alike.
    Guid baseColourTexture;

    /// Roughness in green, metallic in blue — glTF's packing, sampled as one
    /// texture. See the header comment on why this is not two fields.
    Guid metallicRoughnessTexture;

    /// Tangent-space normal map, scaled by `normalScale`.
    Guid normalTexture;

    /// Ambient occlusion, applied at `occlusionStrength`.
    Guid occlusionTexture;

    /// Emitted light, multiplied by `emissive`.
    Guid emissiveTexture;
};

/// The stable pool name for a material.
template <>
struct AssetTraits<Material> {
    /// Matches `assetKindName(AssetKind::Material)`.
    static constexpr const char* name = "Material";
};

/// The schema version written into a `.hpmat` document.
///
/// The **material** schema, versioned separately from the scene's: the two
/// change for unrelated reasons, and one number for both would force a scene
/// migration every time a material gained a field.
inline constexpr std::uint32_t kMaterialSchemaVersion = 1;

/// The extension a material asset is stored under.
///
/// `.hpmat` rather than `.mat`, which belongs to several other engines and to
/// MATLAB, and rather than `.yaml`, which would make every material look like
/// configuration to every tool that sorts by type.
inline constexpr const char* kMaterialExtension = ".hpmat";

/// Reflects `Material` and `AlphaMode`.
///
/// Idempotent, and called by everything here that needs reflection, so a caller
/// that only ever loads a material never has to know this exists. Exposed
/// because the inspector (T0035) and any gameplay module that resolves the type
/// by name need the same guarantee.
/// @returns nothing.
HP_API void registerMaterialTypes();

/// Serialises a material to YAML (60.1, via T0020).
///
/// @param material the material to write.
/// @returns the document text.
[[nodiscard]] HP_API std::string writeMaterial(const Material& material);

/// Parses a `.hpmat` document.
///
/// **Reading is lenient in the same way scenes are** (see
/// `documentation/10-scene-file-format.md`): a field the document omits keeps
/// its default, and a field this build does not have is ignored. So the minimum
/// valid material is a version and an empty map, and adding a parameter does not
/// invalidate a single file already written.
///
/// @param yaml the document contents.
/// @param name a name for error messages, usually the virtual path.
/// @returns the material, or nothing when the text does not parse or its schema
///          version is newer than this build's — the same refusal a scene makes,
///          and for the same reason: loading the half it understands would write
///          the loss back on the next save.
[[nodiscard]] HP_API std::optional<Material> parseMaterial(std::string_view yaml,
                                                           std::string_view name = "<memory>");

/// Loads a material through the VFS (D13).
///
/// @param virtualPath the material's path in the mount tree.
/// @returns the material, or nullptr when the file is missing or unreadable.
///          **Not fatal**: the caller substitutes the fallback, which is the
///          whole of 60.10.
[[nodiscard]] HP_API std::shared_ptr<Material> loadMaterial(std::string_view virtualPath);

} // namespace hp
