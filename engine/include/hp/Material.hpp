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
// **The height slot is the engine's own, not one of Diligent's 17** (T0141.7).
// `PBR_Renderer` has no parallax-occlusion path — that was the ceiling D26
// lifted — so `heightTexture` binds to a resource `SurfacePipeline` adds to
// the signature itself, and the parallax runs in the engine's surface stage.
// The field arrived *with* the shader that reads it, per the rule that a field
// nothing reads is the mistake `Camera::cullingMask` spent three tickets being.
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
#include <hp/ShaderParams.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

/// How a texture is addressed outside the 0..1 range.
///
/// Values match `Diligent::TEXTURE_ADDRESS_MODE` minus one, which is the packing
/// `GLTF::Material::TextureShaderAttribs::SetWrapUMode` uses.
enum class TextureWrap : std::uint8_t {
    /// Tiles. The default, and what a surface texture almost always wants.
    Repeat,

    /// Tiles, flipping every other repeat. Hides a seam in a non-tiling texture.
    Mirror,

    /// Clamps to the edge pixel. What a decal, a lightmap or an atlased texture
    /// wants — **tiling one of those bleeds a neighbour's pixels in**.
    Clamp,
};

/// One UV channel: how the mesh's texture coordinates are transformed before use.
///
/// **Per channel rather than per texture slot, which is Godot's shape and not
/// Diligent's.** `TextureShaderAttribs` carries a transform on every one of the
/// 17 slots independently; exposing that directly would mean five copies of the
/// same four numbers in the common case, where every map on a surface shares one
/// tiling. Writing a channel's settings into each slot that selects it is one
/// loop in the renderer and recovers the general case for anything that ever
/// needs it.
///
/// Two channels because `PBR_Renderer` has two — `PSO_FLAG_USE_TEXCOORD0` and
/// `USE_TEXCOORD1` — and `SelectUV` lerps between exactly those.
struct UvChannel {
    /// Tiling. `[2, 2]` repeats the texture twice over the mesh's UVs.
    float2 scale{1.0F, 1.0F};

    /// Shift, applied after scaling. Scrolling water is this animated.
    float2 offset{0.0F, 0.0F};

    /// Rotation about the UV origin, in **radians**, matching every other angle
    /// in the engine (`Light::innerConeAngle`, `Camera::verticalFov`).
    float rotation = 0.0F;

    /// How U repeats outside 0..1.
    TextureWrap wrapU = TextureWrap::Repeat;

    /// How V repeats outside 0..1.
    TextureWrap wrapV = TextureWrap::Repeat;
};

/// One value this material gives a parameter its shader module declared
/// (T0160.3).
///
/// **Name-keyed, not typed fields**, and that is the whole difference between
/// this and every other member of `Material`: the engine does not know what
/// parameters exist until it reflects the module, so it cannot have a field per
/// one. The name is the shader's declared field name; what it *means* is the
/// shader's business, and this struct deliberately knows nothing about it.
///
/// A name the module does not declare is dropped when the material is written
/// to the GPU — the same leniency a `.hpmat` field this build does not have
/// gets, and it is what lets a shader lose a parameter without invalidating
/// every material that set it.
struct MaterialParam {
    /// The parameter's name, as the module's `HpMaterialParams` block declares
    /// it.
    std::string name;

    /// The authored value, 1 to 4 components. Converted to the shader's
    /// declared type when it is written.
    ShaderValue value;
};

/// One texture this material binds to a texture the module declares
/// (T0160.3, author-named since T0161).
struct MaterialTexture {
    /// The texture's name, **as the module declared it** — `detailAlbedo`,
    /// not an engine identifier. The per-module resource signature is built
    /// from the module's own reflection (T0161, D35), so no name has to
    /// exist before the module does. T0160's `HpTexture0` … `HpTexture3`
    /// still work: they are ordinary declarations in the contract file now,
    /// deprecated, and bind through the same walk.
    std::string name;

    /// The `TextureAsset` bound there. Default binds the renderer's white
    /// default; a GUID that does not resolve binds the checkerboard — the
    /// same two answers every engine-named slot gives.
    Guid texture;
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

    /// Height map for parallax occlusion (T0141.7). White is high, black is
    /// deep, red channel read. Default means none, and the surface is flat.
    ///
    /// **Samples and displaces UV0**, deliberately: the offset is computed in
    /// UV0's space, and applying one channel's offset to another channel's
    /// unwrap would be wrong for exactly the lightmap-style case UV1 exists
    /// for. A mesh without texture coordinates renders flat — parallax has
    /// nothing to displace — **unless the material is `triplanar`** (T0156),
    /// where the height field is projected and marched along each world axis
    /// exactly as the colour-like maps are sampled, and no UVs are needed.
    Guid heightTexture;

    /// How deep the parallax reads, in texture-space units at full height
    /// range — UV0 units normally, projection units (`1 / triplanarScale`
    /// metres) under `triplanar`, so the relief's apparent depth tracks the
    /// texture's tiling in both cases.
    ///
    /// 0.04 is a strong effect on tiling surfaces; 0 disables the march's
    /// effect without changing the pipeline. Ignored without `heightTexture`.
    float heightScale = 0.04F;

    /// Project textures from world space along the three axes instead of
    /// sampling the mesh's UVs (T0141.8).
    ///
    /// **This is what textures a mesh that has no usable unwrap** — terrain,
    /// CSG blockouts, kitbashed geometry — and it needs no texture coordinates
    /// at all. Applies to the colour-like maps (base colour,
    /// metallic-roughness, occlusion, emissive) and to `heightTexture`'s
    /// parallax, which marches each projection in its own world-axis frame
    /// (T0156). The **normal map is ignored** while this is set — reorienting
    /// a tangent-space map per projection plane is real work that arrives
    /// when something needs it. The per-slot UV selectors and channel
    /// transforms are all bypassed.
    bool triplanar = false;

    /// Texture tiles per metre when `triplanar` is set.
    ///
    /// World-anchored by construction: moving the mesh slides it through the
    /// pattern, which is the trade triplanar makes everywhere it is used.
    float triplanarScale = 1.0F;

    /// The custom shader module this material is shaded by (T0142.15, D28).
    ///
    /// **Default means the standard material** — every other field below and
    /// above still applies, because the module *extends* the standard
    /// material rather than replacing the pipeline: it defines
    /// `struct HpMaterial : IHpMaterial` and overrides the methods it wants,
    /// with `override` mandatory, and everything it does not override keeps
    /// the engine's behaviour, textures and factors included.
    ///
    /// Names a `ShaderAsset` (a `.slang` file imported like any content).
    /// A GUID that does not resolve renders the missing-material pattern
    /// (T0141.12); a module that does not compile renders the same pattern
    /// with the compiler's error in the log (T0141.4).
    Guid shader;

    /// The first UV channel's transform, used by every slot whose selector is 0.
    UvChannel uv0;

    /// The second UV channel's transform, used by every slot whose selector is 1.
    ///
    /// **A second channel is what a lightmap, a detail map or an atlas needs**:
    /// one set of coordinates laid out for tiling the surface, another laid out
    /// for a unique unwrap. A mesh that carries only one set makes this inert —
    /// `SelectUV` falls back to whichever it has.
    UvChannel uv1;

    /// Which UV channel the base colour texture samples with. 0 or 1.
    ///
    /// **Per slot, because that is the whole reason two channels exist.** A
    /// material with a tiling base colour and a unique-unwrapped occlusion map
    /// is the ordinary case, not an exotic one, and a single material-wide
    /// selector could not express it.
    std::uint8_t baseColourUv = 0;

    /// Which UV channel the metallic-roughness texture samples with.
    std::uint8_t metallicRoughnessUv = 0;

    /// Which UV channel the normal map samples with.
    std::uint8_t normalUv = 0;

    /// Which UV channel the occlusion texture samples with. **Frequently 1** —
    /// baked occlusion usually comes from the same unique unwrap a lightmap does.
    std::uint8_t occlusionUv = 0;

    /// Which UV channel the emissive texture samples with.
    std::uint8_t emissiveUv = 0;

    /// Values for the parameters `shader`'s module declares (T0160.3).
    ///
    /// **Ordered and by name, not a map**, for two reasons that pull the same
    /// way: a person editing a `.hpmat` should see parameters in the order the
    /// shader declares them rather than alphabetically, and the reflected
    /// serializer bottoms out in sequences and leaves — an associative
    /// container is a shape it has never had to write, and inventing one for
    /// this would be T0020's ticket, not this one.
    ///
    /// **Changing a value here rebuilds no pipeline and invalidates no cook.**
    /// The values are written into a constant buffer per draw, exactly as the
    /// engine's own material attribs are; module *identity* already keys the
    /// pipeline cache, so parameters add no permutation axis at all. That is
    /// the point, and it is recorded against T0151.
    ///
    /// Empty for every material without a custom shader, which is what makes
    /// this cost nothing to have.
    std::vector<MaterialParam> params;

    /// Textures for the slots `shader`'s module uses (T0160.3).
    ///
    /// Keyed by the slot's engine-given name, `HpTexture0` … `HpTexture3`.
    std::vector<MaterialTexture> textures;
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
///
/// **2 since T0160**, which added `params` and `textures`. Every version-1
/// document still loads unchanged — reading is lenient and both keys are
/// absent from them — and the bump is the signal in the other direction: a
/// build that predates this refuses a document that might carry parameters it
/// would drop on the next save.
inline constexpr std::uint32_t kMaterialSchemaVersion = 2;

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

// --- the missing-material convention (60.10) --------------------------------
//
// **Three states, and conflating the first two would make every unassigned mesh
// in the project look broken.** That is the whole reason this is a resolver
// returning a state rather than a `shared_ptr` that is null on both "nothing
// assigned" and "assigned but broken".
//
// | Slot | Meaning | What renders |
// |---|---|---|
// | default GUID, or no slot at that index | Nothing assigned. **Legitimate and the common case** | The model's imported material |
// | names an asset that is not in the pool | An error | The missing-material pattern |
// | names a loaded material | Assigned | That material |
//
// **The policy lives here and the drawing lives in T0141.12**, which is the
// re-cut: deciding *what a slot means* is data-model work and needs no device,
// so it is testable in the fast bucket and cannot drift with the shader. T0141's
// failed-compile path (141.4) reaches the same convention rather than inventing
// a second one — one visual language for "something is wrong here", with the log
// saying which.

/// What a material slot resolved to.
enum class MaterialSlot : std::uint8_t {
    /// Nothing is assigned to this surface, so the model's own material is used.
    ///
    /// **Not an error, and by far the most common state.** An untouched import
    /// is entirely this. Rendering the missing-material pattern here would make
    /// every unassigned surface in a project look broken.
    Imported,

    /// A material asset is assigned and loaded.
    Assigned,

    /// A material is assigned and **could not be resolved** — not in the pool,
    /// or it failed to load. Renders the missing-material pattern (T0141.12).
    Missing,
};

/// A slot, resolved against a pool.
struct ResolvedMaterial {
    /// Which of the three states this slot is in.
    MaterialSlot state = MaterialSlot::Imported;

    /// The GUID the slot named. Default when `state` is `Imported`; **valid and
    /// worth logging** when `Missing`, because "which asset is missing" is the
    /// only question a developer has at that point.
    Guid guid;

    /// The material, non-null **only** when `state` is `Assigned`.
    ///
    /// Held by `shared_ptr` so a draw keeps the material alive even if the pool
    /// drops it mid-frame (T0058's hot reload is exactly that).
    std::shared_ptr<Material> material;
};

/// Resolves one surface's material slot (60.10).
///
/// @param pool the pool to look the material up in.
/// @param slots the `MeshRenderer::materials` overrides, which may be empty.
/// @param surface the surface index, matching the model's `primitive.MaterialId`.
/// @returns what the slot resolved to. **An index past the end of @p slots is
///          `Imported`, not an error** — that is what makes an empty override
///          vector mean "use the import for everything" and lets the renderer
///          index without first checking the length against the model's
///          material count.
[[nodiscard]] HP_API ResolvedMaterial resolveMaterialSlot(const AssetPool& pool,
                                                          const std::vector<Guid>& slots,
                                                          std::size_t surface);

/// The material a surface is shaded with when its slot is `Missing`.
///
/// **Unlit and magenta**, and the unlit part is the one that gets missed: a
/// magenta surface standing in shadow reads as plausible art, and an unlit one
/// cannot. Visible, never invisible — a mesh that disappears is a much harder
/// bug to find than an ugly one.
///
/// The **checkerboard** comes from binding `makePlaceholderTexture` (T0023.6) as
/// this material's base colour map, which needs a device and is therefore
/// T0141.12's half. What is here is the part that needs none, so the convention
/// is pinned by a test rather than by a comment.
/// @returns the fallback material.
[[nodiscard]] HP_API Material missingMaterial();

} // namespace hp
